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

/* Fase 7.3: formatos de enunciado. Vazios = embutidos (comportamento
   historico). Variaveis: choice {{trecho}} {{pagina}}; score {{trecho}}
   {{min}} {{max}} {{pagina}}; noul {{afirmacao}} {{pagina}}. */
typedef struct {
    char choice[2048];
    char score[2048];
    char noul[2048];
    int ok;
} QuestionTemplates;

void templates_padrao(QuestionTemplates *t);
/* Carrega dir/question_{choice,score,noul}.tpl (campo "enunciado" ou o
   arquivo inteiro como formato). Retorna 1 se ao menos um carregou,
   0 com fallback silencioso (compilacao nunca falha por template). */
int carregar_templates(const char *dir, QuestionTemplates *out);

PerguntaTipada *gerar_perguntas(Chunk *chunks, int num_chunks,
                                 const QuestionGenConfig *cfg, int *num_perguntas);
PerguntaTipada *gerar_perguntas_tpl(Chunk *chunks, int num_chunks,
                                    const QuestionGenConfig *cfg,
                                    const QuestionTemplates *tpl,
                                    int *num_perguntas);
void liberar_perguntas(PerguntaTipada *p, int n);
const char *tipo_pergunta_str(TipoPergunta t);

#endif
