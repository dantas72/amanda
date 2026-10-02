#include "calibra.h"
#include "decision_engine.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char *NEG_PROBES[] = {
    "Qual e a capital de Marte no ano de 3020?",
    "Explique a receita de bolo de cenoura com cobertura quantica intergalactica.",
    "Quem venceu a copa do mundo de 2150?"
};
#define N_NEG ((int)(sizeof NEG_PROBES / sizeof NEG_PROBES[0]))

static unsigned int lcg_next(unsigned int *s) {
    *s = (*s) * 1664525u + 1013904223u;
    return *s;
}

/* Fase 12.4b: extrai pares (pergunta, pagina_esperada) de um gold JSON.
   Parser minimo sem dependencias: procura "pergunta" : "..." (com
   unescape) e associa a "pagina_esperada" mais proxima no mesmo
   objeto (opcional; -1 quando ausente). Retorna 0 ok, -1 erro. */
typedef struct {
    char *pergunta;
    int pagina;
} ValQ;

static void val_free(ValQ *v, int n) {
    for (int i = 0; i < n; i++) free(v[i].pergunta);
    free(v);
}

static const char *json_skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

/* copia string JSON com unescape basico; *end recebe apos fechar. */
static char *json_dup_str(const char *p, const char **end) {
    ByteBuf b; buf_init(&b);
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) {
            char e = p[1];
            if (e == 'n') buf_append(&b, "\n", 1);
            else if (e == 't') buf_append(&b, "\t", 1);
            else if (e == 'r') buf_append(&b, "\r", 1);
            else if (e == 'u') {
                unsigned int cp = 0;
                if (sscanf(p + 2, "%4x", &cp) == 1 && cp < 128) {
                    char c = (char)cp;
                    buf_append(&b, &c, 1);
                } else buf_append(&b, "?", 1);
                p += 6;
                continue;
            } else buf_append(&b, &e, 1);
            p += 2;
        } else { buf_append(&b, p, 1); p++; }
    }
    buf_reserve(&b, 1); b.data[b.len] = '\0';
    if (end) *end = (*p == '"') ? p + 1 : p;
    return (char *)b.data;
}

static int val_carregar(const char *path, ValQ **out, int *n_out, char **erro) {
    *out = NULL; *n_out = 0;
    char *txt = read_file_text(path);
    if (!txt) {
        if (erro) *erro = xstrdup("validacao: nao foi possivel ler arquivo");
        return -1;
    }
    int cap = 32, n = 0;
    ValQ *v = (ValQ *)xmalloc(sizeof(ValQ) * (size_t)cap);
    /* scan sequencial: "pagina_esperada" arma pendente; "pergunta"
       consome (ordem dos golds: pagina antes da pergunta). */
    int pend_pg = -1;
    const char *p = txt;
    while (*p) {
        const char *kpg = strstr(p, "\"pagina_esperada\"");
        const char *kq = strstr(p, "\"pergunta\"");
        const char *nx = NULL;
        int eh_pg = 0;
        if (kpg && (!kq || kpg < kq)) { nx = kpg; eh_pg = 1; }
        else if (kq) { nx = kq; eh_pg = 0; }
        else break;
        p = nx + (eh_pg ? 17 : 10);
        p = json_skip_ws(p);
        if (*p != ':') continue;
        p = json_skip_ws(p + 1);
        if (eh_pg) {
            pend_pg = atoi(p);
            while (*p && *p != ',' && *p != '}') p++;
        } else {
            if (*p != '"') continue;
            const char *end = NULL;
            char *q = json_dup_str(p + 1, &end);
            p = end ? end : p + 1;
            if (q && q[0]) {
                if (n >= cap) { cap *= 2; v = (ValQ *)xrealloc(v, sizeof(ValQ) * (size_t)cap); }
                v[n].pergunta = q;
                v[n].pagina = pend_pg;
                n++;
            } else free(q);
            pend_pg = -1;
        }
    }
    free(txt);
    if (n == 0) {
        free(v);
        if (erro) *erro = xstrdup("validacao: nenhuma pergunta encontrada (esperado {\"perguntas\":[{\"pergunta\":\"...\"}]})");
        return -1;
    }
    *out = v; *n_out = n;
    return 0;
}

int calibra_carregar_validacao(const char *path, CalibraValQ **out, int *n_out, char **erro) {
    ValQ *v = NULL;
    int n = 0;
    if (val_carregar(path, &v, &n, erro) != 0) return -1;
    *out = (CalibraValQ *)v;
    *n_out = n;
    return 0;
}

