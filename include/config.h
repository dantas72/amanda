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
    /* Fase 10: secao extracao (limiares TJ; flag CLI prevalece) */
    float tj_espaco;
    float tj_salto;
    int tem_extracao;
    /* Fase 13: secao servidor (serve --config; flag CLI prevalece) */
    char srv_host[256];
    int srv_port;
    char srv_cors[256];
    char srv_api_key[512];
    char srv_api_key_file[1024];
    long srv_max_body;
    int srv_max_conns;
    int srv_workers;
    int srv_eval_max;
    char srv_backend[32];
    char srv_laya_url[256];
    int srv_laya_timeout_ms;
    int srv_laya_max;
    int srv_laya_queue;
    int srv_laya_queue_ms;
    char srv_pacote[1024];
    int tem_servidor;
} AmandaConfig;

void config_defaults(AmandaConfig *c);
int config_ler(const char *path, AmandaConfig *out, char **erro);

#endif
