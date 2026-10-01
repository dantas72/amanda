#ifndef SERVER_H
#define SERVER_H

#include "packager.h"

typedef struct {
    const char *host;
    int port;
    AmandaPackage *pkg;
    volatile int *stop_flag;
    /* Fase 7.2: calibracao (zeros = padrao do motor).
       Backend segue sempre local no serve: com servidor single-thread,
       uma inferencia LLM por request (ate 120s) travaria o servico.
       Reavaliar na Fase 7.5 (threads). */
    float conf_center;
    float conf_slope;
    float limiar_recusa;
    int tem_limiar;
} ServerConfig;

int server_run(const ServerConfig *cfg);

#endif
