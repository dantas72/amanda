#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/time.h>
#endif

void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) { fprintf(stderr, "amandac: out of memory\n"); exit(3); }
    return p;
}

void *xcalloc(size_t n, size_t sz) {
    void *p = calloc(n ? n : 1, sz ? sz : 1);
    if (!p) { fprintf(stderr, "amandac: out of memory\n"); exit(3); }
    return p;
}

void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) { fprintf(stderr, "amandac: out of memory\n"); exit(3); }
    return q;
}

char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char *)xmalloc(n);
    memcpy(p, s, n);
    return p;
}

char *xstrndup(const char *s, size_t n) {
    char *p = (char *)xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

int read_file_bytes(const char *path, unsigned char **out, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) sz = 0;
    unsigned char *buf = (unsigned char *)xmalloc((size_t)sz + 1);
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = '\0';
    *out = buf;
    if (out_len) *out_len = got;
    return 0;
}

int write_file_bytes(const char *path, const unsigned char *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t w = fwrite(data, 1, len, f);
    fclose(f);
    return (w == len) ? 0 : -1;
}

char *read_file_text(const char *path) {
    unsigned char *b = NULL;
    size_t n = 0;
    if (read_file_bytes(path, &b, &n) != 0) return NULL;
    return (char *)b;
}

uint32_t fnv1a_32(const unsigned char *data, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

static uint32_t crc_table[256];
static int crc_table_ready = 0;

static void crc_init(void) {
    if (crc_table_ready) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc_table[i] = c;
    }
    crc_table_ready = 1;
}

uint32_t crc32_ieee(const unsigned char *data, size_t len) {
    crc_init();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++)
        c = crc_table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

void hash_hex32(uint32_t h, char out[9]) {
    static const char *hex = "0123456789abcdef";
    for (int i = 7; i >= 0; i--) {
        out[i] = hex[h & 0xF];
        h >>= 4;
    }
    out[8] = '\0';
}

char *str_trim(char *s) {
    if (!s) return s;
    while (*s && isspace((unsigned char)*s)) s++;
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
    return s;
}

int str_starts_with(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

int str_ends_with(const char *s, const char *suffix) {
    size_t a = strlen(s), b = strlen(suffix);
    if (b > a) return 0;
    return strcmp(s + a - b, suffix) == 0;
}

char *str_to_lower_dup(const char *s) {
    char *p = xstrdup(s);
    for (char *q = p; *q; q++) *q = (char)tolower((unsigned char)*q);
    return p;
}

void write_u32_le(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

void write_i32_le(unsigned char *p, int32_t v) {
    write_u32_le(p, (uint32_t)v);
}

void write_f32_le(unsigned char *p, float v) {
    uint32_t u;
    memcpy(&u, &v, sizeof u);
    write_u32_le(p, u);
}

uint32_t read_u32_le(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int32_t read_i32_le(const unsigned char *p) {
    return (int32_t)read_u32_le(p);
}

float read_f32_le(const unsigned char *p) {
    uint32_t u = read_u32_le(p);
    float f;
    memcpy(&f, &u, sizeof f);
    return f;
}

void buf_init(ByteBuf *b) {
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

void buf_free(ByteBuf *b) {
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

int buf_reserve(ByteBuf *b, size_t extra) {
    if (b->len + extra <= b->cap) return 0;
    size_t ncap = b->cap ? b->cap * 2 : 1024;
    while (ncap < b->len + extra) ncap *= 2;
    b->data = (unsigned char *)xrealloc(b->data, ncap);
    b->cap = ncap;
    return 0;
}

int buf_append(ByteBuf *b, const void *src, size_t n) {
    if (n == 0) return 0;
    buf_reserve(b, n);
    memcpy(b->data + b->len, src, n);
    b->len += n;
    return 0;
}

int buf_append_cstr(ByteBuf *b, const char *s) {
    return buf_append(b, s, s ? strlen(s) : 0);
}

int buf_append_u32(ByteBuf *b, uint32_t v) {
    unsigned char tmp[4];
    write_u32_le(tmp, v);
    return buf_append(b, tmp, 4);
}

int buf_append_i32(ByteBuf *b, int32_t v) {
    unsigned char tmp[4];
    write_i32_le(tmp, v);
    return buf_append(b, tmp, 4);
}

int buf_append_f32(ByteBuf *b, float v) {
    unsigned char tmp[4];
    write_f32_le(tmp, v);
    return buf_append(b, tmp, 4);
}

int buf_append_str(ByteBuf *b, const char *s) {
    uint32_t n = s ? (uint32_t)strlen(s) : 0;
    buf_append_u32(b, n);
    if (n) buf_append(b, s, n);
    return 0;
}

void reader_init(ByteReader *r, const unsigned char *data, size_t len) {
    r->data = data;
    r->len = len;
    r->pos = 0;
    r->err = 0;
}

uint32_t reader_u32(ByteReader *r) {
    if (r->err || r->pos + 4 > r->len) { r->err = 1; return 0; }
    uint32_t v = read_u32_le(r->data + r->pos);
    r->pos += 4;
    return v;
}

int32_t reader_i32(ByteReader *r) {
    return (int32_t)reader_u32(r);
}

float reader_f32(ByteReader *r) {
    if (r->err || r->pos + 4 > r->len) { r->err = 1; return 0; }
    float v = read_f32_le(r->data + r->pos);
    r->pos += 4;
    return v;
}

char *reader_str(ByteReader *r) {
    uint32_t n = reader_u32(r);
    if (r->err) return NULL;
    if (n > 16 * 1024 * 1024) { r->err = 1; return NULL; }
    if (r->pos + n > r->len) { r->err = 1; return NULL; }
    char *s = xstrndup((const char *)(r->data + r->pos), n);
    r->pos += n;
    return s;
}

void reader_bytes(ByteReader *r, void *dst, size_t n) {
    if (r->err || r->pos + n > r->len) { r->err = 1; return; }
    memcpy(dst, r->data + r->pos, n);
    r->pos += n;
}

char *json_escape(const char *s) {
    if (!s) return xstrdup("");
    ByteBuf b;
    buf_init(&b);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
        case '"': buf_append(&b, "\\\"", 2); break;
        case '\\': buf_append(&b, "\\\\", 2); break;
        case '\n': buf_append(&b, "\\n", 2); break;
        case '\r': buf_append(&b, "\\r", 2); break;
        case '\t': buf_append(&b, "\\t", 2); break;
        default:
            if (*p < 0x20) {
                char tmp[7];
                snprintf(tmp, sizeof tmp, "\\u%04x", *p);
                buf_append(&b, tmp, 6);
            } else {
                buf_append(&b, p, 1);
            }
            break;
        }
    }
    buf_reserve(&b, 1);
    b.data[b.len] = '\0';
    return (char *)b.data;
}

long long now_ms(void) {
#ifdef _WIN32
    return (long long)GetTickCount64();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
}

void sleep_ms(int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep((useconds_t)ms * 1000);
#endif
}
