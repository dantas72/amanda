#ifndef EVAL_H
#define EVAL_H

#include "packager.h"

typedef struct {
    double sample;
    unsigned int seed;
    int top_k;
    int backend;
    char laya_url[256];
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
} EvalReport;

int eval_run(AmandaPackage *pkg, const EvalConfig *cfg, EvalReport *out, char **erro);
char *eval_to_json(const EvalReport *r, const char *package_path);
void eval_print_text(const EvalReport *r, const char *package_path);

#endif
