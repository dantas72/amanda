#ifndef DECISION_ENGINE_H
#define DECISION_ENGINE_H

#include "chunker.h"
#include "embedder.h"
#include "question_gen.h"

typedef struct {
    char *resposta;
    float probabilidade;
    float confianca;
    int pagina;
    char *citacao;
    int recusada;
} Decisao;

typedef struct {
    int indice_chunk;
    float score;
} RankItem;

typedef struct {
    float limiar_confianca;
    float limiar_recusa;
    int top_k;
} DecisionConfig;

Decisao *executar_decisao(const char *pergunta, Chunk *chunks, int num_chunks,
                           Embeddings *emb, const DecisionConfig *cfg);
RankItem *recuperar_chunks(const char *pergunta, Chunk *chunks, int num_chunks,
                           Embeddings *emb, int top_k, int *n_out);
void liberar_decisao(Decisao *d);
char *montar_resposta_chat(const char *pergunta, Chunk *chunks, int num_chunks,
                           Embeddings *emb, const DecisionConfig *cfg,
                           float *confianca_out, int *pagina_out);

#endif
