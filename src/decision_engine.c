#include "decision_engine.h"
#include "laya_backend.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int cmp_rank(const void *a, const void *b) {
    float fa = ((const RankItem *)a)->score;
    float fb = ((const RankItem *)b)->score;
    if (fa < fb) return 1;
    if (fa > fb) return -1;
    return 0;
}

/* sobreposicao lexica simples */
static float lexical_overlap(const char *q, const char *d) {
    int nq = 0, nd = 0;
    char **tq = tokenizar(q, &nq);
    char **td = tokenizar(d, &nd);
    if (nq == 0 || nd == 0) { liberar_tokens(tq, nq); liberar_tokens(td, nd); return 0; }
    int hit = 0;
    for (int i = 0; i < nq; i++)
        for (int j = 0; j < nd; j++)
            if (strcmp(tq[i], td[j]) == 0) { hit++; break; }
    float v = (float)hit / (float)nq;
    liberar_tokens(tq, nq);
    liberar_tokens(td, nd);
    return v;
}

RankItem *recuperar_chunks(const char *pergunta, Chunk *chunks, int num_chunks,
                           Embeddings *emb, int top_k, int *n_out) {
    if (num_chunks <= 0 || !emb || !emb->vetores) { *n_out = 0; return NULL; }
    if (top_k <= 0) top_k = 3;
    if (top_k > num_chunks) top_k = num_chunks;

    Embeddings *eq = embed_query(pergunta);
    RankItem *all = (RankItem *)xmalloc(sizeof(RankItem) * (size_t)num_chunks);
    for (int i = 0; i < num_chunks; i++) {
        float cos = cos_sim(eq->vetores, emb->vetores + (size_t)i * emb->dimensao, emb->dimensao);
        if (cos < 0) cos = 0;
        float lex = lexical_overlap(pergunta, chunks[i].texto);
        float score = 0.6f * cos + 0.4f * lex;
        all[i].indice_chunk = i;
        all[i].score = score;
    }
    liberar_embeddings(eq);
    qsort(all, (size_t)num_chunks, sizeof(RankItem), cmp_rank);
    RankItem *top = (RankItem *)xmalloc(sizeof(RankItem) * (size_t)top_k);
    memcpy(top, all, sizeof(RankItem) * (size_t)top_k);
    free(all);
    *n_out = top_k;
    return top;
}

static float sigmoid(float x) { return 1.0f / (1.0f + expf(-x)); }

Decisao *executar_decisao(const char *pergunta, Chunk *chunks, int num_chunks,
                           Embeddings *emb, const DecisionConfig *cfg) {
    DecisionConfig c;
    memset(&c, 0, sizeof c);
    c.limiar_confianca = 0.7f; c.limiar_recusa = 0.3f; c.top_k = 3;
    if (cfg) c = *cfg;
    Decisao *d = (Decisao *)xcalloc(1, sizeof(*d));
    int n = 0;
    RankItem *rk = recuperar_chunks(pergunta, chunks, num_chunks, emb, c.top_k, &n);
    if (!rk || n == 0) {
        d->resposta = xstrdup("Nao encontrei informacao suficiente no documento.");
        d->recusada = 1;
        d->probabilidade = 0;
        d->confianca = 0;
        return d;
    }
    float best = rk[0].score;
    /* Fase 6: sigmoide parametrizavel. Zeros = padrao historico
       (centro 0.12, inclinacao 12: separa escopo ~0.15-0.50 de
       fora ~0.0-0.08). */
    float center = c.conf_center;
    float slope = c.conf_slope;
    if (slope <= 0.0f) {
        slope = 12.0f;
        if (center == 0.0f) center = 0.12f;
    }
    float conf = sigmoid((best - center) * slope);
    d->confianca = conf;
    d->probabilidade = conf;
    int idx = rk[0].indice_chunk;
    d->pagina = chunks[idx].pagina_inicio;
    free(rk);

    if (conf < c.limiar_recusa) {
        d->recusada = 1;
        d->resposta = xstrdup("Nao encontrei informacao suficiente no documento para responder com seguranca.");
        d->citacao = xstrdup("");
        return d;
    }
    d->recusada = 0;
    /* citacao: ate 400 chars do chunk */
    size_t cl = strlen(chunks[idx].texto);
    size_t cn = cl > 400 ? 400 : cl;
    d->citacao = xstrndup(chunks[idx].texto, cn);
    /* resposta = citacao guiada */
    ByteBuf b; buf_init(&b);
    buf_append(&b, "Com base no documento (p. ", 26);
    char pg[32];
    snprintf(pg, sizeof pg, "%d", d->pagina);
    buf_append(&b, pg, strlen(pg));
    buf_append(&b, "): ", 3);
    buf_append(&b, d->citacao, strlen(d->citacao));
    buf_reserve(&b, 1); b.data[b.len] = '\0';
    d->resposta = (char *)b.data;
    return d;
}

