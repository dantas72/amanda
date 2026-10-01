#ifndef EVAL_H
#define EVAL_H

#include "packager.h"

typedef struct {
    double sample;
    unsigned int seed;
    int top_k;
    int backend;
    char laya_url[256];
    float conf_center;
    float conf_slope;
    float limiar_recusa;
    int tem_limiar;
    /* Fase 10: teto de amostradas (0 = sem teto). Quando o sorteio
       excede, usa as primeiras `max_amostras` do embaralhamento
       (deterministico dado seed) e relata amostradas = teto. */
    int max_amostras;
} EvalConfig;

typedef struct {
    int total_perguntas;
    int amostradas;
    int acertos;
    int recusas_in;
    double fidelidade;
    double acuracia;
    double confianca_media;
    double gap_calibracao;
    double ece;
    double lat_media_ms;
    double lat_max_ms;
    int n_choice;
    int n_score;
    int n_noul;
    int hit_choice;
    int hit_score;
    int hit_noul;
    int n_probes;
    int recusas_probe;
    double taxa_recusa_probe;
    int pass_latencia;
    int via_laya;
    int via_local;
    /* Fase 7.4: cobertura da extracao (derivada do pacote v2) */
    int cov_paginas;
    int cov_blocos;
    int cov_chunks;
    long long cov_chars;
    double cov_chars_por_pag;
    double cov_perg_por_chunk;
    int cov_total_streams;
    int cov_text_streams;
    int cov_failed;
    int cov_fallback;
} EvalReport;

int eval_run(AmandaPackage *pkg, const EvalConfig *cfg, EvalReport *out, char **erro);
char *eval_to_json(const EvalReport *r, const char *package_path);
void eval_print_text(const EvalReport *r, const char *package_path);

#endif
