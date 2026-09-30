#ifndef SERVER_H
#define SERVER_H

#include "packager.h"

typedef struct {
    const char *host;
    int port;
    AmandaPackage *pkg;
    volatile int *stop_flag;
} ServerConfig;

int server_run(const ServerConfig *cfg);

#endif
