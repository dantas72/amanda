#ifndef CHUNKER_H
#define CHUNKER_H

#include "pdf_extractor.h"

typedef struct {
    char *texto;
    int pagina_inicio;
    int pagina_fim;
    char *hash;
    int num_tokens;
} Chunk;

Chunk *dividir_em_chunks(DocumentoExtraido *doc, int tamanho_max_palavras,
                         int sobreposicao_palavras, int *num_chunks);
void liberar_chunks(Chunk *chunks, int n);
int contar_palavras(const char *texto);

#endif
