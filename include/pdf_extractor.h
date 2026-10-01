#ifndef PDF_EXTRACTOR_H
#define PDF_EXTRACTOR_H

#include <stddef.h>

typedef struct {
    char *texto;
    int pagina;
    float x, y, largura, altura;
} BlocoTexto;

/* Fase 7.4: estatisticas de extracao (tolerancia/cobertura). */
typedef struct {
    int total_streams;
    int text_streams;
    int failed_inflate;
    int failed_decode;
    int used_fallback;
    size_t bytes_raw;
    size_t chars_extraidos;
} PdfExtractStats;

typedef struct {
    BlocoTexto *blocos;
    int num_blocos;
    int num_paginas;
    char *titulo;
    PdfExtractStats stats;
} DocumentoExtraido;

DocumentoExtraido *extrair_documento(const char *caminho, char **erro);
DocumentoExtraido *extrair_pdf(const char *caminho, char **erro);
DocumentoExtraido *extrair_txt(const char *caminho, char **erro);
DocumentoExtraido *extrair_csv(const char *caminho, char **erro);
DocumentoExtraido *extrair_json(const char *caminho, char **erro);
void liberar_documento(DocumentoExtraido *doc);
char *documento_texto_completo(const DocumentoExtraido *doc);

#endif
