#include "embedder.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

char **tokenizar(const char *texto, int *n_out) {
    int cap = 32, n = 0;
    char **t = (char **)xmalloc(sizeof(char *) * (size_t)cap);
    const unsigned char *p = (const unsigned char *)texto;
    ByteBuf cur; buf_init(&cur);
    while (1) {
        unsigned char c = *p;
        int fim = (c == '\0');
        int is_tok = !fim && (isalnum(c) || c >= 128);
        if (is_tok) {
            char lc = (c < 128) ? (char)tolower(c) : (char)c;
            buf_append(&cur, &lc, 1);
        } else {
            if (cur.len >= 2) {
                buf_reserve(&cur, 1);
                cur.data[cur.len] = '\0';
                if (n >= cap) { cap *= 2; t = (char **)xrealloc(t, sizeof(char *) * (size_t)cap); }
                t[n++] = xstrdup((char *)cur.data);
            }
            cur.len = 0;
            if (fim) break;
        }
        p++;
    }
    buf_free(&cur);
    *n_out = n;
    return t;
}

void liberar_tokens(char **toks, int n) {
    for (int i = 0; i < n; i++) free(toks[i]);
    free(toks);
}

void normalizar_vetor(float *v, int dim) {
    double s = 0;
    for (int i = 0; i < dim; i++) s += (double)v[i] * v[i];
    if (s <= 0) return;
    float inv = (float)(1.0 / sqrt(s));
    for (int i = 0; i < dim; i++) v[i] *= inv;
}

float cos_sim(const float *a, const float *b, int dim) {
    double s = 0;
    for (int i = 0; i < dim; i++) s += (double)a[i] * b[i];
    return (float)s;
}

static void embed_texto(const char *texto, float *vec, int dim) {
    for (int i = 0; i < dim; i++) vec[i] = 0;
    int n = 0;
    char **toks = tokenizar(texto, &n);
    /* unigramas + bigramas com hash */
    for (int i = 0; i < n; i++) {
        uint32_t h = fnv1a_32((unsigned char *)toks[i], strlen(toks[i]));
        vec[h % (uint32_t)dim] += 1.0f;
        if (i + 1 < n) {
            char bg[256];
            snprintf(bg, sizeof bg, "%s %s", toks[i], toks[i+1]);
            uint32_t h2 = fnv1a_32((unsigned char *)bg, strlen(bg));
            vec[h2 % (uint32_t)dim] += 0.5f;
        }
    }
    /* ponderacao log-tf */
    for (int i = 0; i < dim; i++)
        if (vec[i] > 0) vec[i] = 1.0f + logf(vec[i]);
    normalizar_vetor(vec, dim);
    liberar_tokens(toks, n);
}

Embeddings *gerar_embeddings(Chunk *chunks, int num_chunks) {
    Embeddings *e = (Embeddings *)xcalloc(1, sizeof(*e));
    e->num_vetores = num_chunks;
    e->dimensao = EMBEDDER_DIM;
    if (num_chunks == 0) return e;
    e->vetores = (float *)xcalloc((size_t)num_chunks * EMBEDDER_DIM, sizeof(float));
    for (int i = 0; i < num_chunks; i++)
        embed_texto(chunks[i].texto, e->vetores + (size_t)i * EMBEDDER_DIM, EMBEDDER_DIM);
    return e;
}

Embeddings *embed_query(const char *texto) {
    Embeddings *e = (Embeddings *)xcalloc(1, sizeof(*e));
    e->num_vetores = 1;
    e->dimensao = EMBEDDER_DIM;
    e->vetores = (float *)xcalloc(EMBEDDER_DIM, sizeof(float));
    embed_texto(texto, e->vetores, EMBEDDER_DIM);
    return e;
}

void liberar_embeddings(Embeddings *e) {
    if (!e) return;
    free(e->vetores);
    free(e);
}
