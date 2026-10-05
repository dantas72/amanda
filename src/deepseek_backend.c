#include "deepseek_backend.h"
#include "laya_backend.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

DsStatus deepseek_redact(const char *base_url, const char *model,
                         const char *key, const char *message,
                         int timeout_ms, char **content_out,
                         char **erro) {
    if (!message || !content_out) return DS_ERR_ARGS;
    if (!base_url || !base_url[0]) base_url = DEEPSEEK_URL_DEFAULT;
    if (!model || !model[0]) model = DEEPSEEK_MODEL_DEFAULT;
    if (!key) key = "";
    if (timeout_ms <= 0) timeout_ms = DEEPSEEK_TIMEOUT_DEFAULT_MS;

    /* base [+ path]: aceita base pura ou URL completa. */
    char url[1024];
    if (strstr(base_url, "/chat/completions") != NULL)
        snprintf(url, sizeof url, "%s", base_url);
    else {
        size_t len = strlen(base_url);
        while (len > 0 && base_url[len - 1] == '/') len--;
        snprintf(url, sizeof url, "%.*s/chat/completions",
                 (int)len, base_url);
    }

    char *esc = json_escape(message);
    char *esm = json_escape(model);
    ByteBuf jb;
    buf_init(&jb);
    buf_append_cstr(&jb, "{\"model\":\"");
    buf_append_cstr(&jb, esm);
    buf_append_cstr(&jb, "\",\"messages\":[{\"role\":\"system\",\"content\":\""
                         "Voce redige respostas em portugues a partir do contexto "
                         "fornecido. Seja direto e fiel ao contexto.\"},"
                         "{\"role\":\"user\",\"content\":\"");
    buf_append_cstr(&jb, esc);
    buf_append_cstr(&jb, "\"}],\"stream\":false,\"temperature\":0}");
    free(esc);
    free(esm);
    buf_reserve(&jb, 1);
    jb.data[jb.len] = '\0';

    int code = 0;
    char *rbody = NULL;
    char *herr = NULL;
    int rc = http_post_json(url, key[0] ? key : NULL, (char *)jb.data,
                            timeout_ms, &code, &rbody, &herr);
    buf_free(&jb);
    if (rc != 0) {
        if (erro) *erro = herr ? herr : xstrdup("deepseek: transporte indisponivel");
        else free(herr);
        return DS_UNAVAILABLE;
    }
    if (code < 200 || code >= 300) {
        if (erro) {
            char tmp[160];
            snprintf(tmp, sizeof tmp,
                     "deepseek: HTTP %d (confira URL, modelo e DEEPSEEK_API_KEY)", code);
            *erro = xstrdup(tmp);
        }
        free(rbody);
        return DS_UNAVAILABLE;
    }
    char *content = laya_extract_content(rbody);
    free(rbody);
    if (!content || !content[0]) {
        free(content);
        if (erro) *erro = xstrdup("deepseek: resposta sem conteudo");
        return DS_UNAVAILABLE;
    }
    *content_out = content;
    return DS_OK;
}
