#ifndef AMANDA_UTILS_H
#define AMANDA_UTILS_H

#include <stddef.h>
#include <stdint.h>

void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t sz);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);

int read_file_bytes(const char *path, unsigned char **out, size_t *out_len);
int write_file_bytes(const char *path, const unsigned char *data, size_t len);
char *read_file_text(const char *path);

uint32_t fnv1a_32(const unsigned char *data, size_t len);
uint32_t crc32_ieee(const unsigned char *data, size_t len);
void hash_hex32(uint32_t h, char out[9]);

char *str_trim(char *s);
int str_starts_with(const char *s, const char *prefix);
int str_ends_with(const char *s, const char *suffix);
char *str_to_lower_dup(const char *s);

void write_u32_le(unsigned char *p, uint32_t v);
void write_i32_le(unsigned char *p, int32_t v);
void write_f32_le(unsigned char *p, float v);
uint32_t read_u32_le(const unsigned char *p);
int32_t read_i32_le(const unsigned char *p);
float read_f32_le(const unsigned char *p);

typedef struct {
    unsigned char *data;
    size_t len;
    size_t cap;
} ByteBuf;

void buf_init(ByteBuf *b);
void buf_free(ByteBuf *b);
int buf_reserve(ByteBuf *b, size_t extra);
int buf_append(ByteBuf *b, const void *src, size_t n);
int buf_append_cstr(ByteBuf *b, const char *s);
int buf_append_u32(ByteBuf *b, uint32_t v);
int buf_append_i32(ByteBuf *b, int32_t v);
int buf_append_f32(ByteBuf *b, float v);
int buf_append_str(ByteBuf *b, const char *s);

typedef struct {
    const unsigned char *data;
    size_t len;
    size_t pos;
    int err;
} ByteReader;

void reader_init(ByteReader *r, const unsigned char *data, size_t len);
uint32_t reader_u32(ByteReader *r);
int32_t reader_i32(ByteReader *r);
float reader_f32(ByteReader *r);
char *reader_str(ByteReader *r);
void reader_bytes(ByteReader *r, void *dst, size_t n);

char *json_escape(const char *s);

long long now_ms(void);
void sleep_ms(int ms);

#endif
