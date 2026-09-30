#include "question_gen.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

const char *tipo_pergunta_str(TipoPergunta t) {
    switch (t) {
    case TIPO_CHOICE: return "choice";
    case TIPO_SCORE: return "score";
    case TIPO_NOUL: return "noul";
    default: return "unknown";
    }
}

/* divide texto em sentencas */
static char **split_sentences(const char *texto, int *n_out) {
    int cap = 16, n = 0;
    char **s = (char **)xmalloc(sizeof(char *) * (size_t)cap);
    ByteBuf cur; buf_init(&cur);
    for (const char *p = texto; ; p++) {
        char c = *p;
        int fim = (c == '\0');
        if (!fim) buf_append(&cur, &c, 1);
        int boundary = fim || ((c == '.' || c == '!' || c == '?' || c == ';') &&
                               (p[1] == '\0' || isspace((unsigned char)p[1]) || p[1] == '"'));
        if (boundary) {
            buf_reserve(&cur, 1);
            cur.data[cur.len] = '\0';
            char *t = xstrdup(str_trim((char *)cur.data));
            if (strlen(t) >= 20) {
                if (n >= cap) { cap *= 2; s = (char **)xrealloc(s, sizeof(char *) * (size_t)cap); }
                s[n++] = t;
            } else free(t);
            cur.len = 0;
        }
        if (fim) break;
    }
    buf_free(&cur);
    *n_out = n;
    return s;
}

/* entidade candidata: sequencia de palavras capitalizadas ou termos entre aspas/numeros */
static int is_cap_word(const char *w) {
    if (!w || !w[0]) return 0;
    unsigned char c = (unsigned char)w[0];
    if (c >= 'A' && c <= 'Z') return 1;
    if (c >= 128) return 1; /* acentuada maiuscula aproximada */
    if (c >= '0' && c <= '9') return 1;
    return 0;
}

static char *shorten(const char *s, size_t maxn) {
    if (strlen(s) <= maxn) return xstrdup(s);
    char *p = xstrndup(s, maxn);
    char *sp = strrchr(p, ' ');
    if (sp && (size_t)(sp - p) > maxn / 2) *sp = '\0';
    return p;
}

static void push_pergunta(PerguntaTipada **arr, int *n, int *cap, PerguntaTipada *q) {
    if (*n >= *cap) {
        *cap = *cap ? *cap * 2 : 16;
        *arr = (PerguntaTipada *)xrealloc(*arr, sizeof(PerguntaTipada) * (size_t)*cap);
    }
    (*arr)[(*n)++] = *q;
}

