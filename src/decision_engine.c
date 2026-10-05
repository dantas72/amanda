#include "decision_engine.h"
#include "laya_backend.h"
#include "typesafe_backend.h"
#include "deepseek_backend.h"
#include "embedder.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
static CRITICAL_SECTION g_qc_cs;
static int g_qc_once = 0;
static void qc_lock(void) {
    if (!g_qc_once) { InitializeCriticalSection(&g_qc_cs); g_qc_once = 1; }
    EnterCriticalSection(&g_qc_cs);
}
static void qc_unlock(void) { LeaveCriticalSection(&g_qc_cs); }
#else
#include <pthread.h>
static pthread_mutex_t g_qc_mtx = PTHREAD_MUTEX_INITIALIZER;
static void qc_lock(void) { pthread_mutex_lock(&g_qc_mtx); }
static void qc_unlock(void) { pthread_mutex_unlock(&g_qc_mtx); }
#endif

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

/* ============ Fase 12.4: indice invertido BM25 ============ */

typedef struct {
    char *termo;   /* owned */
    int *docs;     /* chunk ids com o termo */
    int *tfs;      /* tf paralelo a docs */
    int npost;
    int cap;
    int used;      /* slot do hash ocupado */
} TermEntry;

struct RetrievalIndex {
    int num_chunks;
    char ***toks;  /* tokens por chunk (owned) */
    int *doc_len;
    int *valido;
    double avgdl;
    TermEntry *tab;
    int tab_cap;   /* potencia de 2 */
    int tab_n;
};

static uint32_t idx_hash_str(const char *s) {
    return fnv1a_32((const unsigned char *)s, strlen(s));
}

/* encontra slot (existente ou livre p/ inserir). */
static TermEntry *idx_slot(RetrievalIndex *ix, const char *termo, int *achou) {
    uint32_t h = idx_hash_str(termo);
    int mask = ix->tab_cap - 1;
    int pos = (int)(h & (uint32_t)mask);
    for (int t = 0; t < ix->tab_cap; t++) {
        TermEntry *e = &ix->tab[pos];
        if (!e->used) { *achou = 0; return e; }
        if (strcmp(e->termo, termo) == 0) { *achou = 1; return e; }
        pos = (pos + 1) & mask;
    }
    *achou = 0;
    return NULL; /* tabela cheia (nao deve ocorrer: cresce antes) */
}

static void idx_tab_grow(RetrievalIndex *ix) {
    int old_cap = ix->tab_cap;
    TermEntry *old = ix->tab;
    ix->tab_cap *= 2;
    ix->tab = (TermEntry *)xcalloc((size_t)ix->tab_cap, sizeof(TermEntry));
    ix->tab_n = 0;
    int mask = ix->tab_cap - 1;
    for (int i = 0; i < old_cap; i++) {
        if (!old[i].used) continue;
        uint32_t h = idx_hash_str(old[i].termo);
        int pos = (int)(h & (uint32_t)mask);
        while (ix->tab[pos].used) pos = (pos + 1) & mask;
        ix->tab[pos] = old[i]; /* move ponteiros */
        ix->tab_n++;
    }
    free(old);
}

static void idx_posting_add(TermEntry *e, int doc, int tf) {
    if (e->npost >= e->cap) {
        e->cap = e->cap ? e->cap * 2 : 4;
        e->docs = (int *)xrealloc(e->docs, sizeof(int) * (size_t)e->cap);
        e->tfs = (int *)xrealloc(e->tfs, sizeof(int) * (size_t)e->cap);
    }
    e->docs[e->npost] = doc;
    e->tfs[e->npost] = tf;
    e->npost++;
}

