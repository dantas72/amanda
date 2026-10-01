#ifndef SERVER_H
#define SERVER_H

#include "packager.h"

typedef struct {
    const char *host;
    int port;
    AmandaPackage *pkg;
    volatile int *stop_flag;
    /* Fase 7.2: calibracao (zeros = padrao do motor). */
    float conf_center;
    float conf_slope;
    float limiar_recusa;
    int tem_limiar;
    /* Fase 7.5: robustez (defaults aplicados quando zero/nulo).
       Backend segue local por request; com threads, varias decisoes
       locais concorrem sem travar o servico (sem estado global). */
    const char *cors_origin;  /* default "*" */
    const char *api_key;      /* NULL/vazio = aberto; senao exige Bearer */
    long max_body;            /* bytes; default 1048576; > responde 413 */
    int max_conns;            /* conexoes simultaneas; default 16; cheio = 503 */
    int eval_max;             /* teto de amostradas no POST /v1/eval; default 200 */
} ServerConfig;

int server_run(const ServerConfig *cfg);

#endif
