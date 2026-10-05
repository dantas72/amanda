#ifndef LAYA_BACKEND_H
#define LAYA_BACKEND_H

#define LAYA_URL_DEFAULT "http://127.0.0.1:8420"
#define LAYA_TIMEOUT_DEFAULT_MS 120000

typedef enum {
    LAYA_OK = 0,
    LAYA_UNAVAILABLE = 1,
    LAYA_ERR_ARGS = 2
} LayaStatus;

/* Envia {"message": ...} ao POST /chat do engine Laya.
   Retorna LAYA_OK com *content_out alocado (chamador libera com free),
   ou LAYA_UNAVAILABLE quando o engine nao responde, demora alem de
   timeout_ms, responde erro HTTP ou responde sem modelo ativo
   (string de erro padrao do Laya). Nunca bloqueia alem de timeout_ms. */
LayaStatus laya_chat(const char *base_url, const char *message, int timeout_ms,
                     char **content_out, char **erro);

/* 1 se o engine lista ao menos um modelo em algum provider
   (GET /settings/available-models com "models" nao vazio), 0 caso contrario. */
int laya_providers_ready(const char *base_url, int timeout_ms);

/* Extrai o valor da PRIMEIRA ocorrencia de "content":"..." em um corpo JSON,
   com unescape (\" \\ \n \t \r). Retorna string alocada ou NULL. */
char *laya_extract_content(const char *body);

/* ============ Pool LLM com prioridade (fila propria) ============
 *
 * Substitui o "slot ou fallback imediato": quando todos os slots de
 * inferencia estao ocupados, o request espera numa fila limitada em
 * vez de cair na hora para o motor local. Duas prioridades FIFO:
 * ALTA (POST /v1/decisions, o nucleo tipado) passa na frente de
 * NORMAL (POST /v1/chat/completions). Fila cheia ou espera esgotada
 * = fallback local honesto (campo "backend" informa, como antes).
 * Thread-safe nas duas plataformas; sem dependencias novas. */

#define LLM_PRIO_NORMAL 0
#define LLM_PRIO_ALTA 1
#define LLM_POOL_SLOTS_DEFAULT 2
#define LLM_POOL_FILA_DEFAULT 16
#define LLM_POOL_ESPERA_DEFAULT_MS 5000

typedef struct LlmPool LlmPool;

/* max_slots <= 0 vira 2; max_fila < 0 vira 0 (0 = sem espera:
   comportamento antigo, fallback imediato). NULL em falta de memoria. */
LlmPool *llm_pool_criar(int max_slots, int max_fila);
void llm_pool_liberar(LlmPool *p);

/* 1 = slot obtido (depois chamar llm_pool_devolver); 0 = sem slot
   (fila cheia ou espera esgotada -> usar motor local). espera_ms <= 0
   tenta uma vez sem esperar. prioridade: LLM_PRIO_* (outro valor =
   NORMAL). */
int llm_pool_adquirir(LlmPool *p, int prioridade, int espera_ms);
void llm_pool_devolver(LlmPool *p);

/* Contadores acumulados (para testes/diagnostico). */
void llm_pool_stats(LlmPool *p, long *atendidas_out,
                    long *fb_fila_out, long *fb_tempo_out);

/* POST JSON generico (transporte dos backends nuvem/JEV).
 * url: http://... (socket nativo) ou https://... (via curl do
 * sistema, com TLS real). bearer opcional (Authorization; nunca
 * logado). body: JSON pronto. Retorna 0 com *code_out (HTTP) e
 * *rbody_out (corpo alocado) — mesmo com 4xx/5xx; != 0 em falha
 * de transporte (erro alocado). */
int http_post_json(const char *url, const char *bearer, const char *body,
                   int timeout_ms, int *code_out, char **rbody_out,
                   char **erro);

#endif