RetrievalIndex *indice_criar(Chunk *chunks, int num_chunks) {
    RetrievalIndex *ix = (RetrievalIndex *)xcalloc(1, sizeof(*ix));
    ix->num_chunks = num_chunks;
    if (num_chunks <= 0) return ix;
    ix->toks = (char ***)xcalloc((size_t)num_chunks, sizeof(char **));
    ix->doc_len = (int *)xcalloc((size_t)num_chunks, sizeof(int));
    ix->valido = (int *)xcalloc((size_t)num_chunks, sizeof(int));
    long total = 0;
    for (int i = 0; i < num_chunks; i++) {
        int nd = 0;
        ix->toks[i] = tokenizar(chunks[i].texto ? chunks[i].texto : "", &nd);
        ix->doc_len[i] = nd;
        total += nd;
        ix->valido[i] = (nd >= 4 && chunk_eh_texto(chunks[i].texto)) ? 1 : 0;
    }
    ix->avgdl = num_chunks > 0 ? (double)total / (double)num_chunks : 0.0;
    if (ix->avgdl < 1.0) ix->avgdl = 1.0;
    ix->tab_cap = 4096;
    ix->tab = (TermEntry *)xcalloc((size_t)ix->tab_cap, sizeof(TermEntry));
    /* postings: para cada chunk, conta tf por termo distinto */
    for (int i = 0; i < num_chunks; i++) {
        int nd = ix->doc_len[i];
        for (int j = 0; j < nd; j++) {
            /* pula se termo ja visto neste chunk */
            int visto = 0;
            for (int k = 0; k < j; k++) {
                if (strcmp(ix->toks[i][j], ix->toks[i][k]) == 0) { visto = 1; break; }
            }
            if (visto) continue;
            int tf = 0;
            for (int k = j; k < nd; k++) {
                if (strcmp(ix->toks[i][j], ix->toks[i][k]) == 0) tf++;
            }
            if ((ix->tab_n + 1) * 4 >= ix->tab_cap * 3) idx_tab_grow(ix);
            int achou = 0;
            TermEntry *e = idx_slot(ix, ix->toks[i][j], &achou);
            if (!e) continue;
            if (!achou) {
                e->termo = xstrdup(ix->toks[i][j]);
                e->used = 1;
                ix->tab_n++;
            }
            idx_posting_add(e, i, tf);
        }
    }
    return ix;
}

void indice_liberar(RetrievalIndex *ix) {
    if (!ix) return;
    if (ix->toks) {
        for (int i = 0; i < ix->num_chunks; i++) liberar_tokens(ix->toks[i], ix->doc_len[i]);
        free(ix->toks);
    }
    free(ix->doc_len);
    free(ix->valido);
    if (ix->tab) {
        for (int i = 0; i < ix->tab_cap; i++) {
            if (!ix->tab[i].used) continue;
            free(ix->tab[i].termo);
            free(ix->tab[i].docs);
            free(ix->tab[i].tfs);
        }
        free(ix->tab);
    }
    free(ix);
}

/* pontuacao BM25 + fusao + junk + phrase sobre o indice.
   Mesma matematica do caminho antigo (fases 12.2/12.3). */
static void indice_pontuar(RetrievalIndex *ix, char **tq, int nq,
                           Embeddings *eq, Embeddings *emb,
                           float *scores_out) {
    int n = ix->num_chunks;
    for (int i = 0; i < n; i++) scores_out[i] = 0.0f;
    if (nq == 0) {
        /* sem termos (ex.: so stopwords): score zero, como o legado
           (eq de query vazia eh vetor nulo, mas garante exato). */
        for (int i = 0; i < n; i++) scores_out[i] = 0.0f;
        return;
    }
    const double k1 = 1.2, b = 0.75;
    /* double (como o caminho legado): soma na mesma ordem por
       query-termo, bit-identica ao legado para o mesmo indice. */
    double *raw = (double *)xcalloc((size_t)n, sizeof(double));
    for (int q = 0; q < nq; q++) {
        int achou = 0;
        TermEntry *e = idx_slot(ix, tq[q], &achou);
        int df = (achou && e) ? e->npost : 0;
        double idf = log(1.0 + ((double)n - (double)df + 0.5) / ((double)df + 0.5));
        if (!achou || !e) continue;
        for (int p = 0; p < e->npost; p++) {
            int i = e->docs[p];
            int tf = e->tfs[p];
            double denom = (double)tf + k1 * (1.0 - b + b * ((double)ix->doc_len[i] / ix->avgdl));
            raw[i] += idf * ((double)tf * (k1 + 1.0) / denom);
        }
    }
    for (int i = 0; i < n; i++) {
        float cos = eq ? cos_sim(eq->vetores, emb->vetores + (size_t)i * emb->dimensao, emb->dimensao) : 0.0f;
        if (cos < 0) cos = 0;
        float ra = (float)raw[i];
        float bm = ra / (ra + 8.0f);
        float score = 0.6f * cos + 0.4f * bm;
        if (!ix->valido[i]) {
            scores_out[i] = 0.0f;
            continue;
        }
        /* phrase-boost: janela direta sobre tokens (sem alloc) */
        if (nq > 0 && ix->doc_len[i] >= nq) {
            for (int s = 0; s + nq <= ix->doc_len[i]; s++) {
                int ok = 1;
                for (int qq = 0; qq < nq; qq++) {
                    if (strcmp(ix->toks[i][s + qq], tq[qq]) != 0) { ok = 0; break; }
                }
                if (ok) {
                    score += 0.2f;
                    if (score > 1.0f) score = 1.0f;
                    break;
                }
            }
        }
        scores_out[i] = score;
    }
    free(raw);
}

