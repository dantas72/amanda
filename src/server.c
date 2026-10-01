#include "server.h"
#include "decision_engine.h"
#include "laya_backend.h"
#include "eval.h"
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
#include <process.h>
typedef SOCKET sock_t;
#define SOCK_INVALID INVALID_SOCKET
#define sock_close closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
typedef int sock_t;
#define SOCK_INVALID -1
#define sock_close close
#endif

/* ============ Fase 7.5: contexto por request (sem globais de negocio) ============ */

typedef struct {
    AmandaPackage *pkg;
    DecisionConfig base;
    const char *cors;
    const char *api_key;
    long max_body;
    int eval_max;
    /* Fase 11 */
    int backend;
    char laya_url[256];
    int laya_timeout_ms;
    int laya_max;
} ReqCtx;

/* ============ Fase 7.5: slots de concorrencia (contador com mutex) ============ */

static int g_slots_active = 0;
#ifdef _WIN32
static CRITICAL_SECTION g_slots_cs;
static int g_slots_once = 0;
static void slots_lock(void) {
    if (!g_slots_once) { InitializeCriticalSection(&g_slots_cs); g_slots_once = 1; }
    EnterCriticalSection(&g_slots_cs);
}
static void slots_unlock(void) { LeaveCriticalSection(&g_slots_cs); }
#else
static pthread_mutex_t g_slots_mtx = PTHREAD_MUTEX_INITIALIZER;
static void slots_lock(void) { pthread_mutex_lock(&g_slots_mtx); }
static void slots_unlock(void) { pthread_mutex_unlock(&g_slots_mtx); }
#endif

/* Tenta ocupar um slot; 1 = ok, 0 = cheio. */
static int slots_try_acquire(int max) {
    int ok = 0;
    slots_lock();
    if (g_slots_active < max) { g_slots_active++; ok = 1; }
    slots_unlock();
    return ok;
}

static void slots_release(void) {
    slots_lock();
    if (g_slots_active > 0) g_slots_active--;
    slots_unlock();
}

/* Fase 11: slots de inferencia LLM (contador proprio, mesmo padrao). */
static int g_llm_active = 0;

static int llm_try_acquire(int max) {
    int ok = 0;
    slots_lock();
    if (g_llm_active < max) { g_llm_active++; ok = 1; }
    slots_unlock();
    return ok;
}

static void llm_release(void) {
    slots_lock();
    if (g_llm_active > 0) g_llm_active--;
    slots_unlock();
}

/* Fase 11: monta cfg hibrida quando ha backend LLM + slot livre.
   Retorna 1 com *usou_slot = 1 se o chamador deve liberar o slot. */
static int llm_begin(const ReqCtx *ctx, DecisionConfig *out, int *usou_slot) {
    *usou_slot = 0;
    *out = ctx->base;
    if (ctx->backend != DECISION_BACKEND_LAYA_HTTP) return 0;
    int max = (ctx->laya_max > 0) ? ctx->laya_max : 2;
    if (!llm_try_acquire(max)) return 0;
    *usou_slot = 1;
    out->backend = DECISION_BACKEND_LAYA_HTTP;
    if (ctx->laya_url[0])
        snprintf(out->laya_url, sizeof out->laya_url, "%s", ctx->laya_url);
    out->laya_timeout_ms = (ctx->laya_timeout_ms > 0) ? ctx->laya_timeout_ms : 60000;
    return 1;
}

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

static void send_json(sock_t fd, int code, const char *status, const char *json, const char *cors) {
    if (!cors || !cors[0]) cors = "*";
    char head[640];
    int hl = snprintf(head, sizeof head,
        "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\n"
        "Content-Length: %llu\r\nConnection: close\r\n"
        "Access-Control-Allow-Origin: %s\r\n\r\n",
        code, status, (unsigned long long)strlen(json), cors);
    send_all(fd, head, (size_t)hl);
    send_all(fd, json, strlen(json));
}

