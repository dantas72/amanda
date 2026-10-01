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
    /* Fase 3: backend de inferencia. 0 = local (padrao), 1 = laya-http
       (com fallback automatico para local). */
    int backend;
    char laya_url[256];
    int laya_timeout_ms;
} DecisionConfig;

#define DECISION_BACKEND_LOCAL 0
#define DECISION_BACKEND_LAYA_HTTP 1

Decisao *executar_decisao(const char *pergunta, Chunk *chunks, int num_chunks,
                           Embeddings *emb, const DecisionConfig *cfg);
/* Fase 3: tenta o backend configurado (laya-http) e cai para o motor local
   em qualquer falha. *usou_laya_out (opcional) recebe 1 se a resposta veio
   do Laya, 0 se veio do motor local. Grounding (pagina/citacao/confianca)
   e sempre do indice local. */
Decisao *executar_decisao_hibrida(const char *pergunta, Chunk *chunks, int num_chunks,
                                  Embeddings *emb, const DecisionConfig *cfg,
                                  int *usou_laya_out);
RankItem *recuperar_chunks(const char *pergunta, Chunk *chunks, int num_chunks,
                           Embeddings *emb, int top_k, int *n_out);
void liberar_decisao(Decisao *d);
char *montar_resposta_chat(const char *pergunta, Chunk *chunks, int num_chunks,
                           Embeddings *emb, const DecisionConfig *cfg,
                           float *confianca_out, int *pagina_out);

#endif