/* ============ cache de embeddings de query (FIFO 32, thread-safe) ============ */

#define QCACHE_N 32
static struct {
    char *q;
    float vec[EMBEDDER_DIM];
    int used;
} g_qc[QCACHE_N];
static int g_qc_next = 0;
static long g_qc_hits = 0, g_qc_misses = 0;

Embeddings *embed_query_cached(const char *texto) {
    const char *t = texto ? texto : "";
    qc_lock();
    for (int i = 0; i < QCACHE_N; i++) {
        if (g_qc[i].used && strcmp(g_qc[i].q, t) == 0) {
            Embeddings *e = (Embeddings *)xcalloc(1, sizeof(*e));
            e->num_vetores = 1;
            e->dimensao = EMBEDDER_DIM;
            e->vetores = (float *)xcalloc(EMBEDDER_DIM, sizeof(float));
            memcpy(e->vetores, g_qc[i].vec, sizeof g_qc[i].vec);
            g_qc_hits++;
            qc_unlock();
            return e;
        }
    }
    qc_unlock();
    Embeddings *e = embed_query(t);
    qc_lock();
    int slot = g_qc_next;
    g_qc_next = (g_qc_next + 1) % QCACHE_N;
    free(g_qc[slot].q);
    g_qc[slot].q = xstrdup(t);
    if (e && e->vetores)
        memcpy(g_qc[slot].vec, e->vetores,
               sizeof(float) * (size_t)(e->dimensao < EMBEDDER_DIM ? e->dimensao : EMBEDDER_DIM));
    else
        memset(g_qc[slot].vec, 0, sizeof g_qc[slot].vec);
    g_qc[slot].used = 1;
    g_qc_misses++;
    qc_unlock();
    return e;
}

void query_cache_limpar(void) {
    qc_lock();
    for (int i = 0; i < QCACHE_N; i++) { free(g_qc[i].q); g_qc[i].q = NULL; g_qc[i].used = 0; }
    g_qc_next = 0;
    g_qc_hits = g_qc_misses = 0;
    qc_unlock();
}

void query_cache_stats(long *hits_out, long *misses_out) {
    qc_lock();
    if (hits_out) *hits_out = g_qc_hits;
    if (misses_out) *misses_out = g_qc_misses;
    qc_unlock();
}

void decisao_usar_calib_pacote(DecisionConfig *cfg, int tem_calib,
                               float cal_center, float cal_slope,
                               float cal_limiar) {
    if (!cfg || !tem_calib) return;
    if (cfg->conf_center == 0.0f && cfg->conf_slope == 0.0f) {
        cfg->conf_center = cal_center;
        cfg->conf_slope = cal_slope;
    }
    if (cfg->limiar_recusa == 0.0f) cfg->limiar_recusa = cal_limiar;
}

RankItem *indice_recuperar(RetrievalIndex *idx, const char *pergunta,
                           Embeddings *emb, int top_k, int *n_out) {
    if (!idx || idx->num_chunks <= 0 || !emb || !emb->vetores) {
        if (n_out) *n_out = 0;
        return NULL;
    }
    int n = idx->num_chunks;
    if (top_k <= 0) top_k = 3;
    if (top_k > n) top_k = n;
    int nq = 0;
    char **tq = tokenizar(pergunta ? pergunta : "", &nq);
    Embeddings *eq = embed_query_cached(pergunta ? pergunta : "");
    float *scores = (float *)xcalloc((size_t)n, sizeof(float));
    indice_pontuar(idx, tq, nq, eq, emb, scores);
    liberar_tokens(tq, nq);
    liberar_embeddings(eq);
    RankItem *all = (RankItem *)xmalloc(sizeof(RankItem) * (size_t)n);
    for (int i = 0; i < n; i++) { all[i].indice_chunk = i; all[i].score = scores[i]; }
    free(scores);
    qsort(all, (size_t)n, sizeof(RankItem), cmp_rank);
    RankItem *top = (RankItem *)xmalloc(sizeof(RankItem) * (size_t)top_k);
    memcpy(top, all, sizeof(RankItem) * (size_t)top_k);
    free(all);
    if (n_out) *n_out = top_k;
    return top;
}

