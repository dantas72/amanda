#ifndef SERVER_H
#define SERVER_H

#include "packager.h"

#define SRV_MAX_PKGS 8
#define SRV_WORKERS_DEFAULT 8
#define SRV_WORKERS_MAX 64

typedef struct {
    const char *host;
    int port;
    AmandaPackage *pkg;
    volatile int *stop_flag;
    /* Fase 7.2: calibracao (zeros = padrao do motor). */
    float conf_center;
    float conf_slope;
    float limiar_recusa;
    int tem_limiar;
    /* Fase 7.5: robustez (defaults aplicados quando zero/nulo).
        Backend segue local por request; com threads, varias decisoes
        locais concorrem sem travar o servico (sem estado global). */
    const char *cors_origin;  /* default "*" */
    const char *api_key;      /* NULL/vazio = aberto; senao exige Bearer */
    long max_body;            /* bytes; default 1048576; > responde 413 */
    int max_conns;            /* conexoes simultaneas; default 16; cheio = 503 */
    int eval_max;             /* teto de amostradas no POST /v1/eval; default 200 */
    /* Fase 11: inferencia LLM no serve (default local).
       0 = local; 1 = laya-http com fallback automatico p/ local.
       Slots LLM (--laya-max, default 2) protegem o engine: sem slot
       livre, a resposta sai do motor local (campo backend informa). */
    int backend;
    char laya_url[256];
    int laya_timeout_ms;      /* default 60000 */
    int laya_max;
    /* Pool LLM com prioridade: laya_queue = teto de esperas (-1 = 16,
       0 = sem espera, legado Fase 11); laya_queue_ms = espera maxima
       (-1 = 5000ms, 0 = tenta uma vez). decisions tem prioridade ALTA
       sobre chat (NORMAL); cheia/estouro = fallback local honesto. */
    int laya_queue;
    int laya_queue_ms;
    /* Backends reais (chaves: malloc resolvido no CLI; nunca logar). */
    char typesafe_url[256];
    char typesafe_model[64];
    char *typesafe_key;
    int typesafe_timeout_ms;
    char deepseek_url[256];
    char deepseek_model[64];
    char *deepseek_key;
    int deepseek_timeout_ms;
    /* Fase 13: multi-pacote (roteado por "model", default 1o).
       Quando n_pkgs > 0, pkgs/nomes prevalecem sobre pkg (legado).
       Nomes unicos (ex.: basename sem extensao); "amanda" = alias
       do primeiro. /v1/models lista todos. */
    AmandaPackage **pkgs;
    const char **pkg_names;
    int n_pkgs;
    /* Fase 13: pool de workers (default 8, teto 64). 0 = default.
       max_conns segue o teto total (ativas + fila); cheio = 503. */
    int workers;
} ServerConfig;

int server_run(const ServerConfig *cfg);

/* Fase 13: resolve chave de API (malloc; free com free; NULL = aberto).
   Precedencia: flag CLI > env AMANDA_API_KEY > arquivo (trim).
   Nunca logar o valor retornado. */
char *amanda_resolve_api_key(const char *flag, const char *file);

/* Generico para segredos de backends (TYPESAFE_API_KEY,
   DEEPSEEK_API_KEY...). Precedencia: flag > env > --key-file >
   amandac.conf (KEY=valor, ver abaixo). Mesma higiene; zere com
   memset apos copiar. */
char *amanda_resolve_secret(const char *flag, const char *env, const char *file);

/* Le UMA chave de arquivo KEY=valor (amandac.conf ou --key-file no
 * formato KEY=valor). Ignora # comentarios, vazias e aspas em volta.
 * Retorna malloc ou NULL. Usado pelo resolver e pelos testes. */
char *amanda_conf_ler_chave(const char *path, const char *env);

#endif
