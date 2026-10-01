#ifndef CONFIG_H
#define CONFIG_H

/* Fase 7.3: loader do subconjunto YAML usado em examples/config.yaml.
   Suporta `secao:` + `  chave: valor` (2 espacos), valores com/sem aspas,
   comentarios `#` e chaves desconhecidas (ignoradas). Sem dependencias. */

typedef struct {
    char input[1024];
    char output[1024];
    char title[256];
    char author[256];
    char lang[32];
    char templates_dir[1024];
    int chunk_words;
    int overlap;
    int max_choice;
    int max_score;
    int max_noul;
    float limiar_recusa;
    int tem_limiar;
    float conf_center;
    float conf_slope;
    int tem_input;
    int tem_chunk;
    int tem_qg;
} AmandaConfig;

void config_defaults(AmandaConfig *c);
int config_ler(const char *path, AmandaConfig *out, char **erro);

#endif
