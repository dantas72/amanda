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
} AmandaPackage;

int empacotar_amanda(AmandaPackage *pkg, const char *saida, char **erro);
AmandaPackage *carregar_amanda(const char *caminho, char **erro);
void liberar_package(AmandaPackage *pkg);
int package_stats(const AmandaPackage *pkg, char *buf, size_t bufsz);

#endif
