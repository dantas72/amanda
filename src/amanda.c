#include "amanda.h"
#include <stdio.h>
#include <string.h>

#if defined(__has_include)
#if __has_include("version_gen.h")
#include "version_gen.h"
#endif
#elif defined(_WIN32)
#include "version_gen.h"
#endif

#ifndef AMANDA_VERSION_GEN
#define AMANDA_VERSION_GEN AMANDA_VERSION_DEFAULT
#endif

const char *amanda_version(void) {
    static char ver[64] = {0};
    static int loaded = 0;
    if (loaded) return ver;
    loaded = 1;
    FILE *f = fopen("version.bin", "rb");
    if (f) {
        size_t n = fread(ver, 1, sizeof(ver) - 1, f);
        fclose(f);
        ver[n] = '\0';
        size_t len = strlen(ver);
        while (len > 0 && (ver[len-1] == '\n' || ver[len-1] == '\r' ||
                           ver[len-1] == ' ' || ver[len-1] == '\t'))
            ver[--len] = '\0';
        size_t start = 0;
        while (ver[start] == ' ' || ver[start] == '\t') start++;
        if (start > 0) memmove(ver, ver + start, strlen(ver + start) + 1);
        if (ver[0] != '\0') return ver;
    }
    strncpy(ver, AMANDA_VERSION_GEN, sizeof(ver) - 1);
    return ver;
}

const char *amanda_strerror(AmandaStatus st) {
    switch (st) {
    case AMANDA_OK: return "ok";
    case AMANDA_ERR_IO: return "erro de I/O";
    case AMANDA_ERR_MEM: return "sem memoria";
    case AMANDA_ERR_FORMAT: return "formato invalido";
    case AMANDA_ERR_ARGS: return "argumentos invalidos";
    case AMANDA_ERR_NOTFOUND: return "nao encontrado";
    case AMANDA_ERR_NET: return "erro de rede";
    default: return "erro desconhecido";
    }
}