RankItem *recuperar_chunks(const char *pergunta, Chunk *chunks, int num_chunks,
                           Embeddings *emb, int top_k, int *n_out) {
    if (num_chunks <= 0 || !emb || !emb->vetores) {
        if (n_out) *n_out = 0;
        return NULL;
    }
    RetrievalIndex *ix = indice_criar(chunks, num_chunks);
    RankItem *r = indice_recuperar(ix, pergunta, emb, top_k, n_out);
    indice_liberar(ix);
    return r;
}

static float sigmoid(float x) { return 1.0f / (1.0f + expf(-x)); }

/* nucleo comum: rank ja calculado -> decisao (citacao top-2 + sigmoide). */
static Decisao *decisao_de_rank(const char *pergunta, Chunk *chunks,
                                RankItem *rk, int n,
                                const DecisionConfig *cfg) {
    (void)pergunta;
    DecisionConfig c;
    memset(&c, 0, sizeof c);
    c.limiar_confianca = 0.7f; c.limiar_recusa = 0.3f; c.top_k = 3;
    if (cfg) c = *cfg;
    Decisao *d = (Decisao *)xcalloc(1, sizeof(*d));
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
    return d;
}

Decisao *executar_decisao(const char *pergunta, Chunk *chunks, int num_chunks,
                           Embeddings *emb, const DecisionConfig *cfg) {
    DecisionConfig c;
    memset(&c, 0, sizeof c);
    c.limiar_confianca = 0.7f; c.limiar_recusa = 0.3f; c.top_k = 3;
    if (cfg) c = *cfg;
    int n = 0;
    RankItem *rk = recuperar_chunks(pergunta, chunks, num_chunks, emb, c.top_k, &n);
    Decisao *d = decisao_de_rank(pergunta, chunks, rk, n, &c);
    free(rk);
    return d;
}

