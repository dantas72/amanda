#ifndef AMANDA_H
#define AMANDA_H

#include <stddef.h>
#include <stdint.h>

#define AMANDA_MAGIC "AMND"
#define AMANDA_MAGIC_LEN 4
#define AMANDA_FORMAT_VERSION 1u
#define AMANDA_EMBED_DIM 384

#define AMANDA_VERSION_DEFAULT "1.0.1"

const char *amanda_version(void);

typedef enum {
    AMANDA_OK = 0,
    AMANDA_ERR_IO = 1,
    AMANDA_ERR_MEM = 2,
    AMANDA_ERR_FORMAT = 3,
    AMANDA_ERR_ARGS = 4,
    AMANDA_ERR_NOTFOUND = 5,
    AMANDA_ERR_NET = 6
} AmandaStatus;

const char *amanda_strerror(AmandaStatus st);

#endif