char *montar_resposta_chat(const char *pergunta, Chunk *chunks, int num_chunks,
                           Embeddings *emb, const DecisionConfig *cfg,
                           float *confianca_out, int *pagina_out) {
    Decisao *d = executar_decisao(pergunta, chunks, num_chunks, emb, cfg);
    if (confianca_out) *confianca_out = d->confianca;
    if (pagina_out) *pagina_out = d->pagina;
    char *r = xstrdup(d->resposta);
    liberar_decisao(d);
    return r;
}

void liberar_decisao(Decisao *d) {
    if (!d) return;
    free(d->resposta);
    free(d->citacao);
    free(d);
}

Decisao *executar_decisao_hibrida(const char *pergunta, Chunk *chunks, int num_chunks,
                                  Embeddings *emb, const DecisionConfig *cfg,
                                  int *usou_laya_out) {
    if (usou_laya_out) *usou_laya_out = 0;
    Decisao *local = executar_decisao(pergunta, chunks, num_chunks, emb, cfg);
    if (!cfg || cfg->backend != DECISION_BACKEND_LAYA_HTTP) return local;
    if (!pergunta || local->recusada) return local;

    const char *url = cfg->laya_url[0] ? cfg->laya_url : LAYA_URL_DEFAULT;
    int timeout = cfg->laya_timeout_ms > 0 ? cfg->laya_timeout_ms : LAYA_TIMEOUT_DEFAULT_MS;

    ByteBuf msg; buf_init(&msg);
    buf_append_cstr(&msg, "Com base SOMENTE no contexto abaixo, responda a pergunta "
                          "de forma direta em portugues. Se o contexto nao contiver "
                          "a resposta, diga exatamente: NAO CONSTA.\n\nContexto:\n");
    if (local->citacao) buf_append_cstr(&msg, local->citacao);
    buf_append_cstr(&msg, "\n\nPergunta: ");
    buf_append_cstr(&msg, pergunta);
    buf_reserve(&msg, 1);
    msg.data[msg.len] = '\0';

    char *conteudo = NULL;
    char *lerr = NULL;
    LayaStatus st = laya_chat(url, (char *)msg.data, timeout, &conteudo, &lerr);
    buf_free(&msg);
    free(lerr);
    if (st != LAYA_OK) {
        free(conteudo);
        return local;
    }
    if (strstr(conteudo, "NAO CONSTA") != NULL) {
        free(conteudo);
        return local;
    }
    free(local->resposta);
    {
        ByteBuf b; buf_init(&b);
        buf_append_cstr(&b, conteudo);
        buf_append_cstr(&b, " (via Laya)");
        buf_reserve(&b, 1);
        b.data[b.len] = '\0';
        local->resposta = (char *)b.data;
    }
    free(conteudo);
    if (usou_laya_out) *usou_laya_out = 1;
    return local;
}
