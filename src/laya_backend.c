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

/* Separa URL completa em base (scheme://host[:port]) e path (/...).
 * Suporta http e https. Retorna 0 ok. */
static int split_url(const char *url, char *base, size_t base_sz,
                     char *path, size_t path_sz) {
    if (!url || !base || !path) return -1;
    const char *p = url;
    int https = 0;
    if (strncmp(p, "http://", 7) == 0) p += 7;
    else if (strncmp(p, "https://", 8) == 0) { p += 8; https = 1; }
    else return -1;
    const char *slash = strchr(p, '/');
    size_t hlen = slash ? (size_t)(slash - p) : strlen(p);
    if (hlen == 0 || hlen + 9 >= base_sz) return -1;
    snprintf(base, base_sz, "%s://%.*s", https ? "https" : "http",
             (int)hlen, p);
    if (slash) snprintf(path, path_sz, "%s", slash);
    else snprintf(path, path_sz, "/");
    return 0;
}

#ifdef _WIN32
#include <process.h>
#endif

/* POST https via curl do sistema (TLS real). Corpo vai por arquivo
 * temporario; Bearer via arquivo de headers (nunca na linha de
 * comando). Retorna 0 com codigo+corpo, != 0 em falha. */
static int https_post_curl(const char *url, const char *bearer,
                           const char *body, int timeout_ms,
                           int *code_out, char **rbody_out, char **erro) {
    char bodyf[1024] = {0};
    char hdrf[1024] = {0};
    int ok = 0;
#ifdef _WIN32
    {
        char tpath[MAX_PATH];
        DWORD tl = GetTempPathA(sizeof tpath, tpath);
        if (tl == 0 || tl >= sizeof tpath) {
            if (erro) *erro = xstrdup("http: sem diretorio temporario");
            return -1;
        }
        /* GetTempFileNameA e atomico (arquivo criado com nome unico). */
        if (GetTempFileNameA(tpath, "amn", 0, bodyf) == 0) {
            if (erro) *erro = xstrdup("http: falha ao criar temporario");
            return -1;
        }
        snprintf(hdrf, sizeof hdrf, "%s.hdr", bodyf);
    }
#else
    {
        snprintf(bodyf, sizeof bodyf, "/tmp/amanda_http_XXXXXX");
        int fd = mkstemp(bodyf);
        if (fd < 0) {
            if (erro) *erro = xstrdup("http: falha ao criar temporario");
            return -1;
        }
        close(fd);
        snprintf(hdrf, sizeof hdrf, "%s.hdr", bodyf);
    }
#endif
    FILE *fb = fopen(bodyf, "wb");
    if (!fb) {
        if (erro) *erro = xstrdup("http: falha ao escrever temporario");
        remove(bodyf);
        return -1;
    }
    fwrite(body ? body : "", 1, body ? strlen(body) : 0, fb);
    fclose(fb);
    int use_hdr = (bearer && bearer[0]) ? 1 : 0;
    if (use_hdr) {
        FILE *fh = fopen(hdrf, "wb");
        if (!fh) {
            if (erro) *erro = xstrdup("http: falha ao escrever headers");
            remove(bodyf);
            return -1;
        }
        fprintf(fh, "Authorization: Bearer %s\r\n", bearer);
        fclose(fh);
    }

    long secs = (timeout_ms + 999) / 1000;
    if (secs < 1) secs = 1;
    long conn = (secs > 30) ? 30 : secs;
    char cmd[4096];
    if (use_hdr)
        snprintf(cmd, sizeof cmd,
                 "curl -s -X POST \"%s\" --max-time %ld --connect-timeout %ld "
                 "-H \"Content-Type: application/json\" -H @\"%s\" "
                 "--data-binary @\"%s\" -w \"\nHTTP_CODE:%%{http_code}\"",
                 url, secs, conn, hdrf, bodyf);
    else
        snprintf(cmd, sizeof cmd,
                 "curl -s -X POST \"%s\" --max-time %ld --connect-timeout %ld "
                 "-H \"Content-Type: application/json\" "
                 "--data-binary @\"%s\" -w \"\nHTTP_CODE:%%{http_code}\"",
                 url, secs, conn, bodyf);
#ifdef _WIN32
    FILE *pp = _popen(cmd, "r");
#else
    FILE *pp = popen(cmd, "r");
#endif
    ByteBuf out;
    buf_init(&out);
    if (pp) {
        char chunk[4096];
        size_t n;
        while ((n = fread(chunk, 1, sizeof chunk, pp)) > 0) {
            if (out.len + n > 1024u * 1024u) break;
            buf_append(&out, chunk, n);
        }
#ifdef _WIN32
        _pclose(pp);
#else
        pclose(pp);
#endif
    }
    remove(bodyf);
    if (use_hdr) remove(hdrf);
    if (out.len == 0) {
        buf_free(&out);
        if (erro) *erro = xstrdup("http: curl sem resposta (sem rede? sem curl?)");
        return -1;
    }
    buf_reserve(&out, 1);
    out.data[out.len] = '\0';
    char *mark = NULL;
    /* ultimo marcador (corpo pode conter texto parecido) */
    for (char *q = (char *)out.data; (q = strstr(q, "\nHTTP_CODE:")) != NULL; q++)
        mark = q;
    if (!mark) {
        buf_free(&out);
        if (erro) *erro = xstrdup("http: resposta curl sem codigo");
        return -1;
    }
    int code = atoi(mark + 12);
    *mark = '\0';
    if (code_out) *code_out = code;
    if (rbody_out) *rbody_out = xstrdup((char *)out.data);
    else ok = 0;
    buf_free(&out);
    (void)ok;
    return 0;
}