Decisao *executar_decisao_idx(const char *pergunta, RetrievalIndex *idx,
                              Chunk *chunks, int num_chunks,
                              Embeddings *emb, const DecisionConfig *cfg) {
    DecisionConfig c;
    memset(&c, 0, sizeof c);
    c.limiar_confianca = 0.7f; c.limiar_recusa = 0.3f; c.top_k = 3;
    if (cfg) c = *cfg;
    if (!idx || num_chunks <= 0) {
        Decisao *d = (Decisao *)xcalloc(1, sizeof(*d));
        d->resposta = xstrdup("Nao encontrei informacao suficiente no documento.");
        d->recusada = 1;
        return d;
    }
    (void)num_chunks;
    int n = 0;
    RankItem *rk = indice_recuperar(idx, pergunta, emb, c.top_k, &n);
    Decisao *d = decisao_de_rank(pergunta, chunks, rk, n, &c);
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

const char *decision_backend_nome(int via) {
    switch (via) {
    case DECISION_BACKEND_LAYA_HTTP: return "laya-http";
    case DECISION_BACKEND_TYPESAFE_HTTP: return "typesafe-http";
    case DECISION_BACKEND_DEEPSEEK_HTTP: return "deepseek-http";
    default: return "local";
    }
}

/* Nucleo remoto compartilhado pelas duas hibridas: recebe a decisao
 * local (grounding pronto) e tenta o backend configurado.
 * typesafe: calibra prob/conf pelo noul do JEV real (state = citacao
 * local). laya/deepseek: redigem sobre a citacao. Qualquer falha =
 * local intacto. *via_out: 0 local, 1 laya, 2 typesafe, 3 deepseek. */
static Decisao *hibrida_remota(Decisao *local, const char *pergunta,
                               const DecisionConfig *cfg, int *via_out) {
    if (via_out) *via_out = 0;
    if (!cfg || cfg->backend == DECISION_BACKEND_LOCAL) return local;
    if (!pergunta || !local || local->recusada) return local;

    if (cfg->backend == DECISION_BACKEND_TYPESAFE_HTTP) {
        const char *ctx = (local->citacao && local->citacao[0])
            ? local->citacao : (local->resposta ? local->resposta : "");
        double noul = 0.0;
        char *err = NULL;
        TsStatus st = typesafe_judge(cfg->typesafe_url, cfg->typesafe_model,
                                     cfg->typesafe_key, ctx, pergunta,
                                     cfg->typesafe_timeout_ms, &noul, &err);
        if (st != TS_OK) {
            if (getenv("AMANDA_DEBUG"))
                fprintf(stderr, "amanda: typesafe-judge falhou: %s\n",
                        err ? err : "?");
            free(err);
            return local;
        }
        free(err);
        float lim = (cfg->limiar_recusa > 0.0f) ? cfg->limiar_recusa : 0.3f;
        local->probabilidade = (float)noul;
        local->confianca = (float)noul;
        local->recusada = (noul < (double)lim) ? 1 : 0;
        if (via_out) *via_out = DECISION_BACKEND_TYPESAFE_HTTP;
        return local;
    }

    int is_ds = (cfg->backend == DECISION_BACKEND_DEEPSEEK_HTTP);
    if (!is_ds && cfg->backend != DECISION_BACKEND_LAYA_HTTP) return local;

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
    char *rerr = NULL;
    int ok = 0;
    if (is_ds) {
        const char *url = cfg->deepseek_url[0] ? cfg->deepseek_url : DEEPSEEK_URL_DEFAULT;
        int timeout = cfg->deepseek_timeout_ms > 0 ? cfg->deepseek_timeout_ms : DEEPSEEK_TIMEOUT_DEFAULT_MS;
        ok = (deepseek_redact(url, cfg->deepseek_model, cfg->deepseek_key,
                              (char *)msg.data, timeout, &conteudo, &rerr) == DS_OK);
    } else {
        const char *url = cfg->laya_url[0] ? cfg->laya_url : LAYA_URL_DEFAULT;
        int timeout = cfg->laya_timeout_ms > 0 ? cfg->laya_timeout_ms : LAYA_TIMEOUT_DEFAULT_MS;
        ok = (laya_chat(url, (char *)msg.data, timeout, &conteudo, &rerr) == LAYA_OK);
    }
    buf_free(&msg);
    if (!ok) {
        if (getenv("AMANDA_DEBUG"))
            fprintf(stderr, "amanda: %s falhou: %s\n",
                    is_ds ? "deepseek-redact" : "laya-chat",
                    rerr ? rerr : "?");
        free(rerr);
        free(conteudo);
        return local;
    }
    free(rerr);
    if (strstr(conteudo, "NAO CONSTA") != NULL) {
        free(conteudo);
        return local;
    }
    free(local->resposta);
    {
        ByteBuf b; buf_init(&b);
        buf_append_cstr(&b, conteudo);
        buf_append_cstr(&b, is_ds ? " (via DeepSeek)" : " (via Laya)");
        buf_reserve(&b, 1);
        b.data[b.len] = '\0';
        local->resposta = (char *)b.data;
    }
    free(conteudo);
    if (via_out) *via_out = is_ds ? DECISION_BACKEND_DEEPSEEK_HTTP : DECISION_BACKEND_LAYA_HTTP;
    return local;
}

Decisao *executar_decisao_hibrida(const char *pergunta, Chunk *chunks, int num_chunks,
                                   Embeddings *emb, const DecisionConfig *cfg,
                                   int *via_out) {
    Decisao *local = executar_decisao(pergunta, chunks, num_chunks, emb, cfg);
    return hibrida_remota(local, pergunta, cfg, via_out);
}

Decisao *executar_decisao_hibrida_idx(const char *pergunta, RetrievalIndex *idx,
                                      Chunk *chunks, int num_chunks,
                                      Embeddings *emb, const DecisionConfig *cfg,
                                      int *via_out) {
    Decisao *local = executar_decisao_idx(pergunta, idx, chunks, num_chunks, emb, cfg);
    return hibrida_remota(local, pergunta, cfg, via_out);
}
