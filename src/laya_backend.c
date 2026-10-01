#include "laya_backend.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define SOCK_INVALID INVALID_SOCKET
#define sock_close closesocket
#else
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
typedef int sock_t;
#define SOCK_INVALID -1
#define sock_close close
#endif

#define LAYA_RECV_CAP (256u * 1024u)

static const char *LAYA_NO_MODEL_MSG =
    "I'm sorry, I encountered an error processing your message. Please try again.";

char *laya_extract_content(const char *body) {
    if (!body) return NULL;
    const char *p = strstr(body, "\"content\"");
    if (!p) return NULL;
    p = strchr(p + 9, ':');
    if (!p) return NULL;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != '"') return NULL;
    p++;
    ByteBuf b; buf_init(&b);
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) {
            char e = p[1];
            if (e == 'n') buf_append(&b, "\n", 1);
            else if (e == 't') buf_append(&b, "\t", 1);
            else if (e == 'r') buf_append(&b, "\r", 1);
            else if (e == 'u') {
                buf_append(&b, "?", 1);
                if (p[2] && p[3] && p[4] && p[5]) p += 4;
            } else buf_append(&b, &e, 1);
            p += 2;
        } else {
            buf_append(&b, p, 1);
            p++;
        }
    }
    buf_reserve(&b, 1);
    b.data[b.len] = '\0';
    return (char *)b.data;
}

/* "models" : [ <nao vazio> ] ? */
static int json_array_nonempty(const char *body, const char *key) {
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    for (const char *p = body; (p = strstr(p, pat)) != NULL; p += strlen(pat)) {
        const char *c = strchr(p + strlen(pat), ':');
        if (!c) continue;
        c++;
        while (*c && isspace((unsigned char)*c)) c++;
        if (*c != '[') continue;
        c++;
        while (*c && isspace((unsigned char)*c)) c++;
        if (*c && *c != ']') return 1;
    }
    return 0;
}

static int parse_base_url(const char *url, char *host, size_t hostsz, int *port) {
    if (!url || !host || !port) return -1;
    const char *p = url;
    if (strncmp(p, "http://", 7) == 0) p += 7;
    else if (strncmp(p, "https://", 8) == 0) return -1;
    const char *slash = strchr(p, '/');
    size_t hlen = slash ? (size_t)(slash - p) : strlen(p);
    if (hlen == 0 || hlen >= hostsz) return -1;
    char tmp[280];
    if (hlen >= sizeof tmp) return -1;
    memcpy(tmp, p, hlen);
    tmp[hlen] = '\0';
    *port = 80;
    char *colon = strrchr(tmp, ':');
    if (colon && strchr(colon, ']') == NULL) {
        *colon = '\0';
        int pr = atoi(colon + 1);
        if (pr > 0 && pr < 65536) *port = pr;
    }
    if (tmp[0] == '\0') return -1;
    memcpy(host, tmp, strlen(tmp) + 1);
    return 0;
}

static int wait_writable(sock_t fd, int timeout_ms) {
    fd_set wf;
    FD_ZERO(&wf);
    FD_SET(fd, &wf);
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int r = select((int)fd + 1, NULL, &wf, NULL, timeout_ms >= 0 ? &tv : NULL);
    return r;
}

static sock_t dial(const char *host, int port, int timeout_ms, char **erro) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        if (erro) *erro = xstrdup("laya: falha no WSAStartup");
        return SOCK_INVALID;
    }
#endif
    sock_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == SOCK_INVALID) {
        if (erro) *erro = xstrdup("laya: falha ao criar socket");
#ifdef _WIN32
        WSACleanup();
#endif
        return SOCK_INVALID;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    unsigned long ip = inet_addr(host);
    if (ip != INADDR_NONE) {
        addr.sin_addr.s_addr = ip;
    } else {
        struct hostent *he = gethostbyname(host);
        if (!he) {
            if (erro) *erro = xstrdup("laya: host nao resolvido");
            sock_close(fd);
#ifdef _WIN32
            WSACleanup();
#endif
            return SOCK_INVALID;
        }
        memcpy(&addr.sin_addr, he->h_addr_list[0], (size_t)he->h_length);
    }
#ifdef _WIN32
    {
        u_long nb = 1;
        ioctlsocket(fd, FIONBIO, &nb);
    }
#else
    {
        int fl = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    }
#endif
    int cr = connect(fd, (struct sockaddr *)&addr, sizeof addr);
    if (cr != 0) {
#ifdef _WIN32
        int e = WSAGetLastError();
        if (e != WSAEWOULDBLOCK && e != WSAEINPROGRESS) {
            if (erro) *erro = xstrdup("laya: conexao recusada");
            sock_close(fd);
            WSACleanup();
            return SOCK_INVALID;
        }
#else
        if (errno != EINPROGRESS) {
            if (erro) *erro = xstrdup("laya: conexao recusada");
            sock_close(fd);
            return SOCK_INVALID;
        }
#endif
        if (wait_writable(fd, timeout_ms) <= 0) {
            if (erro) *erro = xstrdup("laya: timeout na conexao");
            sock_close(fd);
#ifdef _WIN32
            WSACleanup();
#endif
            return SOCK_INVALID;
        }
        int soerr = 0;
        socklen_t sl = sizeof soerr;
        getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&soerr, &sl);
        if (soerr != 0) {
            if (erro) *erro = xstrdup("laya: conexao recusada");
            sock_close(fd);
#ifdef _WIN32
            WSACleanup();
#endif
            return SOCK_INVALID;
        }
    }
