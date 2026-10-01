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

#endif
