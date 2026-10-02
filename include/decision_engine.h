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
    /* Fase 6: calibracao do sigmoide conf = S((score-center)*slope).
       Padrao (zeros): center=0.12, slope=12.0. Para customizar passe
       slope > 0 (center 0 = 0.12 apenas quando slope tambem e 0). */
    float conf_center;
    float conf_slope;
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

/* ============ Fase 12.4: indice invertido + cache de query ============ */

/* Indice BM25 pre-tokenizado por pacote (construcao O(chunks) 1x,
   queries O(termos da query + postings)). Somente leitura apos
   criar: seguro para queries concorrentes (serve). Criar/liberar
   nao sao concorrentes entre si. */
typedef struct RetrievalIndex RetrievalIndex;

RetrievalIndex *indice_criar(Chunk *chunks, int num_chunks);
void indice_liberar(RetrievalIndex *idx);
RankItem *indice_recuperar(RetrievalIndex *idx, const char *pergunta,
                           Embeddings *emb, int top_k, int *n_out);
Decisao *executar_decisao_idx(const char *pergunta, RetrievalIndex *idx,
                              Chunk *chunks, int num_chunks,
                              Embeddings *emb, const DecisionConfig *cfg);
Decisao *executar_decisao_hibrida_idx(const char *pergunta, RetrievalIndex *idx,
                                      Chunk *chunks, int num_chunks,
                                      Embeddings *emb, const DecisionConfig *cfg,
                                      int *usou_laya_out);

/* Cache de embeddings de query (global, thread-safe, FIFO 32).
   Chave = string exata da query. Retorna Embeddings novo (liberar
   com liberar_embeddings); NULL em falha. */
Embeddings *embed_query_cached(const char *texto);
void query_cache_limpar(void);
void query_cache_stats(long *hits_out, long *misses_out);

/* Preenche zeros da cfg com a calibracao gravada no pacote (v3).
   Precedencia: flag CLI (campo != 0 / tem_limiar) > pacote > padrao
   historico (0.12/12.0/0.30). Sem pacote ou sem calib -> padrao. */
void decisao_usar_calib_pacote(DecisionConfig *cfg, int tem_calib,
                               float cal_center, float cal_slope,
                               float cal_limiar);

#endif
