#include "config.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void config_defaults(AmandaConfig *c) {
    memset(c, 0, sizeof *c);
    c->chunk_words = 180;
    c->overlap = 30;
    c->max_choice = 3;
    c->max_score = 2;
    c->max_noul = 5;
    c->limiar_recusa = 0.3f;
    c->tj_espaco = -100.0f;
    c->tj_salto = 500.0f;
    snprintf(c->author, sizeof c->author, "amandac");
    snprintf(c->lang, sizeof c->lang, "pt-BR");
    snprintf(c->templates_dir, sizeof c->templates_dir, "templates");
}

static void set_str(char *dst, size_t n, const char *v) {
    snprintf(dst, n, "%s", v);
}

static void strip(char *s) {
    /* remove comentario # fora de aspas e espacos nas bordas */
    int inq = 0;
    for (char *p = s; *p; p++) {
        if (*p == '"') inq = !inq;
        if (*p == '#' && !inq) { *p = '\0'; break; }
    }
    char *t = str_trim(s);
    if (t != s) memmove(s, t, strlen(t) + 1);
    size_t n = strlen(s);
    if (n >= 2 && s[0] == '"' && s[n - 1] == '"') {
        memmove(s, s + 1, n - 2);
        s[n - 2] = '\0';
    }
}

int config_ler(const char *path, AmandaConfig *out, char **erro) {
    if (!path || !out) {
        if (erro) *erro = xstrdup("config: caminho ou saida nulos");
        return -1;
    }
    config_defaults(out);
    char *txt = read_file_text(path);
    if (!txt) {
        if (erro) {
            char tmp[1152];
            snprintf(tmp, sizeof tmp, "config: nao foi possivel ler %s", path);
            *erro = xstrdup(tmp);
        }
        return -1;
    }
    char secao[64] = "";
    for (char *lin = txt; lin && *lin; ) {
        char *nl = strchr(lin, '\n');
        if (nl) *nl = '\0';
        char *prox = nl ? nl + 1 : NULL;
        char *cr = strchr(lin, '\r');
        if (cr) *cr = '\0';
        char *t = lin;
        while (*t == ' ' || *t == '\t') t++;
        if (!*t || *t == '#') { lin = prox; continue; }
        char *dois = strchr(t, ':');
        if (!dois) { lin = prox; continue; }
        if (dois[1] == '\0') {
            /* cabecalho de secao ("chunking:") */
            *dois = '\0';
            str_trim(t);
            snprintf(secao, sizeof secao, "%s", t);
            lin = prox;
            continue;
        }
        *dois = '\0';
        char *k = str_trim(t);
        char *v = dois + 1;
        strip(v);
        if (!*v) { lin = prox; continue; }
        if (strcmp(secao, "pdf") == 0 && strcmp(k, "caminho") == 0) {
            set_str(out->input, sizeof out->input, v); out->tem_input = 1;
        } else if (strcmp(secao, "pdf") == 0 && strcmp(k, "idioma") == 0) {
            set_str(out->lang, sizeof out->lang, v);
        } else if (strcmp(secao, "chunking") == 0 && strcmp(k, "tamanho_max") == 0) {
            out->chunk_words = atoi(v); out->tem_chunk = 1;
        } else if (strcmp(secao, "chunking") == 0 && strcmp(k, "sobreposicao") == 0) {
            out->overlap = atoi(v); out->tem_chunk = 1;
        } else if (strcmp(secao, "question_gen") == 0 && strcmp(k, "max_choice") == 0) {
            out->max_choice = atoi(v); out->tem_qg = 1;
        } else if (strcmp(secao, "question_gen") == 0 && strcmp(k, "max_score") == 0) {
            out->max_score = atoi(v); out->tem_qg = 1;
        } else if (strcmp(secao, "question_gen") == 0 && strcmp(k, "max_noul") == 0) {
            out->max_noul = atoi(v); out->tem_qg = 1;
        } else if (strcmp(secao, "decision_engine") == 0 && strcmp(k, "limiar_recusa") == 0) {
            out->limiar_recusa = (float)atof(v); out->tem_limiar = 1;
        } else if (strcmp(secao, "decision_engine") == 0 && strcmp(k, "conf_center") == 0) {
            out->conf_center = (float)atof(v);
        } else if (strcmp(secao, "decision_engine") == 0 && strcmp(k, "conf_slope") == 0) {
            out->conf_slope = (float)atof(v);
        } else if (strcmp(secao, "templates") == 0 && strcmp(k, "dir") == 0) {
            set_str(out->templates_dir, sizeof out->templates_dir, v);
        } else if (strcmp(secao, "extracao") == 0 && strcmp(k, "tj_espaco") == 0) {
            out->tj_espaco = (float)atof(v); out->tem_extracao = 1;
        } else if (strcmp(secao, "extracao") == 0 && strcmp(k, "tj_salto") == 0) {
            out->tj_salto = (float)atof(v); out->tem_extracao = 1;
        } else if (strcmp(secao, "servidor") == 0 && strcmp(k, "porta") == 0) {
            out->srv_port = atoi(v); out->tem_servidor = 1;
        } else if (strcmp(secao, "servidor") == 0 && strcmp(k, "host") == 0) {
            set_str(out->srv_host, sizeof out->srv_host, v); out->tem_servidor = 1;
        } else if (strcmp(secao, "servidor") == 0 && strcmp(k, "pacote") == 0) {
            set_str(out->srv_pacote, sizeof out->srv_pacote, v); out->tem_servidor = 1;
        } else if (strcmp(secao, "servidor") == 0 && strcmp(k, "cors") == 0) {
            set_str(out->srv_cors, sizeof out->srv_cors, v); out->tem_servidor = 1;
        } else if (strcmp(secao, "servidor") == 0 && strcmp(k, "api_key") == 0) {
            set_str(out->srv_api_key, sizeof out->srv_api_key, v); out->tem_servidor = 1;
        } else if (strcmp(secao, "servidor") == 0 && strcmp(k, "api_key_file") == 0) {
            set_str(out->srv_api_key_file, sizeof out->srv_api_key_file, v); out->tem_servidor = 1;
        } else if (strcmp(secao, "servidor") == 0 && strcmp(k, "max_body") == 0) {
            out->srv_max_body = atol(v); out->tem_servidor = 1;
        } else if (strcmp(secao, "servidor") == 0 && strcmp(k, "max_conns") == 0) {
            out->srv_max_conns = atoi(v); out->tem_servidor = 1;
        } else if (strcmp(secao, "servidor") == 0 && strcmp(k, "workers") == 0) {
            out->srv_workers = atoi(v); out->tem_servidor = 1;
        } else if (strcmp(secao, "servidor") == 0 && strcmp(k, "eval_max") == 0) {
            out->srv_eval_max = atoi(v); out->tem_servidor = 1;
        } else if (strcmp(secao, "servidor") == 0 && strcmp(k, "backend") == 0) {
            set_str(out->srv_backend, sizeof out->srv_backend, v); out->tem_servidor = 1;
        } else if (strcmp(secao, "servidor") == 0 && strcmp(k, "laya_url") == 0) {
            set_str(out->srv_laya_url, sizeof out->srv_laya_url, v); out->tem_servidor = 1;
        } else if (strcmp(secao, "servidor") == 0 && strcmp(k, "laya_timeout_ms") == 0) {
            out->srv_laya_timeout_ms = atoi(v); out->tem_servidor = 1;
        } else if (strcmp(secao, "servidor") == 0 && strcmp(k, "laya_max") == 0) {
            out->srv_laya_max = atoi(v); out->tem_servidor = 1;
        } else if (strcmp(secao, "servidor") == 0 && strcmp(k, "laya_queue") == 0) {
            out->srv_laya_queue = atoi(v); out->tem_servidor = 1;
        } else if (strcmp(secao, "servidor") == 0 && strcmp(k, "laya_queue_ms") == 0) {
            out->srv_laya_queue_ms = atoi(v); out->tem_servidor = 1;
        } else if (strcmp(secao, "servidor") == 0 && strcmp(k, "typesafe_url") == 0) {
            set_str(out->srv_typesafe_url, sizeof out->srv_typesafe_url, v); out->tem_servidor = 1;
        } else if (strcmp(secao, "servidor") == 0 && strcmp(k, "typesafe_model") == 0) {
            set_str(out->srv_typesafe_model, sizeof out->srv_typesafe_model, v); out->tem_servidor = 1;
        } else if (strcmp(secao, "servidor") == 0 && strcmp(k, "typesafe_timeout_ms") == 0) {
            out->srv_typesafe_timeout_ms = atoi(v); out->tem_servidor = 1;
        } else if (strcmp(secao, "servidor") == 0 && strcmp(k, "deepseek_url") == 0) {
            set_str(out->srv_deepseek_url, sizeof out->srv_deepseek_url, v); out->tem_servidor = 1;
        } else if (strcmp(secao, "servidor") == 0 && strcmp(k, "deepseek_model") == 0) {
            set_str(out->srv_deepseek_model, sizeof out->srv_deepseek_model, v); out->tem_servidor = 1;
        } else if (strcmp(secao, "servidor") == 0 && strcmp(k, "deepseek_timeout_ms") == 0) {
            out->srv_deepseek_timeout_ms = atoi(v); out->tem_servidor = 1;
        } else if (strcmp(secao, "servidor") == 0 && strcmp(k, "laya_queue") == 0) {
            out->srv_laya_queue = atoi(v); out->tem_servidor = 1;
        } else if (strcmp(secao, "servidor") == 0 && strcmp(k, "laya_queue_ms") == 0) {
            out->srv_laya_queue_ms = atoi(v); out->tem_servidor = 1;
        } else if (strcmp(secao, "") == 0 && strcmp(k, "titulo") == 0) {
            set_str(out->title, sizeof out->title, v);
        } else if (strcmp(secao, "") == 0 && strcmp(k, "autor") == 0) {
            set_str(out->author, sizeof out->author, v);
        }
        /* demais chaves (projeto, versao, modelo, dimensao...): info, ignoradas */
        lin = prox;
    }
    free(txt);
    if (out->chunk_words <= 0) out->chunk_words = 180;
    if (out->overlap < 0) out->overlap = 0;
    if (out->max_choice < 0) out->max_choice = 0;
    if (out->max_score < 0) out->max_score = 0;
    if (out->max_noul < 0) out->max_noul = 0;
    return 0;
}

