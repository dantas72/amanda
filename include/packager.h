#ifndef PACKAGER_H
#define PACKAGER_H

#include <stddef.h>
#include "chunker.h"
#include "embedder.h"
#include "question_gen.h"

typedef struct {
    char *titulo;
    char *autor;
    char *data;
    char *idioma;
    char *versao_app;
    Chunk *chunks;
    int num_chunks;
    Embeddings *embeddings;
    PerguntaTipada *perguntas;
    int num_perguntas;
    /* Fase 7.4: cobertura da extracao (persistida no formato v2) */
    int num_paginas;
    int extra_blocos;
    int extra_total_streams;
    int extra_text_streams;
    int extra_failed;
    int extra_fallback;
} AmandaPackage;

int empacotar_amanda(AmandaPackage *pkg, const char *saida, char **erro);
AmandaPackage *carregar_amanda(const char *caminho, char **erro);
void liberar_package(AmandaPackage *pkg);
int package_stats(const AmandaPackage *pkg, char *buf, size_t bufsz);

#endif
