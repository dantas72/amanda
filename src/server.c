#include "server.h"
#include "decision_engine.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
typedef SOCKET sock_t;
#define SOCK_INVALID INVALID_SOCKET
#define sock_close closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int sock_t;
#define SOCK_INVALID -1
#define sock_close close
#endif

static char *json_find_string(const char *body, const char *key) {
    /* procura "key" : "valor" (ultima ocorrencia) */
    char pat[128];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *last = NULL;
    for (const char *p = body; (p = strstr(p, pat)) != NULL; p += strlen(pat))
        last = p;
    if (!last) return NULL;
    const char *c = strchr(last + strlen(pat), ':');
    if (!c) return NULL;
    c++;
    while (*c && isspace((unsigned char)*c)) c++;
    if (*c != '"') return NULL;
    c++;
    ByteBuf b; buf_init(&b);
    while (*c && *c != '"') {
        if (*c == '\\' && c[1]) {
            char e = c[1];
            if (e == 'n') buf_append(&b, "\n", 1);
            else if (e == 't') buf_append(&b, "\t", 1);
            else if (e == 'r') buf_append(&b, "\r", 1);
            else if (e == 'u' && isxdigit((unsigned char)c[2])) {
                /* simplificacao: ignora unicode, insere ? */
                buf_append(&b, "?", 1);
                c += 6;
                continue;
            } else buf_append(&b, &e, 1);
            c += 2;
        } else {
            buf_append(&b, c, 1);
            c++;
        }
    }
    buf_reserve(&b, 1);
    b.data[b.len] = '\0';
    return (char *)b.data;
}

/* extrai ultima mensagem user: procura todas "content" e pega a ultima nao vazia */
static char *extract_prompt(const char *body) {
    char *last = NULL;
    const char *p = body;
    while (1) {
        char *v = json_find_string(p, "content");
        if (!v) break;
        free(last);
        last = v;
        /* avanca p para depois desta ocorrencia */
        const char *f = strstr(p, "\"content\"");
        p = f ? f + 9 : p + 1;
        if (!*p) break;
    }
    if (!last || !last[0]) {
        free(last);
        /* fallback: campo "pergunta" ou "input" ou "prompt" */
        last = json_find_string(body, "pergunta");
        if (!last) last = json_find_string(body, "input");
        if (!last) last = json_find_string(body, "prompt");
    }
    return last;
}

static void send_json(sock_t fd, int code, const char *status, const char *json) {
    char head[512];
    int hl = snprintf(head, sizeof head,
        "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\n"
        "Content-Length: %llu\r\nConnection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n\r\n",
        code, status, (unsigned long long)strlen(json));
    send(fd, head, hl, 0);
    send(fd, json, (int)strlen(json), 0);
}