void calibra_liberar_validacao(CalibraValQ *v, int n) {
    val_free((ValQ *)v, n);
}

static float conf_of(float score, float center, float slope) {
    float z = (score - center) * slope;
    if (z > 20.0f) z = 20.0f;
    if (z < -20.0f) z = -20.0f;
    return 1.0f / (1.0f + expf(-z));
}

typedef struct {
    float score;
    int y;
} Ponto;

static void metricas(Ponto *pts, int n, float center, float slope, float lim,
                     double *tpr, double *tnr, double *gap) {
    int tp = 0, fp = 0, tn = 0, fn = 0;
    double soma_conf = 0.0;
    int acertos = 0;
    for (int i = 0; i < n; i++) {
        float c = conf_of(pts[i].score, center, slope);
        soma_conf += (double)c;
        int prev_pos = (c >= lim) ? 1 : 0;
        int hit = (prev_pos == pts[i].y) ? 1 : 0;
        if (hit) acertos++;
        if (pts[i].y == 1 && prev_pos == 1) tp++;
        else if (pts[i].y == 1) fn++;
        else if (prev_pos == 1) fp++;
        else tn++;
    }
    int np = tp + fn, nn = tn + fp;
    *tpr = np > 0 ? (double)tp / (double)np : 1.0;
    *tnr = nn > 0 ? (double)tn / (double)nn : 1.0;
    double acc = n > 0 ? (double)acertos / (double)n : 0.0;
    double cm = n > 0 ? soma_conf / (double)n : 0.0;
    *gap = fabs(cm - acc);
}