#ifdef _WIN32
    {
        u_long nb = 0;
        ioctlsocket(fd, FIONBIO, &nb);
        DWORD tv = (DWORD)(timeout_ms > 0 ? timeout_ms : 1);
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof tv);
    }
#else
    {
        int fl = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, fl & ~O_NONBLOCK);
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        if (tv.tv_sec == 0 && tv.tv_usec == 0) tv.tv_usec = 1000;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    }
#endif
    return fd;
}

static void dial_close(sock_t fd) {
    sock_close(fd);
#ifdef _WIN32
    WSACleanup();
#endif
}

static int send_all_nb(sock_t fd, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
#ifdef _WIN32
        int r = send(fd, data + sent, (int)(len - sent), 0);
#else
        ssize_t r = send(fd, data + sent, len - sent, 0);
#endif
        if (r <= 0) return -1;
        sent += (size_t)r;
    }
    return 0;
}

static int http_roundtrip(const char *base_url, const char *method, const char *path,
                          const char *body, int timeout_ms,
                          int *status_out, char **body_out, char **erro) {
    char host[256];
    int port = 80;
    if (parse_base_url(base_url, host, sizeof host, &port) != 0) {
        if (erro) *erro = xstrdup("laya: URL invalida (use http://host:porta)");
        return -1;
    }
    sock_t fd = dial(host, port, timeout_ms, erro);
    if (fd == SOCK_INVALID) return -1;

    char head[1024];
    size_t blen = body ? strlen(body) : 0;
    int hl = snprintf(head, sizeof head,
        "%s %s HTTP/1.0\r\nHost: %s:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %llu\r\nConnection: close\r\n\r\n",
        method, path, host, port, (unsigned long long)blen);
    if (send_all_nb(fd, head, (size_t)hl) != 0 ||
        (blen && send_all_nb(fd, body, blen) != 0)) {
        if (erro) *erro = xstrdup("laya: falha ao enviar requisicao");
        dial_close(fd);
        return -1;
    }
    ByteBuf resp; buf_init(&resp);
    char chunk[4096];
    for (;;) {
#ifdef _WIN32
        int r = recv(fd, chunk, sizeof chunk, 0);
#else
        ssize_t r = recv(fd, chunk, sizeof chunk, 0);
#endif
        if (r <= 0) break;
        if (resp.len + (size_t)r > LAYA_RECV_CAP) break;
        buf_append(&resp, chunk, (size_t)r);
    }
    dial_close(fd);
    if (resp.len == 0) {
        if (erro) *erro = xstrdup("laya: resposta vazia");
        buf_free(&resp);
        return -1;
    }
    buf_reserve(&resp, 1);
    resp.data[resp.len] = '\0';
    const char *txt = (const char *)resp.data;
    int code = 0;
    if (sscanf(txt, "HTTP/%*d.%*d %d", &code) != 1) code = 0;
    if (status_out) *status_out = code;
    const char *sep = strstr(txt, "\r\n\r\n");
    const char *bstart = sep ? sep + 4 : txt;
    if (body_out) *body_out = xstrdup(bstart);
    buf_free(&resp);
    return 0;
}

LayaStatus laya_chat(const char *base_url, const char *message, int timeout_ms,
                     char **content_out, char **erro) {
    if (!message || !content_out) return LAYA_ERR_ARGS;
    if (!base_url || !base_url[0]) base_url = LAYA_URL_DEFAULT;
    if (timeout_ms <= 0) timeout_ms = LAYA_TIMEOUT_DEFAULT_MS;
    char *esc = json_escape(message);
    ByteBuf jb; buf_init(&jb);
    buf_append_cstr(&jb, "{\"message\":\"");
    buf_append_cstr(&jb, esc);
    buf_append_cstr(&jb, "\"}");
    free(esc);
    buf_reserve(&jb, 1);
    jb.data[jb.len] = '\0';

    int code = 0;
    char *rbody = NULL;
    char *herr = NULL;
    int rc = http_roundtrip(base_url, "POST", "/chat", (char *)jb.data,
                            timeout_ms, &code, &rbody, &herr);
    buf_free(&jb);
    if (rc != 0) {
        if (erro) *erro = herr ? herr : xstrdup("laya: engine indisponivel");
        else free(herr);
        return LAYA_UNAVAILABLE;
    }
    if (code < 200 || code >= 300) {
        if (erro) {
            char tmp[128];
            snprintf(tmp, sizeof tmp, "laya: HTTP %d no /chat", code);
            *erro = xstrdup(tmp);
        }
        free(rbody);
        return LAYA_UNAVAILABLE;
    }
    char *content = laya_extract_content(rbody);
    free(rbody);
    if (!content || !content[0]) {
        free(content);
        if (erro) *erro = xstrdup("laya: resposta sem conteudo");
        return LAYA_UNAVAILABLE;
    }
    if (strcmp(content, LAYA_NO_MODEL_MSG) == 0) {
        free(content);
        if (erro) *erro = xstrdup("laya: engine sem modelo ativo (configure o slot chat no Settings)");
        return LAYA_UNAVAILABLE;
    }
    *content_out = content;
    return LAYA_OK;
}

int laya_providers_ready(const char *base_url, int timeout_ms) {
    if (!base_url || !base_url[0]) base_url = LAYA_URL_DEFAULT;
    if (timeout_ms <= 0) timeout_ms = 8000;
    int code = 0;
    char *rbody = NULL;
    int rc = http_roundtrip(base_url, "GET", "/settings/available-models",
                            NULL, timeout_ms, &code, &rbody, NULL);
    if (rc != 0) return 0;
    int ok = (code >= 200 && code < 300 && json_array_nonempty(rbody, "models")) ? 1 : 0;
    free(rbody);
    return ok;
}