/* 503 corpo pronto (sem alocacao, usado quando slots esgotam). */
static void send_busy(sock_t fd, const char *cors) {
    if (!cors || !cors[0]) cors = "*";
    static const char body[] = "{\"error\":\"servidor ocupado, tente novamente\"}";
    char head[640];
    int hl = snprintf(head, sizeof head,
        "HTTP/1.1 503 Service Unavailable\r\nContent-Type: application/json\r\n"
        "Content-Length: %llu\r\nConnection: close\r\nRetry-After: 2\r\n"
        "Access-Control-Allow-Origin: %s\r\n\r\n",
        (unsigned long long)(sizeof(body) - 1), cors);
    send_all(fd, head, (size_t)hl);
    send_all(fd, body, sizeof(body) - 1);
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

static void send_sse_chat(sock_t fd, const char *answer, float conf, int pg, const char *cors, const char *backend) {
    if (!cors || !cors[0]) cors = "*";
    char head[640];
    snprintf(head, sizeof head,
        "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\nConnection: close\r\n"
        "Access-Control-Allow-Origin: %s\r\n\r\n", cors);
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
    char tail[576];
    snprintf(tail, sizeof tail,
        "data: {\"id\":\"chatcmpl-amanda\",\"object\":\"chat.completion.chunk\",\"model\":\"amanda\","
        "\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"stop\"}],"
        "\"amanda\":{\"confianca\":%.3f,\"pagina\":%d,\"backend\":\"%s\"}}\n\ndata: [DONE]\n\n",
        conf, pg, (backend && backend[0]) ? backend : "local");
    send_all(fd, tail, strlen(tail));
}

static void base_decision_cfg(const ServerConfig *sc, DecisionConfig *dc) {
    memset(dc, 0, sizeof *dc);
    dc->limiar_confianca = 0.7f;
    dc->limiar_recusa = (sc && sc->tem_limiar) ? sc->limiar_recusa : 0.3f;
    dc->top_k = 3;
    if (sc) {
        dc->conf_center = sc->conf_center;
        dc->conf_slope = sc->conf_slope;
    }
}

/* numero JSON: "key" : 123 | 0.5 (default quando ausente/invalido) */
static double json_find_num(const char *body, const char *key, double def) {
    char pat[128];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(body, pat);
    if (!p) return def;
    p = strchr(p + strlen(pat), ':');
    if (!p) return def;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    char *end = NULL;
    double v = strtod(p, &end);
    if (end == p) return def;
    return v;
}

/* Fase 7.5: Bearer. Sem chave configurada = aberto. Procura o header
   Authorization no inicio de linha (case-insensitive no nome). */
static int check_auth(const char *hdr, size_t hdr_len, const char *api_key) {
    if (!api_key || !api_key[0]) return 1;
    size_t kl = strlen(api_key);
    const char *p = hdr;
    const char *end = hdr + hdr_len;
    while (p < end) {
        const char *eol = strstr(p, "\r\n");
        if (!eol || eol > end) eol = end;
        /* pula espacos iniciais da linha */
        const char *line = p;
        while (line < eol && (*line == ' ' || *line == '\t')) line++;
        if ((size_t)(eol - line) > 14 &&
            (line[0] == 'A' || line[0] == 'a') &&
            strncmp(line + 1, "uthorization:", 13) == 0) {
            const char *v = line + 15;
            while (v < eol && (*v == ' ' || *v == '\t')) v++;
            if ((size_t)(eol - v) > 7 && strncmp(v, "Bearer ", 7) == 0) {
                v += 7;
                const char *ve = v;
                while (ve < eol && *ve != ' ' && *ve != '\t') ve++;
                if ((size_t)(ve - v) == kl && memcmp(v, api_key, kl) == 0) return 1;
            }
            return 0;
        }
        if (eol >= end) break;
        p = eol + 2;
    }
    return 0;
}

/* Limites Fase 7.5 */
#define SRV_HDR_MAX 65536
#define SRV_RECV_TIMEOUT_MS 30000

static int handle_conn(sock_t fd, const ReqCtx *ctx) {
    AmandaPackage *pkg = ctx->pkg;
    const DecisionConfig *base = &ctx->base;
    const char *cors = (ctx->cors && ctx->cors[0]) ? ctx->cors : "*";
    long max_body = (ctx->max_body > 0) ? ctx->max_body : 1048576L;

    char buf[65536];
    int got = 0, hdr_toolarge = 0;
    /* le cabecalho (teto SRV_HDR_MAX -> 431) */
    while (got < (int)sizeof(buf) - 1) {
        int r = recv(fd, buf + got, (int)sizeof(buf) - 1 - got, 0);
        if (r <= 0) break;
        got += r;
        buf[got] = '\0';
        if (strstr(buf, "\r\n\r\n")) break;
        if (got >= (int)sizeof(buf) - 1) hdr_toolarge = 1;
    }
    if (got <= 0) return 0;
    buf[got] = '\0';
    if (hdr_toolarge && !strstr(buf, "\r\n\r\n")) {
        send_json(fd, 431, "Request Header Fields Too Large",
                  "{\"error\":\"cabecalho excede 64KB\"}", cors);
        return 0;
    }

    char method[16] = {0}, path[512] = {0};
    sscanf(buf, "%15s %511s", method, path);

    int content_len = 0;
    char *cl = strstr(buf, "Content-Length:");
    if (!cl) cl = strstr(buf, "content-length:");
    if (cl) content_len = atoi(cl + 15);
    if (content_len < 0) content_len = 0;

    char *hdr_end = strstr(buf, "\r\n\r\n");
    int hdr_len = hdr_end ? (int)(hdr_end + 4 - buf) : got;
    size_t hdr_used = hdr_end ? (size_t)hdr_len : (size_t)got;

    /* CORS preflight (antes do auth: browser precisa do 204 livre) */
    if (strcmp(method, "OPTIONS") == 0) {
        char pre[640];
        snprintf(pre, sizeof pre,
            "HTTP/1.1 204 No Content\r\nAccess-Control-Allow-Origin: %s\r\n"
            "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
            "Access-Control-Max-Age: 86400\r\n"
            "Content-Length: 0\r\nConnection: close\r\n\r\n", cors);
        send_all(fd, pre, strlen(pre));
        return 0;
    }

    /* Fase 7.5: auth Bearer nas rotas v1 (info e models incluidos) */
    if (strncmp(path, "/v1/", 4) == 0 &&
        !check_auth(buf, hdr_used, ctx->api_key)) {
        send_json(fd, 401, "Unauthorized",
                  "{\"error\":\"autenticacao ausente ou invalida (use Authorization: Bearer <api-key>)\"}",
                  cors);
        return 0;
    }

    /* Fase 7.5: teto do corpo -> 413 (sem ler o excedente) */
    if ((long)content_len > max_body) {
        send_json(fd, 413, "Content Too Large",
                  "{\"error\":\"corpo excede o limite do servidor\"}", cors);
        return 0;
    }

    int body_have = got - hdr_len;
    ByteBuf body; buf_init(&body);
    if (hdr_end && body_have > 0) buf_append(&body, hdr_end + 4, (size_t)body_have);
    int body_toolarge = 0;
    while ((int)body.len < content_len) {
        int r = recv(fd, buf, sizeof(buf), 0);
        if (r <= 0) break;
        if (body.len + (size_t)r > (size_t)max_body) { body_toolarge = 1; break; }
        buf_append(&body, buf, (size_t)r);
    }
    if (body_toolarge) {
        buf_free(&body);
        send_json(fd, 413, "Content Too Large",
                  "{\"error\":\"corpo excede o limite do servidor\"}", cors);
        return 0;
    }
    buf_reserve(&body, 1);
    body.data[body.len] = '\0';
    const char *bstr = (const char *)body.data;

    if (strcmp(method, "GET") == 0 && (strcmp(path, "/v1/models") == 0 || strcmp(path, "/v1/models/") == 0)) {
        char js[512];
        snprintf(js, sizeof js,
            "{\"object\":\"list\",\"data\":[{\"id\":\"amanda\",\"object\":\"model\",\"owned_by\":\"amanda\",\"permission\":[]}]}");
        send_json(fd, 200, "OK", js, cors);
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
        send_json(fd, 200, "OK", js, cors);
        free(js);
    } else if (strcmp(method, "POST") == 0 && strncmp(path, "/v1/chat/completions", 22) == 0) {
        char *prompt = extract_prompt(bstr);
        if (!prompt || !prompt[0]) {
            free(prompt);
            send_json(fd, 400, "Bad Request", "{\"error\":\"campo messages[].content ausente\"}", cors);
        } else {
            /* Fase 11: hibrida quando backend LLM + slot; senao local. */
            DecisionConfig cfg;
            int slot = 0;
            llm_begin(ctx, &cfg, &slot);
            int via_laya = 0;
            Decisao *dd = executar_decisao_hibrida(prompt, pkg->chunks, pkg->num_chunks,
                                                   pkg->embeddings, &cfg, &via_laya);
            if (slot) llm_release();
            const char *bname = via_laya ? "laya-http" : "local";
            float conf = dd->confianca; int pg = dd->pagina;
            char *ans = xstrdup(dd->resposta ? dd->resposta : "");
            liberar_decisao(dd);
            int stream = json_find_bool(bstr, "stream");
            if (stream) {
                send_sse_chat(fd, ans, conf, pg, cors, bname);
            } else {
                char *esc = json_escape(ans);
                char *js = (char *)xmalloc(strlen(esc) + 1024);
                snprintf(js, strlen(esc) + 1024,
                    "{\"id\":\"chatcmpl-amanda\",\"object\":\"chat.completion\",\"model\":\"amanda\","
                    "\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\",\"content\":\"%s\"},\"finish_reason\":\"stop\"}],"
                    "\"amanda\":{\"confianca\":%.3f,\"pagina\":%d,\"backend\":\"%s\"}}",
                    esc, conf, pg, bname);
                free(esc);
                send_json(fd, 200, "OK", js, cors);
                free(js);
            }
            free(ans);
            free(prompt);
        }
    } else if (strcmp(method, "POST") == 0 && strncmp(path, "/v1/decisions", 14) == 0) {
        char *prompt = extract_prompt(bstr);
        if (!prompt || !prompt[0]) {
            free(prompt);
            send_json(fd, 400, "Bad Request", "{\"error\":\"campo pergunta ausente\"}", cors);
        } else {
            DecisionConfig cfg;
            int slot = 0;
            llm_begin(ctx, &cfg, &slot);
            int via_laya = 0;
            Decisao *d = executar_decisao_hibrida(prompt, pkg->chunks, pkg->num_chunks,
                                                  pkg->embeddings, &cfg, &via_laya);
            if (slot) llm_release();
            char *esc = json_escape(d->resposta);
            char *escc = json_escape(d->citacao ? d->citacao : "");
            char *js = (char *)xmalloc(strlen(esc) + strlen(escc) + 512);
            snprintf(js, strlen(esc) + strlen(escc) + 512,
                "{\"resposta\":\"%s\",\"probabilidade\":%.4f,\"confianca\":%.4f,\"pagina\":%d,\"citacao\":\"%s\",\"recusada\":%s,\"backend\":\"%s\"}",
                esc, d->probabilidade, d->confianca, d->pagina, escc,
                d->recusada ? "true" : "false",
                via_laya ? "laya-http" : "local");
            free(esc); free(escc);
            liberar_decisao(d);
            free(prompt);
            send_json(fd, 200, "OK", js, cors);
            free(js);
        }
    } else if (strcmp(method, "POST") == 0 && strncmp(path, "/v1/embeddings", 14) == 0) {
        int ni = 0;
        char **inputs = parse_inputs(bstr, &ni);
        if (!inputs || ni == 0) {
            send_json(fd, 400, "Bad Request", "{\"error\":\"campo input ausente (string ou array de strings)\"}", cors);
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
            send_json(fd, 200, "OK", (char *)js.data, cors);
            buf_free(&js);
            for (int i = 0; i < ni; i++) free(inputs[i]);
            free(inputs);
        }
    } else if (strcmp(method, "POST") == 0 && strncmp(path, "/v1/eval", 8) == 0 &&
               (path[8] == '\0' || path[8] == '/' || path[8] == '?')) {
        /* Fase 7.5: eval sob o pacote servido (backend sempre local).
           Teto de amostradas (eval_max) evita DoS em pacotes gigantes. */
        double sample = json_find_num(bstr, "sample", 0.1);
        long seed = (long)json_find_num(bstr, "seed", 42.0);
        int top_k = (int)json_find_num(bstr, "top_k", 3.0);
        if (!(sample > 0.0)) sample = 0.1;
        if (sample > 1.0) sample = 1.0;
        if (seed < 0) seed = 0;
        if (top_k <= 0) top_k = 3;
        int total = pkg ? pkg->num_perguntas : 0;
        int k = (int)(total * sample + 0.5);
        if (k < 1) k = 1;
        if (k > total) k = total;
        int cap = (ctx->eval_max > 0) ? ctx->eval_max : 200;
        if (k > cap) {
            char js[256];
            snprintf(js, sizeof js,
                "{\"error\":\"amostra %d excede o teto do servidor (%d); use sample menor\"}",
                k, cap);
            send_json(fd, 400, "Bad Request", js, cors);
        } else {
            EvalConfig ec;
            memset(&ec, 0, sizeof ec);
            ec.sample = sample; ec.seed = (unsigned int)seed; ec.top_k = top_k;
            ec.backend = DECISION_BACKEND_LOCAL;
            ec.conf_center = base->conf_center;
            ec.conf_slope = base->conf_slope;
            ec.limiar_recusa = base->limiar_recusa;
            ec.tem_limiar = 1;
            EvalReport rep;
            char *erro = NULL;
            if (!pkg || eval_run(pkg, &ec, &rep, &erro) != 0) {
                char js[256];
                snprintf(js, sizeof js, "{\"error\":\"eval falhou: %s\"}",
                         erro ? erro : "?");
                free(erro);
                send_json(fd, 500, "Internal Server Error", js, cors);
            } else {
                char *j = eval_to_json(&rep, "served-package");
                send_json(fd, 200, "OK", j, cors);
                free(j);
            }
        }
    } else {
        send_json(fd, 404, "Not Found", "{\"error\":\"rota nao encontrada\"}", cors);
    }
    buf_free(&body);
    return 0;
}

typedef struct {
    sock_t fd;
    ReqCtx ctx;
} ConnArg;

static void conn_set_timeout(sock_t fd) {
#ifdef _WIN32
    DWORD ms = (DWORD)SRV_RECV_TIMEOUT_MS;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&ms, sizeof ms);
#else
    struct timeval tv;
    tv.tv_sec = SRV_RECV_TIMEOUT_MS / 1000;
    tv.tv_usec = (SRV_RECV_TIMEOUT_MS % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
#endif
}

#ifdef _WIN32
static unsigned __stdcall conn_thread(void *p) {
    ConnArg *a = (ConnArg *)p;
    handle_conn(a->fd, &a->ctx);
    sock_close(a->fd);
    free(a);
    slots_release();
    return 0;
}
#else
static void *conn_thread(void *p) {
    ConnArg *a = (ConnArg *)p;
    handle_conn(a->fd, &a->ctx);
    sock_close(a->fd);
    free(a);
    slots_release();
    return NULL;
}
#endif

static void spawn_conn(ConnArg *a) {
#ifdef _WIN32
    uintptr_t h = _beginthreadex(NULL, 0, conn_thread, a, 0, NULL);
    if (h == 0) {
        handle_conn(a->fd, &a->ctx);
        sock_close(a->fd);
        free(a);
        slots_release();
    } else {
        CloseHandle((HANDLE)h);
    }
#else
    pthread_t th;
    if (pthread_create(&th, NULL, conn_thread, a) != 0) {
        handle_conn(a->fd, &a->ctx);
        sock_close(a->fd);
        free(a);
        slots_release();
    } else {
        pthread_detach(th);
    }
#endif
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
    printf("rotas: GET /v1/models | GET /v1/amanda/info | POST /v1/chat/completions (+stream) | POST /v1/decisions | POST /v1/embeddings | POST /v1/eval\n");
    DecisionConfig base;
    base_decision_cfg(cfg, &base);
    printf("calibracao: center=%.3f slope=%.1f limiar_recusa=%.2f%s\n",
           base.conf_slope > 0.0f ? base.conf_center : 0.12f,
           base.conf_slope > 0.0f ? base.conf_slope : 12.0f,
           base.limiar_recusa, " (backend local)");
    const char *cors = (cfg->cors_origin && cfg->cors_origin[0]) ? cfg->cors_origin : "*";
    long max_body = (cfg->max_body > 0) ? cfg->max_body : 1048576L;
    int max_conns = (cfg->max_conns > 0) ? cfg->max_conns : 16;
    int eval_max = (cfg->eval_max > 0) ? cfg->eval_max : 200;
    printf("robustez: threads ate %d conns | cors=%s | auth=%s | max_body=%ld | eval_max=%d | recv_timeout=%dms\n",
           max_conns, cors, (cfg->api_key && cfg->api_key[0]) ? "on (Bearer)" : "off (aberto)",
           max_body, eval_max, SRV_RECV_TIMEOUT_MS);
    fflush(stdout);

    ReqCtx ctx;
    ctx.pkg = cfg->pkg;
    ctx.base = base;
    ctx.cors = cors;
    ctx.api_key = cfg->api_key;
    ctx.max_body = max_body;
    ctx.eval_max = eval_max;
    ctx.backend = (cfg->backend == DECISION_BACKEND_LAYA_HTTP) ? DECISION_BACKEND_LAYA_HTTP : DECISION_BACKEND_LOCAL;
    if (cfg->laya_url[0])
        snprintf(ctx.laya_url, sizeof ctx.laya_url, "%s", cfg->laya_url);
    else
        snprintf(ctx.laya_url, sizeof ctx.laya_url, "%s", LAYA_URL_DEFAULT);
    ctx.laya_timeout_ms = (cfg->laya_timeout_ms > 0) ? cfg->laya_timeout_ms : 60000;
    ctx.laya_max = (cfg->laya_max > 0) ? cfg->laya_max : 2;
    if (ctx.backend == DECISION_BACKEND_LAYA_HTTP)
        printf("llm: backend=laya-http url=%s timeout=%dms slots=%d (fallback local automatico)\n",
               ctx.laya_url, ctx.laya_timeout_ms, ctx.laya_max);
    else
        printf("llm: backend=local (use --backend laya-http para inferencia via Laya)\n");
    fflush(stdout);

    for (;;) {
        if (cfg->stop_flag && *cfg->stop_flag) break;
        struct sockaddr_in cli;
        socklen_t cl = sizeof cli;
        sock_t fd = accept(srv, (struct sockaddr *)&cli, &cl);
        if (fd == SOCK_INVALID) continue;
        conn_set_timeout(fd);
        if (!slots_try_acquire(max_conns)) {
            send_busy(fd, cors);
            sock_close(fd);
            continue;
        }
        ConnArg *a = (ConnArg *)xmalloc(sizeof(*a));
        a->fd = fd;
        a->ctx = ctx;
        spawn_conn(a);
    }
    /* esvazia workers antes de voltar (detach: espera ativa zerar) */
    for (int w = 0; w < 200; w++) {
        int n = 0;
        slots_lock(); n = g_slots_active; slots_unlock();
        if (n <= 0) break;
        sleep_ms(50);
    }
    sock_close(srv);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
