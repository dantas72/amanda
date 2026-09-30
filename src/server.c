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

static void send_all(sock_t fd, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int r = send(fd, data + sent, (int)(len - sent), 0);
        if (r <= 0) break;
        sent += (size_t)r;
    }
}

static void send_json(sock_t fd, int code, const char *status, const char *json) {
    char head[512];
    int hl = snprintf(head, sizeof head,
        "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\n"
        "Content-Length: %llu\r\nConnection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n\r\n",
        code, status, (unsigned long long)strlen(json));
    send_all(fd, head, (size_t)hl);
    send_all(fd, json, strlen(json));
}

/* "stream": true ? */
static int json_find_bool(const char *body, const char *key) {
    char pat[128];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(body, pat);
    if (!p) return 0;
    p = strchr(p + strlen(pat), ':');
    if (!p) return 0;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    return (strncmp(p, "true", 4) == 0) ? 1 : 0;
}

/* extrai "input": "txt" | ["a","b"] para /v1/embeddings */
static char **parse_inputs(const char *body, int *n_out) {
    *n_out = 0;
    const char *p = strstr(body, "\"input\"");
    if (!p) return NULL;
    p = strchr(p + 7, ':');
    if (!p) return NULL;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    int cap = 4, n = 0;
    char **out = (char **)xmalloc(sizeof(char *) * (size_t)cap);
    if (*p == '[') {
        p++;
        while (*p && *p != ']') {
            while (*p && *p != '"' && *p != ']') p++;
            if (*p != '"') break;
            p++;
            ByteBuf b; buf_init(&b);
            while (*p && *p != '"') {
                if (*p == '\\' && p[1]) {
                    char e = p[1];
                    if (e == 'n') buf_append(&b, "\n", 1);
                    else buf_append(&b, &e, 1);
                    p += 2;
                } else { buf_append(&b, p, 1); p++; }
            }
            if (*p == '"') p++;
            buf_reserve(&b, 1); b.data[b.len] = '\0';
            if (n >= cap) { cap *= 2; out = (char **)xrealloc(out, sizeof(char *) * (size_t)cap); }
            out[n++] = (char *)b.data;
        }
    } else if (*p == '"') {
        char *one = json_find_string(body, "input");
        if (one) {
            out[n++] = one;
        }
    }
    if (n == 0) { free(out); return NULL; }
    *n_out = n;
    return out;
}

