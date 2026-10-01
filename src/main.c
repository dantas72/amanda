#include <stdio.h>

#include "cli.h"

#ifndef _WIN32
#include <signal.h>
#endif

int main(int argc, char **argv) {
#ifndef _WIN32
    /* Linux: send() em socket fechado retorna EPIPE em vez de matar
       o processo com SIGPIPE (ex.: cliente que desconectou cedo). */
    signal(SIGPIPE, SIG_IGN);
#endif
    return cli_main(argc, argv);
}
