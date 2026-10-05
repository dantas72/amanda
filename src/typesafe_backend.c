#include "typesafe_backend.h"
#include "laya_backend.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void typesafe_endpoint(const char *base_url, char *out, size_t n) {
    if (!out || n == 0) return;
    if (!base_url || !base_url[0]) base_url = TYPESAFE_URL_DEFAULT;
    if (strstr(base_url, "/v1/") != NULL) {
        snprintf(out, n, "%s", base_url);
        return;
    }
    /* tira barras finais para nao duplicar */
    size_t len = strlen(base_url);
    while (len > 0 && base_url[len - 1] == '/') len--;
    snprintf(out, n, "%.*s%s", (int)len, base_url, TYPESAFE_PATH);
}

/* Extrai numero de "answers" -> "suporte" -> "noul". Ignora a
 * ocorrencia de "noul" como VALOR de "type" (exige ':' apos a chave).
 * Retorna 1 ok. */
static int extract_noul(const char *body, double *out) {
    if (!body || !out) return 0;
    const char *ans = strstr(body, "\"answers\"");
    if (!ans) ans = body;
    const char *sup = strstr(ans, "\"suporte\"");
    const char *scope = sup ? sup : ans;
    const char *k = scope;
    while ((k = strstr(k, "\"noul\"")) != NULL) {
        const char *v = k + 6;
        while (*v && (*v == ' ' || *v == '\t' || *v == '\r' || *v == '\n')) v++;
        if (*v == ':') {
            v++;
            while (*v && (*v == ' ' || *v == '\t' || *v == '\r' || *v == '\n')) v++;
            if ((*v < '0' || *v > '9') && *v != '.' && *v != '-' && *v != '+') {
                k += 6;
                continue;
            }
            char *end = NULL;
            double val = strtod(v, &end);
            if (end == v) { k += 6; continue; }
            if (val < 0.0) val = 0.0;
            if (val > 1.0) val = 1.0;
            *out = val;
            return 1;
        }
        k += 6;
    }
    return 0;
}

TsStatus typesafe_judge(const char *base_url, const char *model,
                        const char *key, const char *state,
                        const char *pergunta, int timeout_ms,
                        double *noul_out, char **erro) {
    if (!state || !pergunta || !noul_out) return TS_ERR_ARGS;
    if (!model || !model[0]) model = TYPESAFE_MODEL_DEFAULT;
    if (!key) key = "";
    if (timeout_ms <= 0) timeout_ms = TYPESAFE_TIMEOUT_DEFAULT_MS;

    char url[1024];
    typesafe_endpoint(base_url, url, sizeof url);

    char *es = json_escape(state);
    char *em = json_escape(model);
    char *ep = json_escape(pergunta);
    ByteBuf jb;
    buf_init(&jb);
    buf_append_cstr(&jb, "{\"model\":\"");
    buf_append_cstr(&jb, em);
    buf_append_cstr(&jb, "\",\"state\":\"");
    buf_append_cstr(&jb, es);
    buf_append_cstr(&jb, "\",\"questions\":{\"suporte\":{\"type\":\"noul\","
                         "\"instructions\":\"Dado o contexto acima, a pergunta do "
                         "usuario e respondida de forma sustentada pelo contexto? "
                         "Pergunta: ");
    buf_append_cstr(&jb, ep);
    buf_append_cstr(&jb, "\"}}}");
    free(es);
    free(em);
    free(ep);
    buf_reserve(&jb, 1);
    jb.data[jb.len] = '\0';

    int code = 0;
    char *rbody = NULL;
    char *herr = NULL;
    int rc = http_post_json(url, key[0] ? key : NULL, (char *)jb.data,
                            timeout_ms, &code, &rbody, &herr);
    buf_free(&jb);
    if (rc != 0) {
        if (erro) *erro = herr ? herr : xstrdup("typesafe: transporte indisponivel");
        else free(herr);
        return TS_UNAVAILABLE;
    }
    if (code < 200 || code >= 300) {
        if (erro) {
            char tmp[160];
            snprintf(tmp, sizeof tmp,
                     "typesafe: HTTP %d (confira URL, modelo e TYPESAFE_API_KEY)", code);
            *erro = xstrdup(tmp);
        }
        free(rbody);
        return TS_UNAVAILABLE;
    }
    double noul = 0.0;
    if (!extract_noul(rbody, &noul)) {
        if (erro) *erro = xstrdup("typesafe: resposta sem answers.suporte.noul");
        free(rbody);
        return TS_UNAVAILABLE;
    }
    free(rbody);
    *noul_out = noul;
    return TS_OK;
}
