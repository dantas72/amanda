#ifndef TYPESAFE_BACKEND_H
#define TYPESAFE_BACKEND_H

#include <stddef.h>

/* Backend JEV/TypeSafe (protocolo System One).
 *
 * Fala com qualquer endpoint System One: a nuvem TypeSafe
 * (https://api.typesafe.ai, com Bearer TYPESAFE_API_KEY) ou um
 * nimble local via Ollama (http://127.0.0.1:11434, sem chave).
 * Sem dependencias novas: http:// pelo socket nativo, https:// via
 * curl do sistema (TLS real). A chave nunca e logada; em http com
 * chave o transporte recusa (chave nunca em claro).
 */

#define TYPESAFE_URL_DEFAULT "https://api.typesafe.ai"
#define TYPESAFE_PATH "/v1/systemone"
#define TYPESAFE_MODEL_DEFAULT "jev-latest"
#define TYPESAFE_TIMEOUT_DEFAULT_MS 120000

typedef enum {
    TS_OK = 0,
    TS_UNAVAILABLE = 1,
    TS_ERR_ARGS = 2
} TsStatus;

/* Monta a URL final: se base_url ja contem "/v1/", usa como esta;
 * senao anexa /v1/systemone (composicao sem // duplo). */
void typesafe_endpoint(const char *base_url, char *out, size_t n);

/* Julgamento noul: pergunta se o contexto (state) sustenta resposta a
 * pergunta. Retorna TS_OK com *noul_out em [0,1], ou TS_UNAVAILABLE
 * (engine fora, timeout, HTTP != 2xx, resposta sem noul). model vazio
 * = jev-latest; key vazia = sem Bearer (só vale em http local). */
TsStatus typesafe_judge(const char *base_url, const char *model,
                        const char *key, const char *state,
                        const char *pergunta, int timeout_ms,
                        double *noul_out, char **erro);

#endif
