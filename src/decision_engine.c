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

/* Fase 12.3: chunk com texto util? Sujeira binaria de PDF tem
   poucas letras (controles, $, <, >) mesmo com 4+ tokens. */
static int chunk_eh_texto(const char *texto) {
    if (!texto || !texto[0]) return 0;
    size_t letras = 0, total = 0;
    for (const unsigned char *p = (const unsigned char *)texto; *p; p++) {
        unsigned char c = *p;
        if (c == ' ' || c == '\n' || c == '\t') { total++; continue; }
        if (c < 128) {
            total++;
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) letras++;
        } else {
            /* byte UTF-8 (acentos ja dobrados no tokenizar): conta como letra */
            total++;
            letras++;
        }
    }
    if (total < 20) return 0;
    /* 40%: tabela numerica legitima passa (~45-60%), sujeira
       binaria (p.ex. chunk p.61 INVEST, ~16% letras) nao. */
    return (letras * 100 >= total * 40) ? 1 : 0;
}

/* Fase 12.2: BM25 lexical (k1=1.2, b=0.75) com IDF por pacote.
   Substitui o overlap simples: termo raro pesa mais, stopwords ja
   filtradas em tokenizar(), normalizacao max-norm 0..1 por query
   para fusao estavel com o cosseno (0.6*cos + 0.4*bm25).
   Fase 12.3: tambem informa chunk valido (dl>=4 tokens; sujeira
   binaria raramente tem 4+ termos apos o filtro junk) e hit de
   frase (query normalizada contida no chunk) para o phrase-boost. */
static void bm25_normas(const char *pergunta, Chunk *chunks, int num_chunks,
                         float *norm_out, int *valido_out, int *frase_out) {
    for (int i = 0; i < num_chunks; i++) {
        norm_out[i] = 0.0f;
        if (valido_out) valido_out[i] = 0;
        if (frase_out) frase_out[i] = 0;
    }
    int nq = 0;
    char **tq = tokenizar(pergunta, &nq);
    if (nq == 0) { liberar_tokens(tq, nq); return; }

    char ***dt = (char ***)xmalloc(sizeof(char **) * (size_t)num_chunks);
    int *dl = (int *)xcalloc((size_t)num_chunks, sizeof(int));
    long total = 0;
    for (int i = 0; i < num_chunks; i++) {
        int nd = 0;
        dt[i] = tokenizar(chunks[i].texto ? chunks[i].texto : "", &nd);
        dl[i] = nd;
        total += nd;
    }
    double avgdl = num_chunks > 0 ? (double)total / (double)num_chunks : 0.0;
    if (avgdl < 1.0) avgdl = 1.0;

    double *idf = (double *)xmalloc(sizeof(double) * (size_t)nq);
    for (int q = 0; q < nq; q++) {
        int df = 0;
        for (int i = 0; i < num_chunks; i++) {
            for (int j = 0; j < dl[i]; j++)
                if (strcmp(tq[q], dt[i][j]) == 0) { df++; break; }
        }
        idf[q] = log(1.0 + ((double)num_chunks - (double)df + 0.5) / ((double)df + 0.5));
    }

    const double k1 = 1.2, b = 0.75;
    float *raw = (float *)xcalloc((size_t)num_chunks, sizeof(float));
    for (int i = 0; i < num_chunks; i++) {
        double s = 0.0;
        for (int q = 0; q < nq; q++) {
            int tf = 0;
            for (int j = 0; j < dl[i]; j++)
                if (strcmp(tq[q], dt[i][j]) == 0) tf++;
            if (tf == 0) continue;
            double denom = (double)tf + k1 * (1.0 - b + b * ((double)dl[i] / avgdl));
            s += idf[q] * ((double)tf * (k1 + 1.0) / denom);
        }
        raw[i] = (float)s;
    }
    /* Fase 12.3: norma saturante raw/(raw+8) em vez de max-norm —
       max-norm colapsa quando um outlier (tabela com termo 10x)
       domina o max e achata todo o resto p/ ~0, deixando o cosseno
       (ruidoso p/ chunk curto) decidir sozinho. */
    for (int i = 0; i < num_chunks; i++)
        norm_out[i] = raw[i] / (raw[i] + 8.0f);
    free(raw);
    free(idf);
    /* Fase 12.3: validade (4+ termos e 40%+ letras) + hit de frase.
       Frase = sequencia contigua de tokens (mesma semantica do
       substring normalizado, sem alocar string por chunk: janela
       direta sobre dt — 3811 mallocs/query matavam a latencia). */
    for (int i = 0; i < num_chunks; i++) {
        if (valido_out) valido_out[i] = (dl[i] >= 4 && chunk_eh_texto(chunks[i].texto)) ? 1 : 0;
        if (frase_out && nq > 0 && dl[i] >= nq) {
            for (int s = 0; s + nq <= dl[i]; s++) {
                int ok = 1;
                for (int q = 0; q < nq; q++) {
                    if (strcmp(dt[i][s + q], tq[q]) != 0) { ok = 0; break; }
                }
                if (ok) { frase_out[i] = 1; break; }
            }
        }
    }
    for (int i = 0; i < num_chunks; i++) liberar_tokens(dt[i], dl[i]);
    free(dt);
    free(dl);
    liberar_tokens(tq, nq);
}