int calibra_run(AmandaPackage *pkg, const CalibraConfig *cfg, CalibraReport *out, char **erro) {
    if (!pkg || !out) {
        if (erro) *erro = xstrdup("calibra: pacote ou relatorio nulo");
        return -1;
    }
    if (pkg->num_perguntas <= 0 || !pkg->chunks || pkg->num_chunks <= 0 || !pkg->embeddings) {
        if (erro) *erro = xstrdup("calibra: pacote sem perguntas/chunks/embeddings");
        return -1;
    }
    CalibraConfig c;
    memset(&c, 0, sizeof c);
    c.sample = 0.5; c.seed = 42u;
    if (cfg) c = *cfg;
    if (c.sample <= 0.0) c.sample = 0.5;
    if (c.sample > 1.0) c.sample = 1.0;

    memset(out, 0, sizeof *out);
    out->cur_center = 0.12f;
    out->cur_slope = 12.0f;
    out->cur_limiar = 0.30f;

    int nq = pkg->num_perguntas;
    int k = (int)floor((double)nq * c.sample + 0.5);
    if (k < 1) k = 1;
    if (k > nq) k = nq;

    int *idx = (int *)xmalloc(sizeof(int) * (size_t)nq);
    for (int i = 0; i < nq; i++) idx[i] = i;
    unsigned int st = c.seed;
    for (int i = nq - 1; i > 0; i--) {
        unsigned int r = lcg_next(&st);
        int j = (int)(r % (unsigned int)(i + 1));
        int t = idx[i]; idx[i] = idx[j]; idx[j] = t;
    }

    Ponto *pts = NULL;
    int m = 0;
    /* Fase 12.4: indice 1x por run (antes: retokenizava N chunks
       por pergunta amostrada). */
    RetrievalIndex *rix = indice_criar(pkg->chunks, pkg->num_chunks);
    /* Fase 12.4b: validacao natural (gold) como positivos: ancora a
       cauda de scores reais para o limiar nao recusar o que o rank
       acerta. pts dimensionado p/ amostra + probes + validacao. */
    ValQ *val = NULL;
    int n_val = 0, n_val_cap = 0;
    for (int vf = 0; vf < c.n_validacao; vf++) {
        ValQ *vq = NULL;
        int nq2 = 0;
        char *verr = NULL;
        if (val_carregar(c.validacao[vf], &vq, &nq2, &verr) != 0) {
            indice_liberar(rix);
            free(idx);
            if (erro) *erro = verr ? verr : xstrdup("validacao: falha ao carregar");
            else free(verr);
            return -1;
        }
        if (n_val + nq2 > n_val_cap) {
            n_val_cap = n_val + nq2 + 16;
            val = (ValQ *)xrealloc(val, sizeof(ValQ) * (size_t)n_val_cap);
        }
        for (int i = 0; i < nq2; i++) val[n_val++] = vq[i];
        free(vq);
    }
    pts = (Ponto *)xmalloc(sizeof(Ponto) * (size_t)(k + N_NEG));
    for (int s = 0; s < k; s++) {
        PerguntaTipada *q = &pkg->perguntas[idx[s]];
        int nr = 0;
        RankItem *rk = indice_recuperar(rix, q->enunciado ? q->enunciado : "",
                                        pkg->embeddings, 1, &nr);
        pts[m].score = (rk && nr > 0) ? rk[0].score : 0.0f;
        pts[m].y = 1;
        free(rk);
        m++;
    }
    for (int i = 0; i < N_NEG; i++) {
        int nr = 0;
        RankItem *rk = indice_recuperar(rix, NEG_PROBES[i],
                                        pkg->embeddings, 1, &nr);
        pts[m].score = (rk && nr > 0) ? rk[0].score : 0.0f;
        pts[m].y = 0;
        free(rk);
        m++;
    }
    /* Fase 12.4b: naturais fora do bal (nao diluir): vivem no termo
       val_recall do objetivo. Cache de score+top2 1x. */
    float *vscore = NULL;
    int *vpage1 = NULL, *vpage2 = NULL;
    if (n_val > 0) {
        vscore = (float *)xmalloc(sizeof(float) * (size_t)n_val);
        vpage1 = (int *)xmalloc(sizeof(int) * (size_t)n_val);
        vpage2 = (int *)xmalloc(sizeof(int) * (size_t)n_val);
        for (int i = 0; i < n_val; i++) {
            int nr = 0;
            RankItem *rk = indice_recuperar(rix, val[i].pergunta,
                                            pkg->embeddings, 2, &nr);
            vscore[i] = (rk && nr > 0) ? rk[0].score : 0.0f;
            vpage1[i] = (rk && nr > 0)
                ? pkg->chunks[rk[0].indice_chunk].pagina_inicio : -2;
            vpage2[i] = (rk && nr > 1)
                ? pkg->chunks[rk[1].indice_chunk].pagina_inicio : -2;
            free(rk);
        }
    }
    free(idx);

    out->n_pos = k;
    out->n_neg = N_NEG;
    out->n_val = n_val;

    double tpr, tnr, gap;
    metricas(pts, m, out->cur_center, out->cur_slope, out->cur_limiar, &tpr, &tnr, &gap);
    out->cur_tpr = tpr; out->cur_tnr = tnr;
    out->cur_bal = (tpr + tnr) / 2.0;
    out->cur_gap = gap;

    /* Objetivo: sem validacao = bal puro (Fase 6, intacto); com
       validacao = (bal + recall@2 natural)/2 — mesma semantica do
       gold (pagina OU 2a citacao): o limiar nao pode subir a custa
       das naturais que o rank acerta. */
    double best_obj = -1.0, best_gap = 1e9, best_tpr = 0.0, best_tnr = 0.0;
    double best_vr = 0.0;
    int best_vok = 0;
    float b_c = out->cur_center, b_s = out->cur_slope, b_l = out->cur_limiar;
    /* Grade 12x14x81 = 13608 combos (limiar passo 0.01: separadores
       finos entre probe e natural nao cabem em passo 0.05). */
    for (int ci = 0; ci < 12; ci++) {
        float center = 0.05f + 0.05f * (float)ci;
        for (int si = 0; si < 14; si++) {
            float slope = 4.0f + 2.0f * (float)si;
            for (int li = 0; li <= 80; li++) {
                float lim = 0.10f + 0.01f * (float)li;
                double t2, n2, g2;
                metricas(pts, m, center, slope, lim, &t2, &n2, &g2);
                double bal = (t2 + n2) / 2.0;
                double obj = bal, vr = 1.0;
                int vok = n_val;
                if (n_val > 0) {
                    vok = 0;
                    for (int i = 0; i < n_val; i++) {
                        float cf = conf_of(vscore[i], center, slope);
                        if (cf >= lim &&
                            (val[i].pagina < 0 || vpage1[i] == val[i].pagina ||
                             vpage2[i] == val[i].pagina))
                            vok++;
                    }
                    vr = (double)vok / (double)n_val;
                    obj = (bal + vr) / 2.0;
                }
                if (obj > best_obj + 1e-9 ||
                    (fabs(obj - best_obj) <= 1e-9 && g2 < best_gap - 1e-9)) {
                    best_obj = obj; best_gap = g2;
                    best_tpr = t2; best_tnr = n2;
                    best_vr = vr; best_vok = vok;
                    b_c = center; b_s = slope; b_l = lim;
                }
            }
        }
    }
    free(pts);
    free(vscore);
    free(vpage1);
    free(vpage2);

    out->sug_center = b_c;
    out->sug_slope = b_s;
    out->sug_limiar = b_l;
    out->sug_tpr = best_tpr;
    out->sug_tnr = best_tnr;
    out->sug_bal = (best_tpr + best_tnr) / 2.0;
    out->sug_gap = best_gap;
    out->val_ok = best_vok;
    out->val_recall = best_vr;
    indice_liberar(rix);
    val_free(val, n_val);
    return 0;
}