static int handle_conn(sock_t fd, AmandaPackage *pkg) {
    char buf[65536];
    int got = 0;
    /* le cabecalho */
    while (got < (int)sizeof(buf) - 1) {
        int r = recv(fd, buf + got, (int)sizeof(buf) - 1 - got, 0);
        if (r <= 0) break;
        got += r;
        buf[got] = '\0';
        if (strstr(buf, "\r\n\r\n")) break;
    }
    if (got <= 0) return 0;
    buf[got] = '\0';

    char method[16] = {0}, path[512] = {0};
    sscanf(buf, "%15s %511s", method, path);

    int content_len = 0;
    char *cl = strstr(buf, "Content-Length:");
    if (!cl) cl = strstr(buf, "content-length:");
    if (cl) content_len = atoi(cl + 15);

    char *hdr_end = strstr(buf, "\r\n\r\n");
    int hdr_len = hdr_end ? (int)(hdr_end + 4 - buf) : got;
    int body_have = got - hdr_len;
    ByteBuf body; buf_init(&body);
    if (hdr_end && body_have > 0) buf_append(&body, hdr_end + 4, (size_t)body_have);
    while ((int)body.len < content_len) {
        int r = recv(fd, buf, sizeof(buf), 0);
        if (r <= 0) break;
        buf_append(&body, buf, (size_t)r);
    }
    buf_reserve(&body, 1);
    body.data[body.len] = '\0';
    const char *bstr = (const char *)body.data;

    /* CORS preflight */
    if (strcmp(method, "OPTIONS") == 0) {
        const char *ok = "HTTP/1.1 204 No Content\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        send(fd, ok, (int)strlen(ok), 0);
        buf_free(&body);
        return 0;
    }

    if (strcmp(method, "GET") == 0 && (strcmp(path, "/v1/models") == 0 || strcmp(path, "/v1/models/") == 0)) {
        char js[512];
        snprintf(js, sizeof js,
            "{\"object\":\"list\",\"data\":[{\"id\":\"amanda\",\"object\":\"model\",\"owned_by\":\"amanda\",\"permission\":[]}]}");
        send_json(fd, 200, "OK", js);
    } else if (strcmp(method, "GET") == 0 && strncmp(path, "/v1/amanda/info", 15) == 0) {
        char st[1024];
        package_stats(pkg, st, sizeof st);
        char *esc_t = json_escape(pkg->titulo ? pkg->titulo : "");
        char *js = (char *)xmalloc(2048);
        snprintf(js, 2048,
            "{\"titulo\":\"%s\",\"chunks\":%d,\"perguntas\":%d,\"dimensao\":%d,\"idioma\":\"%s\",\"versao\":\"%s\"}",
            esc_t, pkg->num_chunks, pkg->num_perguntas,
            pkg->embeddings ? pkg->embeddings->dimensao : 0,
            pkg->idioma ? pkg->idioma : "pt-BR",
            pkg->versao_app ? pkg->versao_app : "");
        free(esc_t);
        send_json(fd, 200, "OK", js);
        free(js);
    } else if (strcmp(method, "POST") == 0 && strncmp(path, "/v1/chat/completions", 22) == 0) {
        char *prompt = extract_prompt(bstr);
        if (!prompt || !prompt[0]) {
            free(prompt);
            send_json(fd, 400, "Bad Request", "{\"error\":\"campo messages[].content ausente\"}");
        } else {
            DecisionConfig cfg = {0.7f, 0.3f, 3};
            float conf = 0; int pg = 0;
            char *ans = montar_resposta_chat(prompt, pkg->chunks, pkg->num_chunks,
                                             pkg->embeddings, &cfg, &conf, &pg);
            char *esc = json_escape(ans);
            char *js = (char *)xmalloc(strlen(esc) + 1024);
            snprintf(js, strlen(esc) + 1024,
                "{\"id\":\"chatcmpl-amanda\",\"object\":\"chat.completion\",\"model\":\"amanda\","
                "\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\",\"content\":\"%s\"},\"finish_reason\":\"stop\"}],"
                "\"amanda\":{\"confianca\":%.3f,\"pagina\":%d}}",
                esc, conf, pg);
            free(esc);
            free(ans);
            free(prompt);
            send_json(fd, 200, "OK", js);
            free(js);
        }
    } else if (strcmp(method, "POST") == 0 && strncmp(path, "/v1/decisions", 14) == 0) {
        char *prompt = extract_prompt(bstr);
        if (!prompt || !prompt[0]) {
            free(prompt);
            send_json(fd, 400, "Bad Request", "{\"error\":\"campo pergunta ausente\"}");
        } else {
            DecisionConfig cfg = {0.7f, 0.3f, 3};
            Decisao *d = executar_decisao(prompt, pkg->chunks, pkg->num_chunks, pkg->embeddings, &cfg);
            char *esc = json_escape(d->resposta);
            char *escc = json_escape(d->citacao ? d->citacao : "");
            char *js = (char *)xmalloc(strlen(esc) + strlen(escc) + 512);
            snprintf(js, strlen(esc) + strlen(escc) + 512,
                "{\"resposta\":\"%s\",\"probabilidade\":%.4f,\"confianca\":%.4f,\"pagina\":%d,\"citacao\":\"%s\",\"recusada\":%s}",
                esc, d->probabilidade, d->confianca, d->pagina, escc,
                d->recusada ? "true" : "false");
            free(esc); free(escc);
            liberar_decisao(d);
            free(prompt);
            send_json(fd, 200, "OK", js);
            free(js);
        }
    } else {
        send_json(fd, 404, "Not Found", "{\"error\":\"rota nao encontrada\"}");
    }
    buf_free(&body);
    return 0;
}

int server_run(const ServerConfig *cfg) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "amandac: falha no WSAStartup\n");
        return 1;
    }
#endif
    sock_t srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv == SOCK_INVALID) {
        fprintf(stderr, "amandac: falha ao criar socket\n");
        return 1;
    }
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof opt);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)cfg->port);
    if (!cfg->host || strcmp(cfg->host, "0.0.0.0") == 0) addr.sin_addr.s_addr = htonl(INADDR_ANY);
    else addr.sin_addr.s_addr = inet_addr(cfg->host);

    if (bind(srv, (struct sockaddr *)&addr, sizeof addr) != 0) {
        fprintf(stderr, "amandac: falha no bind %s:%d\n", cfg->host, cfg->port);
        sock_close(srv);
        return 1;
    }
    if (listen(srv, 16) != 0) {
        fprintf(stderr, "amandac: falha no listen\n");
        sock_close(srv);
        return 1;
    }
    printf("amandac serve: http://%s:%d (pacote: %d chunks)\n",
           cfg->host ? cfg->host : "127.0.0.1", cfg->port,
           cfg->pkg ? cfg->pkg->num_chunks : 0);
    printf("rotas: GET /v1/models | GET /v1/amanda/info | POST /v1/chat/completions | POST /v1/decisions\n");
    fflush(stdout);

    for (;;) {
        if (cfg->stop_flag && *cfg->stop_flag) break;
        struct sockaddr_in cli;
        socklen_t cl = sizeof cli;
        sock_t fd = accept(srv, (struct sockaddr *)&cli, &cl);
        if (fd == SOCK_INVALID) continue;
        handle_conn(fd, cfg->pkg);
        sock_close(fd);
    }
    sock_close(srv);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
