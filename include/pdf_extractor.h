#ifndef PDF_EXTRACTOR_H
#define PDF_EXTRACTOR_H

typedef struct {
    char *texto;
    int pagina;
    float x, y, largura, altura;
} BlocoTexto;

typedef struct {
    BlocoTexto *blocos;
    int num_blocos;
    int num_paginas;
    char *titulo;
} DocumentoExtraido;

DocumentoExtraido *extrair_documento(const char *caminho, char **erro);
DocumentoExtraido *extrair_pdf(const char *caminho, char **erro);
DocumentoExtraido *extrair_txt(const char *caminho, char **erro);
DocumentoExtraido *extrair_csv(const char *caminho, char **erro);
DocumentoExtraido *extrair_json(const char *caminho, char **erro);
void liberar_documento(DocumentoExtraido *doc);
char *documento_texto_completo(const DocumentoExtraido *doc);

#endif