RankItem *recuperar_chunks(const char *pergunta, Chunk *chunks, int num_chunks,
                           Embeddings *emb, int top_k, int *n_out) {
    if (num_chunks <= 0 || !emb || !emb->vetores) { *n_out = 0; return NULL; }
    if (top_k <= 0) top_k = 3;
    if (top_k > num_chunks) top_k = num_chunks;

    Embeddings *eq = embed_query(pergunta);
    float *bmn = (float *)xcalloc((size_t)num_chunks, sizeof(float));
    int *val = (int *)xcalloc((size_t)num_chunks, sizeof(int));
    int *fr = (int *)xcalloc((size_t)num_chunks, sizeof(int));
    bm25_normas(pergunta, chunks, num_chunks, bmn, val, fr);
    RankItem *all = (RankItem *)xmalloc(sizeof(RankItem) * (size_t)num_chunks);
    for (int i = 0; i < num_chunks; i++) {
        float cos = cos_sim(eq->vetores, emb->vetores + (size_t)i * emb->dimensao, emb->dimensao);
        if (cos < 0) cos = 0;
        float score = 0.6f * cos + 0.4f * bmn[i];
        /* Fase 12.3: chunk pobre nao ranqueia; frase exata +0.2 (teto 1). */
        if (!val[i]) score = 0.0f;
        else if (fr[i]) {
            score += 0.2f;
            if (score > 1.0f) score = 1.0f;
        }
        all[i].indice_chunk = i;
        all[i].score = score;
    }
    free(fr);
    free(val);
    free(bmn);
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
    /* Fase 12.2: guarda rank para multi-citacao antes de liberar */
    int ncite = (n >= 2 && c.top_k >= 2) ? 2 : 1;
    int idx2 = (ncite == 2) ? rk[1].indice_chunk : -1;
    if (idx2 == idx) ncite = 1;
    int pg2 = (ncite == 2) ? chunks[idx2].pagina_inicio : -1;

    if (conf < c.limiar_recusa) {
        free(rk);
        d->recusada = 1;
        d->resposta = xstrdup("Nao encontrei informacao suficiente no documento para responder com seguranca.");
        d->citacao = xstrdup("");
        return d;
    }
    d->recusada = 0;
    /* citacao multi top-k: "[p.X] <400 chars>[ [p.Y] <400 chars>]" */
    {
        ByteBuf cb; buf_init(&cb);
        char mk[48];
        snprintf(mk, sizeof mk, "[p.%d] ", chunks[idx].pagina_inicio);
        buf_append_cstr(&cb, mk);
        size_t cl = strlen(chunks[idx].texto);
        size_t cn = cl > 400 ? 400 : cl;
        buf_append(&cb, chunks[idx].texto, cn);
        if (ncite == 2) {
            buf_append_cstr(&cb, " [p.");
            char pg[32];
            snprintf(pg, sizeof pg, "%d", pg2);
            buf_append(&cb, pg, strlen(pg));
            buf_append_cstr(&cb, "] ");
            size_t c2 = strlen(chunks[idx2].texto);
            size_t n2 = c2 > 400 ? 400 : c2;
            buf_append(&cb, chunks[idx2].texto, n2);
        }
        buf_reserve(&cb, 1); cb.data[cb.len] = '\0';
        d->citacao = (char *)cb.data;
    }
    /* resposta = citacao guiada */
    ByteBuf b; buf_init(&b);
    buf_append(&b, "Com base no documento (p. ", 26);
    char pg[32];
    snprintf(pg, sizeof pg, "%d", d->pagina);
    buf_append(&b, pg, strlen(pg));
    if (ncite == 2 && pg2 != d->pagina) {
        buf_append_cstr(&b, ", ");
        snprintf(pg, sizeof pg, "%d", pg2);
        buf_append(&b, pg, strlen(pg));
    }
    buf_append(&b, "): ", 3);
    buf_append(&b, d->citacao, strlen(d->citacao));
    buf_reserve(&b, 1); b.data[b.len] = '\0';
    d->resposta = (char *)b.data;
    free(rk);
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
