#include "decision_engine.h"
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
    DecisionConfig c = {0.7f, 0.3f, 3};
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
    /* calibracao: mapeia score hibrido [0,1] para confianca.
       Scores tipicos: pergunta no escopo ~0.15-0.50, fora ~0.0-0.08.
       Centro 0.12 com inclinacao 12 separa bem os dois regimes. */
    float conf = sigmoid((best - 0.12f) * 12.0f);
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
