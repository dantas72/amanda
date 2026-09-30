#include "eval.h"
#include "decision_engine.h"
#include "question_gen.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char *PROBES_FORA_ESCOPO[] = {
    "Qual e a capital de Marte no ano de 3020?",
    "Explique a receita de bolo de cenoura com cobertura quantica intergalactica.",
    "Quem venceu a copa do mundo de 2150?"
};
#define N_PROBES 3

static unsigned int lcg_next(unsigned int *s) {
    *s = (*s) * 1664525u + 1013904223u;
    return *s;
}

int eval_run(AmandaPackage *pkg, const EvalConfig *cfg, EvalReport *out, char **erro) {
    if (!pkg || !out) {
        if (erro) *erro = xstrdup("eval: pacote ou relatorio nulo");
        return -1;
    }
    if (pkg->num_perguntas <= 0) {
        if (erro) *erro = xstrdup("eval: pacote sem perguntas (compile gerou 0 perguntas?)");
        return -1;
    }
    if (!pkg->chunks || pkg->num_chunks <= 0 || !pkg->embeddings) {
        if (erro) *erro = xstrdup("eval: pacote sem chunks/embeddings");
        return -1;
    }
    EvalConfig c = {0.1, 42u, 3};
    if (cfg) c = *cfg;
    if (c.sample <= 0.0) c.sample = 0.1;
    if (c.sample > 1.0) c.sample = 1.0;
    if (c.top_k <= 0) c.top_k = 3;

    memset(out, 0, sizeof *out);
    out->total_perguntas = pkg->num_perguntas;

    int n = pkg->num_perguntas;
    int k = (int)floor((double)n * c.sample + 0.5);
    if (k < 1) k = 1;
    if (k > n) k = n;
    out->amostradas = k;

    int *idx = (int *)xmalloc(sizeof(int) * (size_t)n);
    for (int i = 0; i < n; i++) idx[i] = i;
    unsigned int st = c.seed;
    for (int i = n - 1; i > 0; i--) {
        unsigned int r = lcg_next(&st);
        int j = (int)(r % (unsigned int)(i + 1));
        int t = idx[i]; idx[i] = idx[j]; idx[j] = t;
    }

    DecisionConfig dc = {0.7f, 0.3f, c.top_k};

    double soma_conf = 0.0;
    double soma_lat = 0.0;
    double lat_max = 0.0;
    int acertos = 0;
    int recusas = 0;

    double bin_conf[5] = {0, 0, 0, 0, 0};
    int bin_n[5] = {0, 0, 0, 0, 0};
    int bin_hit[5] = {0, 0, 0, 0, 0};

    for (int s = 0; s < k; s++) {
        PerguntaTipada *q = &pkg->perguntas[idx[s]];
        const char *query = q->enunciado ? q->enunciado : "";
        if (q->tipo == TIPO_CHOICE) out->n_choice++;
        else if (q->tipo == TIPO_SCORE) out->n_score++;
        else if (q->tipo == TIPO_NOUL) out->n_noul++;

        long long t0 = now_ms();
        Decisao *d = executar_decisao(query, pkg->chunks, pkg->num_chunks, pkg->embeddings, &dc);
        long long t1 = now_ms();
        double lat = (double)(t1 - t0);
        soma_lat += lat;
        if (lat > lat_max) lat_max = lat;
        soma_conf += (double)d->confianca;

        int b = (int)floor((double)d->confianca * 5.0);
        if (b < 0) b = 0;
        if (b > 4) b = 4;
        bin_n[b]++;
        bin_conf[b] += (double)d->confianca;

        int hit = (!d->recusada && d->pagina == q->pagina_fonte) ? 1 : 0;
        if (hit) {
            acertos++;
            bin_hit[b]++;
            if (q->tipo == TIPO_CHOICE) out->hit_choice++;
            else if (q->tipo == TIPO_SCORE) out->hit_score++;
            else if (q->tipo == TIPO_NOUL) out->hit_noul++;
        }
        if (d->recusada) {
            recusas++;
            out->recusas_in++;
        }
        liberar_decisao(d);
    }
    free(idx);

    out->acertos = acertos;
    out->acuracia = k > 0 ? (double)acertos / (double)k : 0.0;
    out->fidelidade = out->acuracia;
    out->confianca_media = k > 0 ? soma_conf / (double)k : 0.0;
    out->gap_calibracao = fabs(out->confianca_media - out->acuracia);
    out->lat_media_ms = k > 0 ? soma_lat / (double)k : 0.0;
    out->lat_max_ms = lat_max;

    double ece = 0.0;
    for (int b = 0; b < 5; b++) {
        if (bin_n[b] == 0) continue;
        double acc_b = (double)bin_hit[b] / (double)bin_n[b];
        double conf_b = bin_conf[b] / (double)bin_n[b];
        ece += ((double)bin_n[b] / (double)k) * fabs(acc_b - conf_b);
    }
    out->ece = ece;
    out->pass_latencia = (out->lat_media_ms <= 500.0) ? 1 : 0;

    int rp = 0;
    for (int i = 0; i < N_PROBES; i++) {
        Decisao *d = executar_decisao(PROBES_FORA_ESCOPO[i], pkg->chunks,
                                      pkg->num_chunks, pkg->embeddings, &dc);
        if (d->recusada) rp++;
        liberar_decisao(d);
    }
    out->n_probes = N_PROBES;
    out->recusas_probe = rp;
    out->taxa_recusa_probe = N_PROBES > 0 ? (double)rp / (double)N_PROBES : 0.0;
    return 0;
}

