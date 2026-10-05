#include "mcp.h"
#include "amanda.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================ JSON minimo ============================ */

static const char *j_skip_ws(const char *p) {
    while (p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    return p;
}

/* Localiza o valor bruto de "key" (primeira ocorrencia exata de
 * "key" seguida de ':'). Retorna ponteiro para o inicio do valor
 * (apos o ':') ou NULL. */
static const char *j_find_val(const char *json, const char *key) {
    if (!json || !key) return NULL;
    size_t kl = strlen(key);
    const char *p = json;
    while ((p = strchr(p, '"')) != NULL) {
        if (strncmp(p + 1, key, kl) == 0 && p[1 + kl] == '"') {
            const char *q = j_skip_ws(p + 1 + kl + 1);
            if (*q == ':') return j_skip_ws(q + 1);
            p = p + 1 + kl + 1;
        } else {
            p++;
        }
    }
    return NULL;
}

/* Decodifica string JSON a partir de *pp (que aponta para '"').
 * Avanca *pp para apos a aspa final. Retorna malloc ou NULL. */
static char *j_decode_str(const char **pp) {
    const char *p = *pp;
    if (!p || *p != '"') return NULL;
    p++;
    ByteBuf b;
    buf_init(&b);
    int ok = 1;
    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            char c = *p;
            if (c == '"') buf_append(&b, "\"", 1);
            else if (c == '\\') buf_append(&b, "\\", 1);
            else if (c == '/') buf_append(&b, "/", 1);
            else if (c == 'n') buf_append(&b, "\n", 1);
            else if (c == 'r') buf_append(&b, "\r", 1);
            else if (c == 't') buf_append(&b, "\t", 1);
            else if (c == 'b') buf_append(&b, "\b", 1);
            else if (c == 'f') buf_append(&b, "\f", 1);
            else if (c == 'u' && p[1] && p[2] && p[3] && p[4]) {
                unsigned int cp = 0;
                int digits = 1;
                for (int i = 1; i <= 4; i++) {
                    char h = p[i];
                    cp <<= 4;
                    if (h >= '0' && h <= '9') cp |= (unsigned int)(h - '0');
                    else if (h >= 'a' && h <= 'f') cp |= (unsigned int)(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') cp |= (unsigned int)(h - 'A' + 10);
                    else { digits = 0; break; }
                }
                if (!digits) { ok = 0; break; }
                /* \uXXXX -> UTF-8 */
                char u8[4];
                int nl = 0;
                if (cp < 0x80) { u8[0] = (char)cp; nl = 1; }
                else if (cp < 0x800) {
                    u8[0] = (char)(0xC0 | (cp >> 6));
                    u8[1] = (char)(0x80 | (cp & 0x3F));
                    nl = 2;
                } else {
                    u8[0] = (char)(0xE0 | (cp >> 12));
                    u8[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    u8[2] = (char)(0x80 | (cp & 0x3F));
                    nl = 3;
                }
                buf_append(&b, u8, (size_t)nl);
                p += 4;
            } else if (c == '\0') { ok = 0; break; }
            else {
                /* Escape desconhecido: preserva o char literal. */
                buf_append(&b, &c, 1);
            }
            if (*p) p++;
        } else {
            buf_append(&b, p, 1);
            p++;
        }
    }
    if (*p != '"' || !ok) { buf_free(&b); return NULL; }
    p++;
    buf_reserve(&b, 1);
    b.data[b.len] = '\0';
    *pp = p;
    return (char *)b.data;
}

/* Extrai string de "key" (malloc) ou NULL se ausente/invalida. */
static char *mcp_get_str(const char *json, const char *key) {
    const char *v = j_find_val(json, key);
    if (!v || *v != '"') return NULL;
    return j_decode_str(&v);
}

/* Captura o token bruto de "id" (numero, string com escapes, true,
 * false, null) para ecoar verbatim. Retorna malloc ou NULL. */
static char *mcp_get_id_raw(const char *json) {
    const char *v = j_find_val(json, "id");
    if (!v) return NULL;
    const char *end = v;
    if (*end == '"') {
        end++;
        while (*end && *end != '"') {
            if (*end == '\\' && end[1]) end += 2;
            else end++;
        }
        if (*end == '"') end++;
    } else {
        while (*end && *end != ',' && *end != '}' && *end != ']' &&
               *end != ' ' && *end != '\t' && *end != '\r' && *end != '\n')
            end++;
    }
    if (end == v) return NULL;
    return xstrndup(v, (size_t)(end - v));
}

static int mcp_get_int(const char *json, const char *key, int def) {
    const char *v = j_find_val(json, key);
    if (!v) return def;
    if (*v == '"') return def;
    return atoi(v);
}

/* ============================ respostas ============================ */

static char *resp_ok(const char *id_raw, const char *result_obj) {
    ByteBuf b;
    buf_init(&b);
    buf_append_cstr(&b, "{\"jsonrpc\":\"2.0\",\"id\":");
    buf_append_cstr(&b, id_raw ? id_raw : "null");
    buf_append_cstr(&b, ",\"result\":");
    buf_append_cstr(&b, result_obj ? result_obj : "{}");
    buf_append_cstr(&b, "}\n");
    buf_reserve(&b, 1);
    b.data[b.len] = '\0';
    return (char *)b.data;
}

static char *resp_err(const char *id_raw, int code, const char *msg) {
    char *em = json_escape(msg ? msg : "?");
    ByteBuf b;
    buf_init(&b);
    char head[256];
    snprintf(head, sizeof head,
             "{\"jsonrpc\":\"2.0\",\"id\":%s,\"error\":{\"code\":%d,\"message\":\"",
             id_raw ? id_raw : "null", code);
    buf_append_cstr(&b, head);
    buf_append_cstr(&b, em);
    buf_append_cstr(&b, "\"}}\n");
    free(em);
    buf_reserve(&b, 1);
    b.data[b.len] = '\0';
    return (char *)b.data;
}

/* Conteudo MCP de texto (com escape). is_err != 0 adiciona isError. */
static char *tool_text(const char *id_raw, const char *text, int is_err) {
    char *et = json_escape(text ? text : "");
    ByteBuf res;
    buf_init(&res);
    buf_append_cstr(&res, "{\"content\":[{\"type\":\"text\",\"text\":\"");
    buf_append_cstr(&res, et);
    buf_append_cstr(&res, "\"}]");
    if (is_err) buf_append_cstr(&res, ",\"isError\":true");
    buf_append_cstr(&res, "}");
    free(et);
    buf_reserve(&res, 1);
    res.data[res.len] = '\0';
    char *out = resp_ok(id_raw, (char *)res.data);
    buf_free(&res);
    return out;
}

/* ============================ contexto ============================ */

static void mcp_name_from_spec(const char *spec, char *name_out, size_t nn,
                               const char **path_out) {
    const char *eq = strchr(spec, '=');
    if (eq && eq[1]) {
        size_t nl = (size_t)(eq - spec);
        if (nl >= nn) nl = nn - 1;
        memcpy(name_out, spec, nl);
        name_out[nl] = '\0';
        *path_out = eq + 1;
        return;
    }
    *path_out = spec;
    const char *b = strrchr(spec, '/');
    const char *b2 = strrchr(spec, '\\');
    if (b2 && (!b || b2 > b)) b = b2;
    b = b ? b + 1 : spec;
    snprintf(name_out, nn, "%s", b);
    char *dot = strrchr(name_out, '.');
    if (dot) *dot = '\0';
    if (!name_out[0]) snprintf(name_out, nn, "amanda");
}

int mcp_ctx_init(McpCtx *ctx, const char **specs, int n,
                 const DecisionConfig *cfg_base, int top_k,
                 char **erro_out) {
    if (erro_out) *erro_out = NULL;
    if (!ctx || !specs || n < 1 || n > MCP_MAX_PKGS) {
        if (erro_out) *erro_out = xstrdup("mcp: 1 a 8 pacotes (--package)");
        return 1;
    }
    memset(ctx, 0, sizeof *ctx);
    for (int i = 0; i < n; i++) {
        const char *path = NULL;
        mcp_name_from_spec(specs[i], ctx->names[i], sizeof ctx->names[i], &path);
        for (int j = 0; j < i; j++) {
            if (strcmp(ctx->names[j], ctx->names[i]) == 0) {
                if (erro_out) {
                    char eb[256];
                    snprintf(eb, sizeof eb,
                             "mcp: nome de pacote duplicado: %s (use nome=caminho)",
                             ctx->names[i]);
                    *erro_out = xstrdup(eb);
                }
                mcp_ctx_free(ctx);
                return 1;
            }
        }
        char *erro = NULL;
        ctx->pkgs[i] = carregar_amanda(path, &erro);
        if (!ctx->pkgs[i]) {
            if (erro_out) {
                char eb[512];
                snprintf(eb, sizeof eb, "mcp: %s (%s)",
                         erro ? erro : "?", path ? path : "?");
                *erro_out = xstrdup(eb);
            }
            free(erro);
            mcp_ctx_free(ctx);
            return 1;
        }
        ctx->rix[i] = indice_criar(ctx->pkgs[i]->chunks,
                                   ctx->pkgs[i]->num_chunks);
    }
    ctx->n_pkgs = n;
    if (cfg_base) ctx->base_cfg = *cfg_base;
    else memset(&ctx->base_cfg, 0, sizeof ctx->base_cfg);
    ctx->top_k = (top_k > 0) ? top_k : 3;
    ctx->usar_calib_pkg = 1;
    return 0;
}

void mcp_ctx_free(McpCtx *ctx) {
    if (!ctx) return;
    for (int i = 0; i < ctx->n_pkgs; i++) {
        if (ctx->rix[i]) indice_liberar(ctx->rix[i]);
        if (ctx->pkgs[i]) liberar_package(ctx->pkgs[i]);
        ctx->rix[i] = NULL;
        ctx->pkgs[i] = NULL;
    }
    ctx->n_pkgs = 0;
}

/* model vazio/"amanda"/ausente = 1o pacote; senao busca por nome. */
static int mcp_pick(const McpCtx *ctx, const char *model) {
    if (!model || !model[0] || strcmp(model, "amanda") == 0) return 0;
    for (int i = 0; i < ctx->n_pkgs; i++)
        if (strcmp(ctx->names[i], model) == 0) return i;
    return -1;
}

/* ============================ ferramentas ============================ */

static char *run_ask(McpCtx *ctx, int pi, const char *pergunta, int top_k) {
    AmandaPackage *pkg = ctx->pkgs[pi];
    DecisionConfig cfg = ctx->base_cfg;
    cfg.top_k = (top_k > 0) ? top_k : ctx->top_k;
    if (ctx->usar_calib_pkg)
        decisao_usar_calib_pacote(&cfg, pkg->tem_calib,
                                  pkg->cal_center, pkg->cal_slope,
                                  pkg->cal_limiar);
    if (cfg.limiar_recusa == 0.0f) cfg.limiar_recusa = 0.3f;
    int via_laya = 0;
    Decisao *d = ctx->rix[pi]
        ? executar_decisao_hibrida_idx(pergunta, ctx->rix[pi],
                                       pkg->chunks, pkg->num_chunks,
                                       pkg->embeddings, &cfg, &via_laya)
        : executar_decisao_hibrida(pergunta, pkg->chunks, pkg->num_chunks,
                                   pkg->embeddings, &cfg, &via_laya);
    ByteBuf t;
    buf_init(&t);
    buf_append_cstr(&t, d->resposta ? d->resposta : "");
    {
        char meta[1024];
        snprintf(meta, sizeof meta,
                 "\n\n[confianca=%.2f probabilidade=%.3f pagina=%d backend=%s%s]%s%s",
                 d->confianca, d->probabilidade, d->pagina,
                 via_laya ? "laya-http" : "local",
                 d->recusada ? " recusada" : "",
                 (d->citacao && d->citacao[0]) ? "\nCitacao: " : "",
                 (d->citacao && d->citacao[0]) ? d->citacao : "");
        buf_append_cstr(&t, meta);
    }
    buf_reserve(&t, 1);
    t.data[t.len] = '\0';
    liberar_decisao(d);
    return (char *)t.data;
}

static char *run_decisions(McpCtx *ctx, int pi, const char *pergunta) {
    AmandaPackage *pkg = ctx->pkgs[pi];
    DecisionConfig cfg = ctx->base_cfg;
    cfg.top_k = ctx->top_k;
    if (ctx->usar_calib_pkg)
        decisao_usar_calib_pacote(&cfg, pkg->tem_calib,
                                  pkg->cal_center, pkg->cal_slope,
                                  pkg->cal_limiar);
    if (cfg.limiar_recusa == 0.0f) cfg.limiar_recusa = 0.3f;
    int via_laya = 0;
    Decisao *d = ctx->rix[pi]
        ? executar_decisao_hibrida_idx(pergunta, ctx->rix[pi],
                                       pkg->chunks, pkg->num_chunks,
                                       pkg->embeddings, &cfg, &via_laya)
        : executar_decisao_hibrida(pergunta, pkg->chunks, pkg->num_chunks,
                                   pkg->embeddings, &cfg, &via_laya);
    char *esc = json_escape(d->resposta ? d->resposta : "");
    char *escc = json_escape(d->citacao ? d->citacao : "");
    size_t need = strlen(esc) + strlen(escc) + 256;
    char *out = (char *)xmalloc(need);
    snprintf(out, need,
             "{\"resposta\":\"%s\",\"probabilidade\":%.4f,\"confianca\":%.4f,"
             "\"pagina\":%d,\"citacao\":\"%s\",\"recusada\":%s,\"backend\":\"%s\"}",
             esc, d->probabilidade, d->confianca, d->pagina, escc,
             d->recusada ? "true" : "false",
             via_laya ? "laya-http" : "local");
    free(esc);
    free(escc);
    liberar_decisao(d);
    return out;
}

static const char *TOOLS_LIST_JSON =
    "{\"tools\":["
    "{\"name\":\"ask\",\"description\":\"Pergunta em linguagem natural sobre o "
    "conteudo do pacote .amanda. Resposta extrativa com pagina e citacao; "
    "fora de escopo e recusada com confianca baixa.\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
    "\"pergunta\":{\"type\":\"string\",\"description\":\"Pergunta do usuario\"},"
    "\"model\":{\"type\":\"string\",\"description\":\"Pacote (nome do --package; "
    "omitido = primeiro)\"},"
    "\"top_k\":{\"type\":\"integer\",\"description\":\"Chunks recuperados (padrao 3)\"}},"
    "\"required\":[\"pergunta\"]}},"
    "{\"name\":\"decisions\",\"description\":\"Motor de decisao cru: mesma "
    "pergunta do ask, retorno JSON completo (resposta, probabilidade, "
    "confianca, pagina, citacao, recusada, backend).\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
    "\"pergunta\":{\"type\":\"string\"},"
    "\"model\":{\"type\":\"string\"}},"
    "\"required\":[\"pergunta\"]}},"
    "{\"name\":\"inspect\",\"description\":\"Estatisticas do pacote .amanda "
    "(chunks, perguntas, formato, calibracao).\","
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{"
    "\"model\":{\"type\":\"string\"}}}},"
    "{\"name\":\"version\",\"description\":\"Versao do amandac e do formato "
    ".amanda.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}"
    "]}";

/* ============================ dispatcher ============================ */

char *mcp_handle_line(McpCtx *ctx, const char *line) {
    if (!line) return NULL;
    const char *p = j_skip_ws(line);
    if (*p == '\0') return NULL; /* linha vazia: sem resposta */

    /* Nao-JSON: erro de parse (id null, sem eco possivel). */
    if (*p != '{') {
        return resp_err(NULL, -32700, "parse error: mensagem nao e um objeto JSON");
    }

    /* Valida braceamento basico (chaves e strings). */
    {
        int depth = 0;
        int in_str = 0;
        const char *q = p;
        while (*q) {
            if (in_str) {
                if (*q == '\\' && q[1]) q += 2;
                else if (*q == '"') { in_str = 0; q++; }
                else q++;
            } else {
                if (*q == '"') { in_str = 1; q++; }
                else if (*q == '{') { depth++; q++; }
                else if (*q == '}') { depth--; q++; if (depth < 0) break; }
                else q++;
            }
        }
        if (in_str || depth != 0) {
            return resp_err(NULL, -32700, "parse error: JSON truncado ou invalido");
        }
    }

    char *method = mcp_get_str(p, "method");
    char *id_raw = mcp_get_id_raw(p);
    if (!method) {
        /* Sem metodo e sem id: notificacao malformada -> silencio. */
        free(id_raw);
        return NULL;
    }

    /* Notificacoes: nunca respondem. */
    if (strncmp(method, "notifications/", 14) == 0) {
        free(method);
        free(id_raw);
        return NULL;
    }

    char *out = NULL;
    if (strcmp(method, "initialize") == 0) {
        ByteBuf r;
        buf_init(&r);
        char head[512];
        snprintf(head, sizeof head,
                 "{\"protocolVersion\":\"%s\",\"capabilities\":{\"tools\":{}},"
                 "\"serverInfo\":{\"name\":\"%s\",\"version\":\"%s\"}}",
                 MCP_PROTOCOL_VERSION, MCP_SERVER_NAME, amanda_version());
        buf_append_cstr(&r, head);
        buf_reserve(&r, 1);
        r.data[r.len] = '\0';
        out = resp_ok(id_raw, (char *)r.data);
        buf_free(&r);
    } else if (strcmp(method, "ping") == 0) {
        out = resp_ok(id_raw, "{}");
    } else if (strcmp(method, "tools/list") == 0) {
        out = resp_ok(id_raw, TOOLS_LIST_JSON);
    } else if (strcmp(method, "tools/call") == 0) {
        /* Nome da ferramenta: primeiro "name" (so existe em params). */
        char *tool = mcp_get_str(p, "name");
        char *model = mcp_get_str(p, "model");
        if (!tool || !tool[0]) {
            out = resp_err(id_raw, -32602, "tools/call: campo params.name ausente");
        } else if (strcmp(tool, "version") == 0) {
            char vt[256];
            snprintf(vt, sizeof vt, "amandac %s (formato .amanda v%u)",
                     amanda_version(), AMANDA_FORMAT_VERSION);
            out = tool_text(id_raw, vt, 0);
        } else if (strcmp(tool, "inspect") == 0) {
            int pi = mcp_pick(ctx, model);
            if (pi < 0) {
                ByteBuf e;
                buf_init(&e);
                buf_append_cstr(&e, "inspect: pacote desconhecido. Disponiveis: ");
                for (int i = 0; i < ctx->n_pkgs; i++) {
                    if (i) buf_append_cstr(&e, ", ");
                    buf_append_cstr(&e, ctx->names[i]);
                }
                buf_reserve(&e, 1);
                e.data[e.len] = '\0';
                out = tool_text(id_raw, (char *)e.data, 1);
                buf_free(&e);
            } else {
                char *j = package_stats_json(ctx->pkgs[pi]);
                out = tool_text(id_raw, j ? j : "{}", 0);
                free(j);
            }
        } else if (strcmp(tool, "ask") == 0 || strcmp(tool, "decisions") == 0) {
            char *pergunta = mcp_get_str(p, "pergunta");
            if (!pergunta) pergunta = mcp_get_str(p, "prompt");
            if (!pergunta) pergunta = mcp_get_str(p, "input");
            if (!pergunta) pergunta = mcp_get_str(p, "question");
            if (!pergunta || !pergunta[0]) {
                out = resp_err(id_raw, -32602,
                               "tools/call: campo params.arguments.pergunta ausente ou vazio");
            } else {
                int pi = mcp_pick(ctx, model);
                if (pi < 0) {
                    ByteBuf e;
                    buf_init(&e);
                    buf_append_cstr(&e, "pacote desconhecido. Disponiveis: ");
                    for (int i = 0; i < ctx->n_pkgs; i++) {
                        if (i) buf_append_cstr(&e, ", ");
                        buf_append_cstr(&e, ctx->names[i]);
                    }
                    buf_reserve(&e, 1);
                    e.data[e.len] = '\0';
                    out = tool_text(id_raw, (char *)e.data, 1);
                    buf_free(&e);
                } else if (strcmp(tool, "ask") == 0) {
                    int tk = mcp_get_int(p, "top_k", ctx->top_k);
                    char *txt = run_ask(ctx, pi, pergunta, tk);
                    out = tool_text(id_raw, txt, 0);
                    free(txt);
                } else {
                    char *js = run_decisions(ctx, pi, pergunta);
                    out = tool_text(id_raw, js, 0);
                    free(js);
                }
            }
            free(pergunta);
        } else {
            ByteBuf e;
            buf_init(&e);
            buf_append_cstr(&e, "ferramenta desconhecida. Disponiveis: ask, decisions, inspect, version");
            buf_reserve(&e, 1);
            e.data[e.len] = '\0';
            out = resp_err(id_raw, -32602, (char *)e.data);
            buf_free(&e);
        }
        free(tool);
        free(model);
    } else {
        char eb[192];
        snprintf(eb, sizeof eb, "metodo desconhecido: %.64s", method);
        out = resp_err(id_raw, -32601, eb);
    }

    free(method);
    free(id_raw);
    return out;
}

/* Le uma linha (qualquer tamanho, termina em \n). Retorna 1 com linha
 * em *out (malloc, sem \r\n), 0 em EOF sem dados, -1 em erro. */
static int read_line(FILE *f, char **out) {
    ByteBuf b;
    buf_init(&b);
    int c;
    int any = 0;
    while ((c = fgetc(f)) != EOF) {
        any = 1;
        if (c == '\n') break;
        char ch = (char)c;
        buf_append(&b, &ch, 1);
    }
    if (!any && b.len == 0) { buf_free(&b); *out = NULL; return 0; }
    /* remove \r final (CRLF de clientes Windows) */
    while (b.len > 0 && b.data[b.len - 1] == '\r') b.len--;
    buf_reserve(&b, 1);
    b.data[b.len] = '\0';
    *out = (char *)b.data;
    return 1;
}

int mcp_run(McpCtx *ctx) {
    fprintf(stderr, "mcp %s: pronto em stdio (%d pacote(s)).\n",
            amanda_version(), ctx ? ctx->n_pkgs : 0);
    fflush(stderr);
    /* stdout sem buffer: cada resposta sai imediatamente (exigencia
     * do transporte stdio — cliente le por linha). */
    setvbuf(stdout, NULL, _IONBF, 0);
    for (;;) {
        char *line = NULL;
        int r = read_line(stdin, &line);
        if (r == 0) break; /* EOF */
        if (r < 0) { fprintf(stderr, "mcp: erro de leitura em stdin.\n"); break; }

        char *msg = line;
        char *owned_body = NULL;
        /* Framing LSP: Content-Length + cabecalhos + corpo exato. */
        if (strncmp(line, "Content-Length:", 15) == 0) {
            long n = atol(line + 15);
            /* consome cabecalhos ate a linha vazia */
            for (;;) {
                char *h = NULL;
                int hr = read_line(stdin, &h);
                if (hr != 1) { free(h); break; }
                int empty = (h[0] == '\0');
                free(h);
                if (empty) break;
            }
            if (n > 0 && n < 64L * 1024L * 1024L) {
                owned_body = (char *)xmalloc((size_t)n + 1);
                size_t got = fread(owned_body, 1, (size_t)n, stdin);
                owned_body[got] = '\0';
                msg = owned_body;
            } else {
                fprintf(stderr, "mcp: Content-Length invalido (%ld), ignorado.\n", n);
                free(line);
                free(owned_body);
                continue;
            }
        }

        char *resp = mcp_handle_line(ctx, msg);
        if (resp) {
            fputs(resp, stdout);
            fflush(stdout);
            free(resp);
        }
        free(line);
        free(owned_body);
    }
    fprintf(stderr, "mcp: stdin fechado, encerrando.\n");
    return 0;
}