int http_post_json(const char *url, const char *bearer, const char *body,
                   int timeout_ms, int *code_out, char **rbody_out,
                   char **erro) {
    if (!url || !url[0] || !rbody_out) {
        if (erro) *erro = xstrdup("http: argumentos invalidos");
        return -1;
    }
    if (timeout_ms <= 0) timeout_ms = 60000;
    if (strncmp(url, "https://", 8) == 0)
        return https_post_curl(url, bearer, body, timeout_ms,
                               code_out, rbody_out, erro);
    if (strncmp(url, "http://", 7) != 0) {
        if (erro) *erro = xstrdup("http: URL deve comecar com http:// ou https://");
        return -1;
    }
    char base[512], path[2048];
    if (split_url(url, base, sizeof base, path, sizeof path) != 0) {
        if (erro) *erro = xstrdup("http: URL invalida");
        return -1;
    }
    /* http_roundtrip nao envia Authorization: Bearer em http anda
     * sempre em claro, entao exigimos https quando ha chave. */
    if (bearer && bearer[0]) {
        if (erro) *erro = xstrdup("http: Bearer exige https:// (chave nunca em claro)");
        return -1;
    }
    int code = 0;
    char *rb = NULL;
    char *herr = NULL;
    int rc = http_roundtrip(base, "POST", path, body, timeout_ms,
                            &code, &rb, &herr);
    if (rc != 0) {
        if (erro) *erro = herr ? herr : xstrdup("http: falha de transporte");
        else free(herr);
        return -1;
    }
    if (code_out) *code_out = code;
    *rbody_out = rb;
    return 0;
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

/* ==================== Pool LLM com prioridade ==================== */

#ifdef _WIN32
#include <process.h>
#endif
#ifndef _WIN32
#include <pthread.h>
#include <sys/time.h>
#endif

/* No de espera: vive na pilha do thread que chama adquirir (o thread
 * fica vivo enquanto espera, logo o no e valido sob o mutex). */
typedef struct LlmWaiter {
    int prio;
    long seq;
    int granted;
    struct LlmWaiter *next;
} LlmWaiter;

struct LlmPool {
    int max_slots;
    int max_fila;
    int active;
    long seq_next;
    LlmWaiter *head;
    LlmWaiter *tail;
    int n_wait;
    long atendidas;
    long fb_fila;
    long fb_tempo;
#ifdef _WIN32
    CRITICAL_SECTION cs;
    CONDITION_VARIABLE cv;
#else
    pthread_mutex_t mtx;
    pthread_cond_t cv;
#endif
};

LlmPool *llm_pool_criar(int max_slots, int max_fila) {
    LlmPool *p = (LlmPool *)calloc(1, sizeof *p);
    if (!p) return NULL;
    p->max_slots = (max_slots > 0) ? max_slots : LLM_POOL_SLOTS_DEFAULT;
    p->max_fila = (max_fila >= 0) ? max_fila : 0;
#ifdef _WIN32
    InitializeCriticalSection(&p->cs);
    InitializeConditionVariable(&p->cv);
#else
    pthread_mutex_init(&p->mtx, NULL);
    pthread_cond_init(&p->cv, NULL);
#endif
    return p;
}

void llm_pool_liberar(LlmPool *p) {
    if (!p) return;
#ifdef _WIN32
    DeleteCriticalSection(&p->cs);
#else
    pthread_mutex_destroy(&p->mtx);
    pthread_cond_destroy(&p->cv);
#endif
    free(p);
}

static void pool_lock(LlmPool *p) {
#ifdef _WIN32
    EnterCriticalSection(&p->cs);
#else
    pthread_mutex_lock(&p->mtx);
#endif
}

static void pool_unlock(LlmPool *p) {
#ifdef _WIN32
    LeaveCriticalSection(&p->cs);
#else
    pthread_mutex_unlock(&p->mtx);
#endif
}

static void pool_broadcast(LlmPool *p) {
#ifdef _WIN32
    WakeAllConditionVariable(&p->cv);
#else
    pthread_cond_broadcast(&p->cv);
#endif
}

/* Espera ate ms (relativo). Retorna 1 se acordou por sinal, 0 se o
 * prazo estourou (ou erro). Chamar com o mutex preso (o Windows e o
 * POSIX liberam/retomam atomicamente). */
static int pool_timedwait(LlmPool *p, long ms) {
    if (ms <= 0) return 0;
#ifdef _WIN32
    return SleepConditionVariableCS(&p->cv, &p->cs, (DWORD)ms) ? 1 : 0;
#else
    struct timeval now;
    gettimeofday(&now, NULL);
    long long ns = (long long)now.tv_sec * 1000000000LL +
                   (long long)now.tv_usec * 1000LL +
                   (long long)ms * 1000000LL;
    struct timespec ts;
    ts.tv_sec = (time_t)(ns / 1000000000LL);
    ts.tv_nsec = (long)(ns % 1000000000LL);
    return (pthread_cond_timedwait(&p->cv, &p->mtx, &ts) == 0) ? 1 : 0;
#endif
}

int llm_pool_adquirir(LlmPool *p, int prioridade, int espera_ms) {
    if (!p) return 0;
    int prio = (prioridade == LLM_PRIO_ALTA) ? LLM_PRIO_ALTA : LLM_PRIO_NORMAL;
    pool_lock(p);
    /* Caminho rapido: slot livre e ninguem na frente. */
    if (p->active < p->max_slots && p->head == NULL) {
        p->active++;
        p->atendidas++;
        pool_unlock(p);
        return 1;
    }
    if (espera_ms <= 0 || p->n_wait >= p->max_fila) {
        /* Sem espera pedida, ou fila cheia: fallback honesto. */
        if (espera_ms > 0) p->fb_fila++;
        else p->fb_tempo++;
        pool_unlock(p);
        return 0;
    }
    LlmWaiter me;
    me.prio = prio;
    me.seq = p->seq_next++;
    me.granted = 0;
    me.next = NULL;
    if (p->tail) p->tail->next = &me;
    else p->head = &me;
    p->tail = &me;
    p->n_wait++;
    long long deadline = now_ms() + (long long)espera_ms;
    for (;;) {
        long long rest = deadline - now_ms();
        if (me.granted) break;
        if (rest <= 0) {
            /* Prazo esgotado. Se o devolver nos escolheu na mesma
             * janela (granted sob o mutex), o slot e nosso: sucesso.
             * Senao, saimos da fila (fallback por tempo). */
            if (me.granted) break;
            LlmWaiter **pp = &p->head;
            while (*pp && *pp != &me) pp = &(*pp)->next;
            if (*pp) {
                *pp = me.next;
                p->n_wait--;
                p->tail = NULL;
                for (LlmWaiter *t = p->head; t; t = t->next) p->tail = t;
            }
            p->fb_tempo++;
            pool_unlock(p);
            return 0;
        }
        pool_timedwait(p, (long)(rest > 60000 ? 60000 : rest));
    }
    p->atendidas++;
    pool_unlock(p);
    return 1;
}

void llm_pool_devolver(LlmPool *p) {
    if (!p) return;
    pool_lock(p);
    /* Passa o slot ao 1o ALTA (FIFO entre iguais); senao ao 1o NORMAL. */
    LlmWaiter **best = NULL;
    for (LlmWaiter **pp = &p->head; *pp; pp = &(*pp)->next) {
        if ((*pp)->prio == LLM_PRIO_ALTA) { best = pp; break; }
        if (!best) best = pp;
    }
    if (best) {
        LlmWaiter *w = *best;
        *best = w->next;
        p->n_wait--;
        p->tail = NULL;
        for (LlmWaiter *t = p->head; t; t = t->next) p->tail = t;
        w->granted = 1;
        /* active segue ocupado: transferencia direta ao escolhido. */
        pool_broadcast(p);
    } else {
        if (p->active > 0) p->active--;
        pool_broadcast(p);
    }
    pool_unlock(p);
}

void llm_pool_stats(LlmPool *p, long *atendidas_out,
                    long *fb_fila_out, long *fb_tempo_out) {
    if (!p) return;
    pool_lock(p);
    if (atendidas_out) *atendidas_out = p->atendidas;
    if (fb_fila_out) *fb_fila_out = p->fb_fila;
    if (fb_tempo_out) *fb_tempo_out = p->fb_tempo;
    pool_unlock(p);
}