PerguntaTipada *gerar_perguntas(Chunk *chunks, int num_chunks,
                                const QuestionGenConfig *cfg, int *num_perguntas) {
    QuestionGenConfig c = {3, 2, 5};
    if (cfg) c = *cfg;
    PerguntaTipada *out = NULL;
    int n = 0, cap = 0;

    /* coleta entidades globais para distratores */
    int gcap = 64, gn = 0;
    char **glob = (char **)xmalloc(sizeof(char *) * (size_t)gcap);
    for (int i = 0; i < num_chunks; i++) {
        int ns = 0;
        char **sents = split_sentences(chunks[i].texto, &ns);
        for (int k = 0; k < ns; k++) {
            char *copy = xstrdup(sents[k]);
            for (char *p = copy; *p; p++) if (*p == ',' || *p == ':' || *p == '(' || *p == ')') *p = ' ';
            /* extrai sequencias capitalizadas */
            char *tok = strtok(copy, " \t\r\n\"'");
            ByteBuf ent; buf_init(&ent);
            int ent_w = 0;
            while (tok) {
                if (is_cap_word(tok) && strlen(tok) >= 3) {
                    if (ent_w) buf_append(&ent, " ", 1);
                    buf_append(&ent, tok, strlen(tok));
                    ent_w++;
                } else {
                    if (ent_w >= 1 && ent.len >= 3) {
                        buf_reserve(&ent, 1); ent.data[ent.len] = '\0';
                        if (gn >= gcap) { gcap *= 2; glob = (char **)xrealloc(glob, sizeof(char *) * (size_t)gcap); }
                        glob[gn++] = xstrdup((char *)ent.data);
                    }
                    ent.len = 0; ent_w = 0;
                }
                tok = strtok(NULL, " \t\r\n\"'");
            }
            if (ent_w >= 1 && ent.len >= 3) {
                buf_reserve(&ent, 1); ent.data[ent.len] = '\0';
                if (gn >= gcap) { gcap *= 2; glob = (char **)xrealloc(glob, sizeof(char *) * (size_t)gcap); }
                glob[gn++] = xstrdup((char *)ent.data);
            }
            buf_free(&ent);
            free(copy);
        }
        for (int k = 0; k < ns; k++) free(sents[k]);
        free(sents);
    }

    for (int i = 0; i < num_chunks; i++) {
        int ns = 0;
        char **sents = split_sentences(chunks[i].texto, &ns);
        int made_choice = 0, made_score = 0, made_noul = 0;

        /* NOUL: afirmacoes = primeiras sentencas */
        for (int k = 0; k < ns && made_noul < c.max_noul; k++) {
            PerguntaTipada q;
            memset(&q, 0, sizeof q);
            q.tipo = TIPO_NOUL;
            char *curto = shorten(sents[k], 220);
            ByteBuf en; buf_init(&en);
            buf_append(&en, "Segundo o documento, a afirmacao \"", 34);
            buf_append(&en, curto, strlen(curto));
            buf_append(&en, "\" e verdadeira?", 14);
            buf_reserve(&en, 1); en.data[en.len] = '\0';
            q.enunciado = (char *)en.data;
            q.afirmacao = xstrdup(sents[k]);
            q.pagina_fonte = chunks[i].pagina_inicio;
            q.chunk_hash = xstrdup(chunks[i].hash);
            push_pergunta(&out, &n, &cap, &q);
            free(curto);
            made_noul++;
        }

        /* CHOICE: usa sentencas com entidades locais */
        for (int k = 0; k < ns && made_choice < c.max_choice; k++) {
            /* encontra entidade local: primeira palavra capitalizada longa */
            char *copy = xstrdup(sents[k]);
            char *best = NULL;
            char *tok = strtok(copy, " \t\r\n\"'(),.:;");
            while (tok) {
                if (is_cap_word(tok) && strlen(tok) >= 4) { best = tok; break; }
                tok = strtok(NULL, " \t\r\n\"'(),.:;");
            }
            if (!best) { free(copy); continue; }
            char *ent = xstrdup(best);
            free(copy);

            PerguntaTipada q;
            memset(&q, 0, sizeof q);
            q.tipo = TIPO_CHOICE;
            char *ctx = shorten(sents[k], 160);
            ByteBuf en; buf_init(&en);
            buf_append(&en, "Qual e o elemento central citado no trecho: \"", 44);
            buf_append(&en, ctx, strlen(ctx));
            buf_append(&en, "\"?", 2);
            buf_reserve(&en, 1); en.data[en.len] = '\0';
            q.enunciado = (char *)en.data;
            free(ctx);
            q.num_opcoes = 4;
            q.opcoes = (char **)xcalloc(4, sizeof(char *));
            q.opcoes[0] = ent;
            int doi = 1;
            for (int g = 0; g < gn && doi < 4; g++) {
                if (strcmp(glob[g], ent) == 0) continue;
                int dup = 0;
                for (int z = 0; z < doi; z++) if (strcmp(q.opcoes[z], glob[g]) == 0) { dup = 1; break; }
                if (!dup) q.opcoes[doi++] = xstrdup(glob[g]);
            }
            while (doi < 4) {
                char tmp[64];
                snprintf(tmp, sizeof tmp, "Alternativa %d (nao consta no trecho)", doi + 1);
                q.opcoes[doi++] = xstrdup(tmp);
            }
            /* embaralha deterministico por hash */
            uint32_t h = fnv1a_32((unsigned char *)chunks[i].hash, strlen(chunks[i].hash));
            h ^= (uint32_t)k * 2654435761u;
            for (int z = 3; z > 0; z--) {
                int j = (int)(h % (uint32_t)(z + 1));
                char *tt = q.opcoes[z]; q.opcoes[z] = q.opcoes[j]; q.opcoes[j] = tt;
                h = h * 1664525u + 1013904223u;
            }
            q.pagina_fonte = chunks[i].pagina_inicio;
            q.chunk_hash = xstrdup(chunks[i].hash);
            push_pergunta(&out, &n, &cap, &q);
            made_choice++;
        }

        /* SCORE: rubrica sobre o chunk */
        for (int k = 0; k < 1 && made_score < c.max_score; k++) {
            PerguntaTipada q;
            memset(&q, 0, sizeof q);
            q.tipo = TIPO_SCORE;
            char *ctx = shorten(chunks[i].texto, 160);
            ByteBuf en; buf_init(&en);
            buf_append(&en, "Em uma escala de 0 a 10, qual a relevancia do trecho \"", 49);
            buf_append(&en, ctx, strlen(ctx));
            buf_append(&en, "\" para o tema do documento?", 26);
            buf_reserve(&en, 1); en.data[en.len] = '\0';
            q.enunciado = (char *)en.data;
            free(ctx);
            q.min = 0; q.max = 10;
            q.pagina_fonte = chunks[i].pagina_inicio;
            q.chunk_hash = xstrdup(chunks[i].hash);
            push_pergunta(&out, &n, &cap, &q);
            made_score++;
            (void)k;
        }

        for (int k = 0; k < ns; k++) free(sents[k]);
        free(sents);
    }

    for (int i = 0; i < gn; i++) free(glob[i]);
    free(glob);
    *num_perguntas = n;
    return out;
}

void liberar_perguntas(PerguntaTipada *p, int n) {
    if (!p) return;
    for (int i = 0; i < n; i++) {
        free(p[i].enunciado);
        for (int k = 0; k < p[i].num_opcoes; k++) free(p[i].opcoes[k]);
        free(p[i].opcoes);
        free(p[i].afirmacao);
        free(p[i].chunk_hash);
    }
    free(p);
}