char *calibra_to_json(const CalibraReport *r, const char *package_path) {
    ByteBuf b;
    buf_init(&b);
    char tmp[2048];
    const char *pk = package_path ? package_path : "";
    char *esc = json_escape(pk);
    snprintf(tmp, sizeof tmp,
        "{\"package\":\"%s\",\"n_pos\":%d,\"n_neg\":%d,\"n_val\":%d,"
        "\"atual\":{\"center\":%.3f,\"slope\":%.1f,\"limiar\":%.2f,\"tpr\":%.4f,\"tnr\":%.4f,\"bal\":%.4f,\"gap\":%.4f},"
        "\"sugerido\":{\"center\":%.3f,\"slope\":%.1f,\"limiar\":%.2f,\"tpr\":%.4f,\"tnr\":%.4f,\"bal\":%.4f,\"gap\":%.4f},"
        "\"validacao\":{\"n\":%d,\"ok\":%d,\"recall\":%.4f}}",
        esc, r->n_pos, r->n_neg, r->n_val,
        r->cur_center, r->cur_slope, r->cur_limiar, r->cur_tpr, r->cur_tnr, r->cur_bal, r->cur_gap,
        r->sug_center, r->sug_slope, r->sug_limiar, r->sug_tpr, r->sug_tnr, r->sug_bal, r->sug_gap,
        r->n_val, r->val_ok, r->val_recall);
    free(esc);
    buf_append_cstr(&b, tmp);
    buf_reserve(&b, 1);
    b.data[b.len] = '\0';
    return (char *)b.data;
}

void calibra_print_text(const CalibraReport *r, const char *package_path) {
    printf("calibrate: %s\n", package_path ? package_path : "(?)");
    printf("  amostras: pos=%d (in-scope) neg=%d (probes) val=%d (naturais)\n",
           r->n_pos, r->n_neg, r->n_val);
    printf("  atual:    center=%.3f slope=%.1f limiar=%.2f | TPR=%.2f TNR=%.2f bal=%.3f gap=%.3f\n",
           r->cur_center, r->cur_slope, r->cur_limiar,
           r->cur_tpr, r->cur_tnr, r->cur_bal, r->cur_gap);
    printf("  sugerido: center=%.3f slope=%.1f limiar=%.2f | TPR=%.2f TNR=%.2f bal=%.3f gap=%.3f\n",
           r->sug_center, r->sug_slope, r->sug_limiar,
           r->sug_tpr, r->sug_tnr, r->sug_bal, r->sug_gap);
    if (r->n_val > 0)
        printf("  validacao natural: recall@2 %d/%d (%.1f%%) no ponto sugerido\n",
               r->val_ok, r->n_val, r->val_recall * 100.0);
    printf("  atual:    center=%.3f slope=%.1f limiar=%.2f | TPR=%.2f TNR=%.2f bal=%.3f gap=%.3f\n",
           r->cur_center, r->cur_slope, r->cur_limiar,
           r->cur_tpr, r->cur_tnr, r->cur_bal, r->cur_gap);
    printf("  sugerido: center=%.3f slope=%.1f limiar=%.2f | TPR=%.2f TNR=%.2f bal=%.3f gap=%.3f\n",
           r->sug_center, r->sug_slope, r->sug_limiar,
           r->sug_tpr, r->sug_tnr, r->sug_bal, r->sug_gap);
    printf("  uso: amandac ask --package <pkg> \"pergunta\" --conf-center %.3f --conf-slope %.1f --limiar-recusa %.2f\n",
           r->sug_center, r->sug_slope, r->sug_limiar);
}
