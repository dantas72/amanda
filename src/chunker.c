#include "chunker.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int contar_palavras(const char *texto) {
    int n = 0, in = 0;
    for (const unsigned char *p = (const unsigned char *)texto; *p; p++) {
        if (isspace(*p)) { in = 0; }
        else if (!in) { in = 1; n++; }
    }
    return n;
}

/* tokeniza em palavras preservando texto original de cada palavra */
static char **split_words(const char *texto, int *n_out) {
    int cap = 64, n = 0;
    char **w = (char **)xmalloc(sizeof(char *) * (size_t)cap);
    const char *p = texto;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        const char *s = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        if (n >= cap) { cap *= 2; w = (char **)xrealloc(w, sizeof(char *) * (size_t)cap); }
        w[n++] = xstrndup(s, (size_t)(p - s));
    }
    *n_out = n;
    return w;
}

Chunk *dividir_em_chunks(DocumentoExtraido *doc, int tamanho_max_palavras,
                         int sobreposicao_palavras, int *num_chunks) {
    if (tamanho_max_palavras <= 0) tamanho_max_palavras = 180;
    if (sobreposicao_palavras < 0) sobreposicao_palavras = 0;
    if (sobreposicao_palavras >= tamanho_max_palavras)
        sobreposicao_palavras = tamanho_max_palavras / 4;

    /* junta blocos em um fluxo de palavras com pagina por palavra */
    int total_w = 0, cap_w = 256;
    char **words = (char **)xmalloc(sizeof(char *) * (size_t)cap_w);
    int *wpages = (int *)xmalloc(sizeof(int) * (size_t)cap_w);

    for (int b = 0; b < doc->num_blocos; b++) {
        int nw = 0;
        char **w = split_words(doc->blocos[b].texto, &nw);
        for (int i = 0; i < nw; i++) {
            if (total_w >= cap_w) {
                cap_w *= 2;
                words = (char **)xrealloc(words, sizeof(char *) * (size_t)cap_w);
                wpages = (int *)xrealloc(wpages, sizeof(int) * (size_t)cap_w);
            }
            words[total_w] = w[i];
            wpages[total_w] = doc->blocos[b].pagina;
            total_w++;
        }
        free(w);
    }

    if (total_w == 0) {
        free(words); free(wpages);
        *num_chunks = 0;
        return NULL;
    }

    int cap_c = 16, nc = 0;
    Chunk *chunks = (Chunk *)xmalloc(sizeof(Chunk) * (size_t)cap_c);
    int step = tamanho_max_palavras - sobreposicao_palavras;
    if (step <= 0) step = tamanho_max_palavras;

    for (int start = 0; start < total_w; start += step) {
        int end = start + tamanho_max_palavras;
        if (end > total_w) end = total_w;
        ByteBuf b; buf_init(&b);
        for (int i = start; i < end; i++) {
            if (i > start) buf_append(&b, " ", 1);
            buf_append(&b, words[i], strlen(words[i]));
        }
        buf_reserve(&b, 1);
        b.data[b.len] = '\0';
        if (nc >= cap_c) { cap_c *= 2; chunks = (Chunk *)xrealloc(chunks, sizeof(Chunk) * (size_t)cap_c); }
        chunks[nc].texto = (char *)b.data;
        chunks[nc].pagina_inicio = wpages[start];
        chunks[nc].pagina_fim = wpages[end - 1];
        chunks[nc].num_tokens = end - start;
        uint32_t h = fnv1a_32(b.data, b.len);
        char hx[9];
        hash_hex32(h, hx);
        chunks[nc].hash = xstrdup(hx);
        nc++;
        if (end >= total_w) break;
    }

    for (int i = 0; i < total_w; i++) free(words[i]);
    free(words); free(wpages);
    *num_chunks = nc;
    return chunks;
}

void liberar_chunks(Chunk *chunks, int n) {
    if (!chunks) return;
    for (int i = 0; i < n; i++) {
        free(chunks[i].texto);
        free(chunks[i].hash);
    }
    free(chunks);
}
