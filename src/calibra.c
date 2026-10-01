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

    Ponto *pts = (Ponto *)xmalloc(sizeof(Ponto) * (size_t)(k + N_NEG));
    int m = 0;
    for (int s = 0; s < k; s++) {
        PerguntaTipada *q = &pkg->perguntas[idx[s]];
        int nr = 0;
        RankItem *rk = recuperar_chunks(q->enunciado ? q->enunciado : "",
                                        pkg->chunks, pkg->num_chunks,
                                        pkg->embeddings, 1, &nr);
        pts[m].score = (rk && nr > 0) ? rk[0].score : 0.0f;
        pts[m].y = 1;
        free(rk);
        m++;
    }
    for (int i = 0; i < N_NEG; i++) {
        int nr = 0;
        RankItem *rk = recuperar_chunks(NEG_PROBES[i], pkg->chunks, pkg->num_chunks,
                                        pkg->embeddings, 1, &nr);
        pts[m].score = (rk && nr > 0) ? rk[0].score : 0.0f;
        pts[m].y = 0;
        free(rk);
        m++;
    }
    free(idx);

    out->n_pos = k;
    out->n_neg = N_NEG;

    double tpr, tnr, gap;
    metricas(pts, m, out->cur_center, out->cur_slope, out->cur_limiar, &tpr, &tnr, &gap);
    out->cur_tpr = tpr; out->cur_tnr = tnr;
    out->cur_bal = (tpr + tnr) / 2.0;
    out->cur_gap = gap;

    double best_bal = -1.0, best_gap = 1e9, best_tpr = 0.0, best_tnr = 0.0;
    float b_c = out->cur_center, b_s = out->cur_slope, b_l = out->cur_limiar;
    for (int ci = 0; ci < 12; ci++) {
        float center = 0.05f + 0.05f * (float)ci;
        for (int si = 0; si < 14; si++) {
            float slope = 4.0f + 2.0f * (float)si;
            for (int li = 0; li < 17; li++) {
                float lim = 0.10f + 0.05f * (float)li;
                double t2, n2, g2;
                metricas(pts, m, center, slope, lim, &t2, &n2, &g2);
                double bal = (t2 + n2) / 2.0;
                if (bal > best_bal + 1e-9 ||
                    (fabs(bal - best_bal) <= 1e-9 && g2 < best_gap - 1e-9)) {
                    best_bal = bal; best_gap = g2;
                    best_tpr = t2; best_tnr = n2;
                    b_c = center; b_s = slope; b_l = lim;
                }
            }
        }
    }
    free(pts);

    out->sug_center = b_c;
    out->sug_slope = b_s;
    out->sug_limiar = b_l;
    out->sug_tpr = best_tpr;
    out->sug_tnr = best_tnr;
    out->sug_bal = best_bal;
    out->sug_gap = best_gap;
    return 0;
}

char *calibra_to_json(const CalibraReport *r, const char *package_path) {
    ByteBuf b;
    buf_init(&b);
    char tmp[2048];
    const char *pk = package_path ? package_path : "";
    char *esc = json_escape(pk);
    snprintf(tmp, sizeof tmp,
        "{\"package\":\"%s\",\"n_pos\":%d,\"n_neg\":%d,"
        "\"atual\":{\"center\":%.3f,\"slope\":%.1f,\"limiar\":%.2f,\"tpr\":%.4f,\"tnr\":%.4f,\"bal\":%.4f,\"gap\":%.4f},"
        "\"sugerido\":{\"center\":%.3f,\"slope\":%.1f,\"limiar\":%.2f,\"tpr\":%.4f,\"tnr\":%.4f,\"bal\":%.4f,\"gap\":%.4f}}",
        esc, r->n_pos, r->n_neg,
        r->cur_center, r->cur_slope, r->cur_limiar, r->cur_tpr, r->cur_tnr, r->cur_bal, r->cur_gap,
        r->sug_center, r->sug_slope, r->sug_limiar, r->sug_tpr, r->sug_tnr, r->sug_bal, r->sug_gap);
    free(esc);
    buf_append_cstr(&b, tmp);
    buf_reserve(&b, 1);
    b.data[b.len] = '\0';
    return (char *)b.data;
}

void calibra_print_text(const CalibraReport *r, const char *package_path) {
    printf("calibrate: %s\n", package_path ? package_path : "(?)");
    printf("  amostras: pos=%d (in-scope) neg=%d (probes)\n", r->n_pos, r->n_neg);
    printf("  atual:    center=%.3f slope=%.1f limiar=%.2f | TPR=%.2f TNR=%.2f bal=%.3f gap=%.3f\n",
           r->cur_center, r->cur_slope, r->cur_limiar,
           r->cur_tpr, r->cur_tnr, r->cur_bal, r->cur_gap);
    printf("  sugerido: center=%.3f slope=%.1f limiar=%.2f | TPR=%.2f TNR=%.2f bal=%.3f gap=%.3f\n",
           r->sug_center, r->sug_slope, r->sug_limiar,
           r->sug_tpr, r->sug_tnr, r->sug_bal, r->sug_gap);
    printf("  uso: amandac ask --package <pkg> \"pergunta\" --conf-center %.3f --conf-slope %.1f --limiar-recusa %.2f\n",
           r->sug_center, r->sug_slope, r->sug_limiar);
}
