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
    EvalConfig c;
    memset(&c, 0, sizeof c);
    c.sample = 0.1; c.seed = 42u; c.top_k = 3;
    if (cfg) c = *cfg;
    if (c.sample <= 0.0) c.sample = 0.1;
    if (c.sample > 1.0) c.sample = 1.0;
    if (c.top_k <= 0) c.top_k = 3;

    memset(out, 0, sizeof *out);
    out->total_perguntas = pkg->num_perguntas;

    /* Fase 7.4: cobertura da extracao direto do pacote (v2; v1 = zeros) */
    out->cov_paginas = pkg->num_paginas;
    out->cov_blocos = pkg->extra_blocos;
    out->cov_chunks = pkg->num_chunks;
    {
        long long tc = 0;
        for (int i = 0; i < pkg->num_chunks; i++)
            if (pkg->chunks[i].texto) tc += (long long)strlen(pkg->chunks[i].texto);
        out->cov_chars = tc;
    }
    out->cov_chars_por_pag = out->cov_paginas > 0
        ? (double)out->cov_chars / (double)out->cov_paginas : 0.0;
    out->cov_perg_por_chunk = pkg->num_chunks > 0
        ? (double)pkg->num_perguntas / (double)pkg->num_chunks : 0.0;
    out->cov_total_streams = pkg->extra_total_streams;
    out->cov_text_streams = pkg->extra_text_streams;
    out->cov_failed = pkg->extra_failed;
    out->cov_fallback = pkg->extra_fallback;

    int n = pkg->num_perguntas;
    int k = (int)floor((double)n * c.sample + 0.5);
    if (k < 1) k = 1;
    if (k > n) k = n;
    if (c.max_amostras > 0 && k > c.max_amostras) k = c.max_amostras;
    out->amostradas = k;

    int *idx = (int *)xmalloc(sizeof(int) * (size_t)n);
    for (int i = 0; i < n; i++) idx[i] = i;
    unsigned int st = c.seed;
    for (int i = n - 1; i > 0; i--) {
        unsigned int r = lcg_next(&st);
        int j = (int)(r % (unsigned int)(i + 1));
        int t = idx[i]; idx[i] = idx[j]; idx[j] = t;
    }

    DecisionConfig dc;
    memset(&dc, 0, sizeof dc);
    dc.limiar_confianca = 0.7f;
    dc.top_k = c.top_k;
    dc.backend = c.backend;
    if (c.laya_url[0])
        snprintf(dc.laya_url, sizeof dc.laya_url, "%s", c.laya_url);
    dc.conf_center = c.conf_center;
    dc.conf_slope = c.conf_slope;
    if (c.tem_limiar) dc.limiar_recusa = c.limiar_recusa;
    /* Fase 12.4: calibracao gravada no pacote (v3) quando a CLI
       nao especificou (zeros / sem tem_limiar). */
    decisao_usar_calib_pacote(&dc, pkg->tem_calib,
                              pkg->cal_center, pkg->cal_slope, pkg->cal_limiar);
    if (dc.limiar_recusa == 0.0f) dc.limiar_recusa = 0.3f;

    /* Fase 12.4: indice 1x por run (antes: retokenizava N chunks
       por query; Direito 3811 chunks/query ~447ms). */
    RetrievalIndex *rix = indice_criar(pkg->chunks, pkg->num_chunks);

    double soma_conf = 0.0;
    double soma_lat = 0.0;
    double lat_max = 0.0;
    int acertos = 0;
    int recusas = 0;

    double bin_conf[5] = {0, 0, 0, 0, 0};
    int bin_n[5] = {0, 0, 0, 0, 0};
    int bin_hit[5] = {0, 0, 0, 0, 0};

    for (int s = 0; s < k; s++) {
        /* Fase 10: progresso em stderr (stdout fica limpo p/ --json) */
        if (k >= 250 && (s % 250 == 0 || s == k - 1)) {
            fprintf(stderr, "eval: %d/%d\n", s + 1, k);
        }
        PerguntaTipada *q = &pkg->perguntas[idx[s]];
        const char *query = q->enunciado ? q->enunciado : "";
        if (q->tipo == TIPO_CHOICE) out->n_choice++;
        else if (q->tipo == TIPO_SCORE) out->n_score++;
        else if (q->tipo == TIPO_NOUL) out->n_noul++;

        long long t0 = now_ms();
        int usou_laya = 0;
        Decisao *d = executar_decisao_hibrida_idx(query, rix, pkg->chunks, pkg->num_chunks, pkg->embeddings, &dc, &usou_laya);
        long long t1 = now_ms();
        if (usou_laya) out->via_laya++; else out->via_local++;
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
        Decisao *d = executar_decisao_idx(PROBES_FORA_ESCOPO[i], rix, pkg->chunks,
                                          pkg->num_chunks, pkg->embeddings, &dc);
        if (d->recusada) rp++;
        liberar_decisao(d);
    }
    indice_liberar(rix);
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
        "\"probes_fora_escopo\":%d,\"recusas_probe\":%d,\"taxa_recusa_probe\":%.4f,"
        "\"via_laya\":%d,\"via_local\":%d,"
        "\"cobertura\":{\"paginas\":%d,\"blocos\":%d,\"chunks\":%d,"
        "\"chars\":%lld,\"chars_por_pag\":%.1f,\"perg_por_chunk\":%.2f,"
        "\"streams\":%d,\"streams_texto\":%d,\"falhas\":%d,\"fallback\":%s}}",
        esc, r->total_perguntas, r->amostradas,
        r->acertos, r->fidelidade, r->acuracia,
        r->confianca_media, r->gap_calibracao, r->ece,
        r->lat_media_ms, r->lat_max_ms, r->pass_latencia ? "true" : "false",
        r->recusas_in,
        r->hit_choice, r->n_choice, r->hit_score, r->n_score, r->hit_noul, r->n_noul,
        r->n_probes, r->recusas_probe, r->taxa_recusa_probe,
        r->via_laya, r->via_local,
        r->cov_paginas, r->cov_blocos, r->cov_chunks,
        r->cov_chars, r->cov_chars_por_pag, r->cov_perg_por_chunk,
        r->cov_total_streams, r->cov_text_streams, r->cov_failed,
        r->cov_fallback ? "true" : "false");
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
    if (r->via_laya + r->via_local > 0)
        printf("  backend: laya=%d local=%d\n", r->via_laya, r->via_local);
    printf("  cobertura: paginas=%d blocos=%d chunks=%d chars=%lld (%.0f/pag) perg/chunk=%.2f\n",
           r->cov_paginas, r->cov_blocos, r->cov_chunks,
           r->cov_chars, r->cov_chars_por_pag, r->cov_perg_por_chunk);
    printf("  extracao: streams=%d texto=%d falhas=%d fallback=%s\n",
           r->cov_total_streams, r->cov_text_streams, r->cov_failed,
           r->cov_fallback ? "sim" : "nao");
}
