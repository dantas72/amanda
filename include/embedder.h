#ifndef EMBEDDER_H
#define EMBEDDER_H

#include "chunker.h"

#define EMBEDDER_DIM 384

typedef struct {
    float *vetores;
    int num_vetores;
    int dimensao;
} Embeddings;

Embeddings *gerar_embeddings(Chunk *chunks, int num_chunks);
void liberar_embeddings(Embeddings *e);
float cos_sim(const float *a, const float *b, int dim);
void normalizar_vetor(float *v, int dim);
Embeddings *embed_query(const char *texto);

char **tokenizar(const char *texto, int *n_out);
void liberar_tokens(char **toks, int n);

#endif