/* ==================== amanda.json (overlay, sem reset) ==================== */

static const char *jw(const char *p) {
    while (p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    return p;
}

/* Localiza "sec" : { ... } e devolve [ini,fim) do miolo. 1 achou. */
static int j_secao(const char *txt, const char *sec,
                   const char **ini, const char **fim) {
    size_t sl = strlen(sec);
    const char *p = txt;
    while ((p = strchr(p, '"')) != NULL) {
        if (strncmp(p + 1, sec, sl) == 0 && p[1 + sl] == '"') {
            const char *q = jw(p + 1 + sl + 1);
            if (*q != ':') { p++; continue; }
            q = jw(q + 1);
            if (*q != '{') { p++; continue; }
            q++;
            int depth = 1;
            int instr = 0;
            const char *r = q;
            while (*r && depth > 0) {
                if (instr) {
                    if (*r == '\\' && r[1]) r += 2;
                    else if (*r == '"') { instr = 0; r++; }
                    else r++;
                } else {
                    if (*r == '"') { instr = 1; r++; }
                    else if (*r == '{') { depth++; r++; }
                    else if (*r == '}') { depth--; r++; }
                    else r++;
                }
            }
            if (depth != 0) return 0;
            *ini = q;
            *fim = r - 1;
            return 1;
        }
        p++;
    }
    return 0;
}

/* Valor bruto de "key" dentro de [ini,fim): numero/true/false ou
 * string com escapes (devolve sem aspas, com unescape basico).
 * Retorna 1 e preenche out (sempre NUL-terminado). */
static int j_val(const char *ini, const char *fim, const char *key,
                 char *out, size_t n) {
    size_t kl = strlen(key);
    const char *p = ini;
    while (p < fim && (p = strchr(p, '"')) != NULL && p < fim) {
        if (strncmp(p + 1, key, kl) == 0 && p[1 + kl] == '"') {
            const char *q = jw(p + 1 + kl + 1);
            if (q >= fim || *q != ':') { p++; continue; }
            q = jw(q + 1);
            if (q >= fim) return 0;
            if (*q == '"') {
                q++;
                size_t w = 0;
                while (q < fim && *q && *q != '"' && w + 1 < n) {
                    if (*q == '\\' && q + 1 < fim) {
                        q++;
                        if (*q == 'n') out[w++] = '\n';
                        else if (*q == 't') out[w++] = '\t';
                        else if (*q == 'r') out[w++] = '\r';
                        else out[w++] = *q;
                        q++;
                    } else {
                        out[w++] = *q++;
                    }
                }
                out[w] = '\0';
                return 1;
            }
            /* numero/bool: ate , } ou fim */
            size_t w = 0;
            while (q < fim && *q && *q != ',' && *q != '}' &&
                   *q != '\r' && *q != '\n' && w + 1 < n)
                out[w++] = *q++;
            while (w > 0 && (out[w-1] == ' ' || out[w-1] == '\t')) w--;
            out[w] = '\0';
            return (w > 0) ? 1 : 0;
        }
        p++;
    }
    return 0;
}

static void j_str(AmandaConfig *o, const char *ini, const char *fim,
                  const char *k, char *dst, size_t dn, int *tem) {
    char v[1024];
    if (j_val(ini, fim, k, v, sizeof v) && v[0] && dn > 0) {
        size_t m = strlen(v);
        if (m >= dn) m = dn - 1;
        memcpy(dst, v, m);
        dst[m] = '\0';
        if (tem) *tem = 1;
    }
    (void)o;
}

static void j_int(AmandaConfig *o, const char *ini, const char *fim,
                  const char *k, int *dst, int *tem) {
    char v[64];
    (void)o;
    if (j_val(ini, fim, k, v, sizeof v) && v[0]) {
        *dst = atoi(v);
        if (tem) *tem = 1;
    }
}

static void j_flt(AmandaConfig *o, const char *ini, const char *fim,
                  const char *k, float *dst, int *tem) {
    char v[64];
    (void)o;
    if (j_val(ini, fim, k, v, sizeof v) && v[0]) {
        *dst = (float)atof(v);
        if (tem) *tem = 1;
    }
}

int config_ler_json(const char *path, AmandaConfig *out, char **erro) {
    if (!path || !out) {
        if (erro) *erro = xstrdup("config: caminho ou saida nulos");
        return -1;
    }
    char *txt = read_file_text(path);
    if (!txt) {
        if (erro) {
            char tmp[1152];
            snprintf(tmp, sizeof tmp, "config: nao foi possivel ler %s", path);
            *erro = xstrdup(tmp);
        }
        return -1;
    }
    /* chaves de API nunca saem deste arquivo: ignora silenciosamente. */
    const char *ini, *fim;
    if (j_secao(txt, "servidor", &ini, &fim)) {
        j_str(out, ini, fim, "host", out->srv_host, sizeof out->srv_host, &out->tem_servidor);
        j_int(out, ini, fim, "porta", &out->srv_port, &out->tem_servidor);
        j_str(out, ini, fim, "pacote", out->srv_pacote, sizeof out->srv_pacote, &out->tem_servidor);
        j_str(out, ini, fim, "cors", out->srv_cors, sizeof out->srv_cors, &out->tem_servidor);
        j_str(out, ini, fim, "api_key_file", out->srv_api_key_file, sizeof out->srv_api_key_file, &out->tem_servidor);
        j_int(out, ini, fim, "max_conns", &out->srv_max_conns, &out->tem_servidor);
        j_int(out, ini, fim, "workers", &out->srv_workers, &out->tem_servidor);
        j_int(out, ini, fim, "eval_max", &out->srv_eval_max, &out->tem_servidor);
        j_str(out, ini, fim, "backend", out->srv_backend, sizeof out->srv_backend, &out->tem_servidor);
        j_str(out, ini, fim, "laya_url", out->srv_laya_url, sizeof out->srv_laya_url, &out->tem_servidor);
        j_int(out, ini, fim, "laya_timeout_ms", &out->srv_laya_timeout_ms, &out->tem_servidor);
        j_int(out, ini, fim, "laya_max", &out->srv_laya_max, &out->tem_servidor);
        j_int(out, ini, fim, "laya_queue", &out->srv_laya_queue, &out->tem_servidor);
        j_int(out, ini, fim, "laya_queue_ms", &out->srv_laya_queue_ms, &out->tem_servidor);
        j_str(out, ini, fim, "typesafe_url", out->srv_typesafe_url, sizeof out->srv_typesafe_url, &out->tem_servidor);
        j_str(out, ini, fim, "typesafe_model", out->srv_typesafe_model, sizeof out->srv_typesafe_model, &out->tem_servidor);
        j_int(out, ini, fim, "typesafe_timeout_ms", &out->srv_typesafe_timeout_ms, &out->tem_servidor);
        j_str(out, ini, fim, "deepseek_url", out->srv_deepseek_url, sizeof out->srv_deepseek_url, &out->tem_servidor);
        j_str(out, ini, fim, "deepseek_model", out->srv_deepseek_model, sizeof out->srv_deepseek_model, &out->tem_servidor);
        j_int(out, ini, fim, "deepseek_timeout_ms", &out->srv_deepseek_timeout_ms, &out->tem_servidor);
    }
    if (j_secao(txt, "decision_engine", &ini, &fim)) {
        j_str(out, ini, fim, "backend", out->srv_backend, sizeof out->srv_backend, &out->tem_servidor);
        j_flt(out, ini, fim, "limiar_recusa", &out->limiar_recusa, &out->tem_limiar);
        j_flt(out, ini, fim, "conf_center", &out->conf_center, NULL);
        j_flt(out, ini, fim, "conf_slope", &out->conf_slope, NULL);
    }
    if (j_secao(txt, "typesafe", &ini, &fim)) {
        j_str(out, ini, fim, "url", out->srv_typesafe_url, sizeof out->srv_typesafe_url, &out->tem_servidor);
        j_str(out, ini, fim, "model", out->srv_typesafe_model, sizeof out->srv_typesafe_model, &out->tem_servidor);
        j_int(out, ini, fim, "timeout_ms", &out->srv_typesafe_timeout_ms, &out->tem_servidor);
    }
    /* Alias: "jev" e o mesmo backend SystemOne (ver --backend jev). */
    if (j_secao(txt, "jev", &ini, &fim)) {
        j_str(out, ini, fim, "url", out->srv_typesafe_url, sizeof out->srv_typesafe_url, &out->tem_servidor);
        j_str(out, ini, fim, "model", out->srv_typesafe_model, sizeof out->srv_typesafe_model, &out->tem_servidor);
        j_int(out, ini, fim, "timeout_ms", &out->srv_typesafe_timeout_ms, &out->tem_servidor);
    }
    if (j_secao(txt, "deepseek", &ini, &fim)) {
        j_str(out, ini, fim, "url", out->srv_deepseek_url, sizeof out->srv_deepseek_url, &out->tem_servidor);
        j_str(out, ini, fim, "model", out->srv_deepseek_model, sizeof out->srv_deepseek_model, &out->tem_servidor);
        j_int(out, ini, fim, "timeout_ms", &out->srv_deepseek_timeout_ms, &out->tem_servidor);
    }
    if (j_secao(txt, "chunking", &ini, &fim)) {
        j_int(out, ini, fim, "tamanho_max", &out->chunk_words, &out->tem_chunk);
        j_int(out, ini, fim, "sobreposicao", &out->overlap, &out->tem_chunk);
    }
    free(txt);
    if (out->chunk_words <= 0) out->chunk_words = 180;
    if (out->overlap < 0) out->overlap = 0;
    return 0;
}
