#ifndef CALIBRA_H
#define CALIBRA_H

#include "packager.h"

typedef struct {
    double sample;
    unsigned int seed;
    /* Fase 12.4b: validacao natural (gold JSON, formato
       {"perguntas":[{"pergunta":"...","pagina_esperada":N}]}).
       Perguntas entram como positivas (y=1): ancoram a cauda
       de scores naturais para o limiar nao recusar o que o
       rank acerta. Repetivel ate 8 arquivos. */
    char validacao[8][260];
    int n_validacao;
} CalibraConfig;

typedef struct {
    int n_pos;
    int n_neg;
    int n_val;             /* naturais carregados (--validacao) */
    int val_ok;            /* naturais com pagina correta no top-1 */
    double val_recall;     /* val_ok/n_val no ponto sugerido */
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
/* Fase 12.4b: carrega gold JSON p/ validacao (teste + reuso).
   Retorna 0 ok com *out malloc (liberar: free perguntas + free out). */
typedef struct {
    char *pergunta;
    int pagina;
} CalibraValQ;
int calibra_carregar_validacao(const char *path, CalibraValQ **out, int *n_out, char **erro);
void calibra_liberar_validacao(CalibraValQ *v, int n);

#endif
