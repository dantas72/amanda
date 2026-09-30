#ifndef QUESTION_GEN_H
#define QUESTION_GEN_H

#include "chunker.h"

typedef enum { TIPO_CHOICE = 0, TIPO_SCORE = 1, TIPO_NOUL = 2 } TipoPergunta;

typedef struct {
    TipoPergunta tipo;
    char *enunciado;
    char **opcoes;
    int num_opcoes;
    float min, max;
    char *afirmacao;
    int pagina_fonte;
    char *chunk_hash;
} PerguntaTipada;

typedef struct {
    int max_choice;
    int max_score;
    int max_noul;
} QuestionGenConfig;

PerguntaTipada *gerar_perguntas(Chunk *chunks, int num_chunks,
                                const QuestionGenConfig *cfg, int *num_perguntas);
void liberar_perguntas(PerguntaTipada *p, int n);
const char *tipo_pergunta_str(TipoPergunta t);

#endif
