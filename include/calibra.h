#ifndef CALIBRA_H
#define CALIBRA_H

#include "packager.h"

typedef struct {
    double sample;
    unsigned int seed;
} CalibraConfig;

typedef struct {
    int n_pos;
    int n_neg;
    float cur_center;
    float cur_slope;
    float cur_limiar;
    double cur_tpr;
    double cur_tnr;
    double cur_bal;
    double cur_gap;
    float sug_center;
    float sug_slope;
    float sug_limiar;
    double sug_tpr;
    double sug_tnr;
    double sug_bal;
    double sug_gap;
} CalibraReport;

/* Roda recuperacao sobre amostra das perguntas (positivas) + probes
   fora-escopo (negativas) e busca em grade (centro, inclinacao, limiar)
   que maximize a acuracia balanceada (TPR+TNR)/2, com desempate pelo
   menor gap |confianca-acuracia|. Deterministico. */
int calibra_run(AmandaPackage *pkg, const CalibraConfig *cfg, CalibraReport *out, char **erro);
char *calibra_to_json(const CalibraReport *r, const char *package_path);
void calibra_print_text(const CalibraReport *r, const char *package_path);

#endif