static void send_sse_chat(sock_t fd, const char *answer, float conf, int pg) {
    const char *head =
        "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\nConnection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n\r\n";
    send_all(fd, head, strlen(head));

    /* fatia a resposta em blocos de ~6 palavras */
    int nw = 0;
    char **words = NULL;
    {
        /* tokenizacao simples por espacos preservando palavras */
        int cap = 64;
        words = (char **)xmalloc(sizeof(char *) * (size_t)cap);
        const char *p = answer;
        while (*p) {
            while (*p && isspace((unsigned char)*p)) p++;
            if (!*p) break;
            const char *s = p;
            while (*p && !isspace((unsigned char)*p)) p++;
            if (nw >= cap) { cap *= 2; words = (char **)xrealloc(words, sizeof(char *) * (size_t)cap); }
            words[nw++] = xstrndup(s, (size_t)(p - s));
        }
    }
    int first = 1;
    ByteBuf piece; buf_init(&piece);
    for (int i = 0; i < nw; i++) {
        if (piece.len) buf_append(&piece, " ", 1);
        buf_append(&piece, words[i], strlen(words[i]));
        int flush = ((i + 1) % 6 == 0) || (i == nw - 1);
        if (!flush) continue;
        buf_reserve(&piece, 1); piece.data[piece.len] = '\0';
        char *esc = json_escape((char *)piece.data);
        if (first) {
            /* primeiro bloco carrega role + dica de citacao */
            char pre[256];
            snprintf(pre, sizeof pre, " [p.%d]", pg);
            char *esc2 = json_escape(pre);
            char *line = (char *)xmalloc(strlen(esc) + strlen(esc2) + 512);
            snprintf(line, strlen(esc) + strlen(esc2) + 512,
                "data: {\"id\":\"chatcmpl-amanda\",\"object\":\"chat.completion.chunk\",\"model\":\"amanda\","
                "\"choices\":[{\"index\":0,\"delta\":{\"role\":\"assistant\",\"content\":\"%s%s\"},\"finish_reason\":null}]}\n\n",
                esc, esc2);
            send_all(fd, line, strlen(line));
            free(line); free(esc2);
            first = 0;
        } else {
            char *line = (char *)xmalloc(strlen(esc) + 320);
            snprintf(line, strlen(esc) + 320,
                "data: {\"id\":\"chatcmpl-amanda\",\"object\":\"chat.completion.chunk\",\"model\":\"amanda\","
                "\"choices\":[{\"index\":0,\"delta\":{\"content\":\"%s \"},\"finish_reason\":null}]}\n\n",
                esc);
            send_all(fd, line, strlen(line));
            free(line);
        }
        free(esc);
        piece.len = 0;
    }
    for (int i = 0; i < nw; i++) free(words[i]);
    free(words);
    buf_free(&piece);
    char tail[512];
    snprintf(tail, sizeof tail,
        "data: {\"id\":\"chatcmpl-amanda\",\"object\":\"chat.completion.chunk\",\"model\":\"amanda\","
        "\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"stop\"}],"
        "\"amanda\":{\"confianca\":%.3f,\"pagina\":%d}}\n\ndata: [DONE]\n\n",
        conf, pg);
    send_all(fd, tail, strlen(tail));
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
            int stream = json_find_bool(bstr, "stream");
            if (stream) {
                send_sse_chat(fd, ans, conf, pg);
            } else {
                char *esc = json_escape(ans);
                char *js = (char *)xmalloc(strlen(esc) + 1024);
                snprintf(js, strlen(esc) + 1024,
                    "{\"id\":\"chatcmpl-amanda\",\"object\":\"chat.completion\",\"model\":\"amanda\","
                    "\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\",\"content\":\"%s\"},\"finish_reason\":\"stop\"}],"
                    "\"amanda\":{\"confianca\":%.3f,\"pagina\":%d}}",
                    esc, conf, pg);
                free(esc);
                send_json(fd, 200, "OK", js);
                free(js);
            }
            free(ans);
            free(prompt);
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
    } else if (strcmp(method, "POST") == 0 && strncmp(path, "/v1/embeddings", 14) == 0) {
        int ni = 0;
        char **inputs = parse_inputs(bstr, &ni);
        if (!inputs || ni == 0) {
            send_json(fd, 400, "Bad Request", "{\"error\":\"campo input ausente (string ou array de strings)\"}");
        } else {
            ByteBuf js; buf_init(&js);
            buf_append_cstr(&js, "{\"object\":\"list\",\"data\":[");
            long total_toks = 0;
            for (int i = 0; i < ni; i++) {
                Embeddings *e = embed_query(inputs[i]);
                int nt = 0;
                char **tk = tokenizar(inputs[i], &nt);
                total_toks += nt;
                liberar_tokens(tk, nt);
                if (i) buf_append(&js, ",", 1);
                char tmp[256];
                snprintf(tmp, sizeof tmp,
                    "{\"object\":\"embedding\",\"index\":%d,\"embedding\":[", i);
                buf_append(&js, tmp, strlen(tmp));
                for (int k = 0; k < e->dimensao; k++) {
                    char num[32];
                    snprintf(num, sizeof num, "%s%.6f", k ? "," : "", e->vetores[k]);
                    buf_append(&js, num, strlen(num));
                }
                buf_append(&js, "]}", 2);
                liberar_embeddings(e);
            }
            char tail[256];
            snprintf(tail, sizeof tail,
                "],\"model\":\"amanda\",\"usage\":{\"prompt_tokens\":%ld,\"total_tokens\":%ld}}",
                total_toks, total_toks);
            buf_append(&js, tail, strlen(tail));
            buf_reserve(&js, 1); js.data[js.len] = '\0';
            send_json(fd, 200, "OK", (char *)js.data);
            buf_free(&js);
            for (int i = 0; i < ni; i++) free(inputs[i]);
            free(inputs);
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
    printf("rotas: GET /v1/models | GET /v1/amanda/info | POST /v1/chat/completions (+stream) | POST /v1/decisions | POST /v1/embeddings\n");
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
