#ifndef DEEPSEEK_BACKEND_H
#define DEEPSEEK_BACKEND_H

/* Backend DeepSeek (chat OpenAI-compatible).
 *
 * Redator alternativo sobre o grounding local: POST
 * {base}/chat/completions com Bearer DEEPSEEK_API_KEY (nuvem,
 * https via curl do sistema). Qualquer falha = fallback local.
 * A chave nunca e logada; http com chave e recusado pelo transporte.
 */

#define DEEPSEEK_URL_DEFAULT "https://api.deepseek.com"
#define DEEPSEEK_MODEL_DEFAULT "deepseek-chat"
#define DEEPSEEK_TIMEOUT_DEFAULT_MS 120000

typedef enum {
    DS_OK = 0,
    DS_UNAVAILABLE = 1,
    DS_ERR_ARGS = 2
} DsStatus;

/* Envia message (contexto + pergunta ja montados pelo chamador) e
 * devolve o texto de choices[0].message.content. model vazio =
 * deepseek-chat. key vazia = sem Bearer (só vale em http, p/ stub). */
DsStatus deepseek_redact(const char *base_url, const char *model,
                         const char *key, const char *message,
                         int timeout_ms, char **content_out,
                         char **erro);

#endif
