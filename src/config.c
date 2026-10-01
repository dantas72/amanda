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
        } else if (strcmp(secao, "") == 0 && strcmp(k, "titulo") == 0) {
            set_str(out->title, sizeof out->title, v);
        } else if (strcmp(secao, "") == 0 && strcmp(k, "autor") == 0) {
            set_str(out->author, sizeof out->author, v);
        }
        /* demais chaves (projeto, versao, modelo, dimensao, servidor...): info, ignoradas */
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