char *eval_to_json(const EvalReport *r, const char *package_path) {
    ByteBuf b;
    buf_init(&b);
    char tmp[2048];
    const char *pk = package_path ? package_path : "";
    char *esc = json_escape(pk);
    snprintf(tmp, sizeof tmp,
        "{\"package\":\"%s\",\"total_perguntas\":%d,\"amostradas\":%d,"
        "\"acertos\":%d,\"fidelidade\":%.4f,\"acuracia\":%.4f,"
        "\"confianca_media\":%.4f,\"gap_calibracao\":%.4f,\"ece\":%.4f,"
        "\"lat_media_ms\":%.2f,\"lat_max_ms\":%.2f,\"pass_latencia\":%s,"
        "\"recusas_in_scope\":%d,"
        "\"por_tipo\":{\"choice\":[%d,%d],\"score\":[%d,%d],\"noul\":[%d,%d]},"
        "\"probes_fora_escopo\":%d,\"recusas_probe\":%d,\"taxa_recusa_probe\":%.4f}",
        esc, r->total_perguntas, r->amostradas,
        r->acertos, r->fidelidade, r->acuracia,
        r->confianca_media, r->gap_calibracao, r->ece,
        r->lat_media_ms, r->lat_max_ms, r->pass_latencia ? "true" : "false",
        r->recusas_in,
        r->hit_choice, r->n_choice, r->hit_score, r->n_score, r->hit_noul, r->n_noul,
        r->n_probes, r->recusas_probe, r->taxa_recusa_probe);
    free(esc);
    buf_append_cstr(&b, tmp);
    buf_reserve(&b, 1);
    b.data[b.len] = '\0';
    return (char *)b.data;
}

void eval_print_text(const EvalReport *r, const char *package_path) {
    printf("eval: %s\n", package_path ? package_path : "(?)");
    printf("  perguntas: total=%d amostradas=%d (%.0f%%)\n",
           r->total_perguntas, r->amostradas,
           r->total_perguntas > 0 ? 100.0 * (double)r->amostradas / (double)r->total_perguntas : 0.0);
    printf("  fidelidade (pagina correta): %.2f%% (%d/%d)\n",
           r->fidelidade * 100.0, r->acertos, r->amostradas);
    printf("  acuracia: %.2f%% | confianca media: %.2f | gap: %.3f | ECE(5bins): %.3f\n",
           r->acuracia * 100.0, r->confianca_media, r->gap_calibracao, r->ece);
    printf("  latencia: media=%.1f ms max=%.1f ms (meta <=500ms) [%s]\n",
           r->lat_media_ms, r->lat_max_ms, r->pass_latencia ? "PASS" : "FAIL");
    printf("  recusa in-scope: %d/%d (%.1f%%)\n", r->recusas_in, r->amostradas,
           r->amostradas > 0 ? 100.0 * (double)r->recusas_in / (double)r->amostradas : 0.0);
    printf("  por tipo: choice %d/%d | score %d/%d | noul %d/%d\n",
           r->hit_choice, r->n_choice, r->hit_score, r->n_score, r->hit_noul, r->n_noul);
    printf("  recusa fora-escopo (probes): %d/%d (%.1f%%)\n",
           r->recusas_probe, r->n_probes, r->taxa_recusa_probe * 100.0);
}
