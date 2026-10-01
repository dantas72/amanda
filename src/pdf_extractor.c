#include "pdf_extractor.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ============ infra DocumentoExtraido ============ */

static DocumentoExtraido *doc_novo(void) {
    DocumentoExtraido *d = (DocumentoExtraido *)xcalloc(1, sizeof(*d));
    return d;
}

static void doc_add_bloco(DocumentoExtraido *d, const char *texto, int pagina) {
    if (!texto || !texto[0]) return;
    d->blocos = (BlocoTexto *)xrealloc(d->blocos, sizeof(BlocoTexto) * (size_t)(d->num_blocos + 1));
    BlocoTexto *b = &d->blocos[d->num_blocos++];
    b->texto = xstrdup(texto);
    b->pagina = pagina;
    b->x = b->y = b->largura = b->altura = 0;
}

void liberar_documento(DocumentoExtraido *doc) {
    if (!doc) return;
    for (int i = 0; i < doc->num_blocos; i++) free(doc->blocos[i].texto);
    free(doc->blocos);
    free(doc->titulo);
    free(doc);
}

char *documento_texto_completo(const DocumentoExtraido *doc) {
    ByteBuf b;
    buf_init(&b);
    for (int i = 0; i < doc->num_blocos; i++) {
        buf_append(&b, doc->blocos[i].texto, strlen(doc->blocos[i].texto));
        buf_append(&b, "\n\n", 2);
    }
    buf_reserve(&b, 1);
    b.data[b.len] = '\0';
    return (char *)b.data;
}

/* ============ TXT / CSV / JSON ============ */

DocumentoExtraido *extrair_txt(const char *caminho, char **erro) {
    char *txt = read_file_text(caminho);
    if (!txt) { if (erro) *erro = xstrdup("nao foi possivel ler arquivo txt"); return NULL; }
    DocumentoExtraido *d = doc_novo();
    d->titulo = xstrdup(caminho);
    /* divide por linhas em branco; pagina a cada 50 linhas */
    ByteBuf cur;
    buf_init(&cur);
    int linha = 0, pagina = 1;
    for (char *p = txt; ; p++) {
        int fim = (*p == '\0');
        int nl = (*p == '\n');
        if (!fim) {
            buf_append(&cur, p, 1);
            if (nl) {
                linha++;
                if (linha % 50 == 0) pagina++;
            }
        }
        if (fim || (nl && (p[1] == '\n' || p[1] == '\r' || p[1] == '\0'))) {
            buf_reserve(&cur, 1);
            cur.data[cur.len] = '\0';
            char *t = str_trim((char *)cur.data);
            if (strlen(t) > 0) doc_add_bloco(d, t, pagina);
            cur.len = 0;
        }
        if (fim) break;
    }
    if (d->num_blocos == 0 && strlen(txt) > 0) doc_add_bloco(d, txt, 1);
    d->num_paginas = pagina;
    buf_free(&cur);
    free(txt);
    return d;
}

DocumentoExtraido *extrair_csv(const char *caminho, char **erro) {
    char *txt = read_file_text(caminho);
    if (!txt) { if (erro) *erro = xstrdup("nao foi possivel ler arquivo csv"); return NULL; }
    DocumentoExtraido *d = doc_novo();
    d->titulo = xstrdup(caminho);
    int pagina = 1, linha = 0;
    char *p = txt;
    while (*p) {
        char *eol = strchr(p, '\n');
        size_t ll = eol ? (size_t)(eol - p) : strlen(p);
        char *line = xstrndup(p, ll);
        char *t = str_trim(line);
        /* remove \r final */
        size_t tl = strlen(t);
        while (tl > 0 && (t[tl-1] == '\r')) t[--tl] = '\0';
        if (strlen(t) > 0) {
            /* troca ; e , por espaco duplo para leitura, preserva conteudo */
            ByteBuf b; buf_init(&b);
            int inq = 0;
            for (char *q = t; *q; q++) {
                if (*q == '"') inq = !inq;
                else if (!inq && (*q == ';' || *q == ',' || *q == '\t')) buf_append(&b, " | ", 3);
                else buf_append(&b, q, 1);
            }
            buf_reserve(&b, 1); b.data[b.len] = '\0';
            doc_add_bloco(d, (char *)b.data, pagina);
            buf_free(&b);
        }
        free(line);
        linha++;
        if (linha % 60 == 0) pagina++;
        if (!eol) break;
        p = eol + 1;
    }
    d->num_paginas = pagina;
    free(txt);
    return d;
}

DocumentoExtraido *extrair_json(const char *caminho, char **erro) {
    unsigned char *raw = NULL;
    size_t n = 0;
    if (read_file_bytes(caminho, &raw, &n) != 0) {
        if (erro) *erro = xstrdup("nao foi possivel ler arquivo json");
        return NULL;
    }
    DocumentoExtraido *d = doc_novo();
    d->titulo = xstrdup(caminho);
    ByteBuf cur; buf_init(&cur);
    int pagina = 1, nblocos = 0;
    for (size_t i = 0; i < n; i++) {
        if (raw[i] == '"') {
            ByteBuf s; buf_init(&s);
            i++;
            while (i < n && raw[i] != '"') {
                if (raw[i] == '\\' && i + 1 < n) {
                    char e = (char)raw[i+1];
                    if (e == 'n') buf_append(&s, "\n", 1);
                    else if (e == 't') buf_append(&s, "\t", 1);
                    else buf_append(&s, &e, 1);
                    i += 2;
                } else {
                    buf_append(&s, &raw[i], 1);
                    i++;
                }
            }
            buf_reserve(&s, 1); s.data[s.len] = '\0';
            /* ignora chaves curtas provaveis (sem espaco e < 32 chars) */
            if (s.len >= 3 && (strchr((char*)s.data, ' ') || s.len >= 24)) {
                doc_add_bloco(d, (char *)s.data, pagina);
                nblocos++;
                if (nblocos % 20 == 0) pagina++;
            }
            buf_free(&s);
        }
    }
    if (d->num_blocos == 0) {
        /* fallback: arquivo inteiro como texto */
        raw[n] = '\0';
        doc_add_bloco(d, (char *)raw, 1);
    }
    d->num_paginas = pagina;
    free(raw);
    buf_free(&cur);
    if (d->num_blocos == 0) {
        if (erro) *erro = xstrdup("json sem conteudo textual");
        liberar_documento(d);
        return NULL;
    }
    return d;
}

/* ============ PDF: inflate (zlib/DEFLATE) minimo e robusto ============ */

typedef struct {
    const unsigned char *data;
    size_t len;
    size_t bytepos;
    unsigned bitbuf;
    int bitcnt;
    int err;
} BitReader;

static void br_init(BitReader *br, const unsigned char *data, size_t len) {
    br->data = data; br->len = len; br->bytepos = 0;
    br->bitbuf = 0; br->bitcnt = 0; br->err = 0;
}

static unsigned br_bits(BitReader *br, int n) {
    while (br->bitcnt < n) {
        if (br->bytepos >= br->len) { br->err = 1; return 0; }
        br->bitbuf |= ((unsigned)br->data[br->bytepos++]) << br->bitcnt;
        br->bitcnt += 8;
    }
    unsigned v = br->bitbuf & ((n == 32) ? 0xFFFFFFFFu : ((1u << n) - 1));
    br->bitbuf >>= n;
    br->bitcnt -= n;
    return v;
}

/* Implementacao inflate direta com tabelas canonicas sem abstracao pesada */
typedef struct {
    int code;
    int len;
    int sym;
} HuffEnt;

static int build_huff(const int *lens, int n, HuffEnt *out, int *out_n, int maxbits) {
    int bl_count[16] = {0};
    for (int i = 0; i < n; i++) if (lens[i] <= 15 && lens[i] >= 0) bl_count[lens[i]]++;
    bl_count[0] = 0; /* RFC 1951: comprimentos zero nao participam do codigo */
    int code = 0;
    int next_code[16] = {0};
    for (int b = 1; b <= 15; b++) {
        code = (code + bl_count[b-1]) << 1;
        next_code[b] = code;
    }
    int p = 0;
    for (int i = 0; i < n; i++) {
        int len = lens[i];
        if (len) {
            out[p].code = next_code[len]++;
            out[p].len = len;
            out[p].sym = i;
            p++;
        }
    }
    *out_n = p;
    (void)maxbits;
    return 0;
}

static int huff_decode(BitReader *br, HuffEnt *t, int tn) {
    int code = 0;
    for (int len = 1; len <= 15; len++) {
        code = (code << 1) | (int)br_bits(br, 1);
        if (br->err) return -1;
        for (int i = 0; i < tn; i++) {
            if (t[i].len == len && t[i].code == code) return t[i].sym;
        }
    }
    return -1;
}

static int inflate_raw(const unsigned char *in, size_t ilen,
                       unsigned char **out, size_t *olen) {
    BitReader br;
    br_init(&br, in, ilen);
    ByteBuf out_b; buf_init(&out_b);
    static const int lbase[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const int lext[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const int dbase[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static const int dext[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

    int done = 0;
    while (!done) {
        int bfinal = (int)br_bits(&br, 1);
        int btype = (int)br_bits(&br, 2);
        if (br.err) { buf_free(&out_b); return -1; }
        if (btype == 0) {
            br.bitbuf = 0; br.bitcnt = 0;
            if (br.bytepos + 4 > br.len) { buf_free(&out_b); return -1; }
            unsigned len = br.data[br.bytepos] | ((unsigned)br.data[br.bytepos+1] << 8);
            unsigned nlen = br.data[br.bytepos+2] | ((unsigned)br.data[br.bytepos+3] << 8);
            br.bytepos += 4;
            if ((len ^ nlen) != 0xFFFF) { buf_free(&out_b); return -1; }
            if (br.bytepos + len > br.len) { buf_free(&out_b); return -1; }
            buf_append(&out_b, br.data + br.bytepos, len);
            br.bytepos += len;
        } else if (btype == 1 || btype == 2) {
            HuffEnt lit_tab[288], dist_tab[32];
            int lit_n = 0, dist_n = 0;
            HuffEnt *lit = lit_tab, *dist = dist_tab;
            HuffEnt dyn_lit[288], dyn_dist[32];
            if (btype == 1) {
                int lens[288];
                for (int i = 0; i < 144; i++) lens[i] = 8;
                for (int i = 144; i < 256; i++) lens[i] = 9;
                for (int i = 256; i < 280; i++) lens[i] = 7;
                for (int i = 280; i < 288; i++) lens[i] = 8;
                build_huff(lens, 288, lit_tab, &lit_n, 9);
                int dl[32];
                for (int i = 0; i < 32; i++) dl[i] = 5;
                build_huff(dl, 32, dist_tab, &dist_n, 5);
            } else {
                int hlit = (int)br_bits(&br, 5) + 257;
                int hdist = (int)br_bits(&br, 5) + 1;
                int hclen = (int)br_bits(&br, 4) + 4;
                if (br.err) { buf_free(&out_b); return -1; }
                static const int order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
                int cl_lens[19] = {0};
                for (int i = 0; i < hclen; i++) cl_lens[order[i]] = (int)br_bits(&br, 3);
                HuffEnt cl_tab[19]; int cl_n = 0;
                build_huff(cl_lens, 19, cl_tab, &cl_n, 7);
                int total = hlit + hdist;
                int *lens = (int *)xcalloc((size_t)total, sizeof(int));
                int li = 0, prev = 0;
                while (li < total) {
                    int s = huff_decode(&br, cl_tab, cl_n);
                    if (s < 0) { free(lens); buf_free(&out_b); return -1; }
                    if (s <= 15) { lens[li++] = s; prev = s; }
                    else if (s == 16) {
                        int r = (int)br_bits(&br, 2) + 3;
                        for (int k = 0; k < r && li < total; k++) lens[li++] = prev;
                    } else if (s == 17) {
                        int r = (int)br_bits(&br, 3) + 3;
                        for (int k = 0; k < r && li < total; k++) lens[li++] = 0;
                        prev = 0;
                    } else {
                        int r = (int)br_bits(&br, 7) + 11;
                        for (int k = 0; k < r && li < total; k++) lens[li++] = 0;
                        prev = 0;
                    }
                }
                build_huff(lens, hlit, dyn_lit, &lit_n, 15);
                build_huff(lens + hlit, hdist, dyn_dist, &dist_n, 15);
                free(lens);
                lit = dyn_lit; dist = dyn_dist;
            }
            for (;;) {
                int s = huff_decode(&br, lit, lit_n);
                if (s < 0 || br.err) { buf_free(&out_b); return -1; }
                if (s < 256) {
                    unsigned char c = (unsigned char)s;
                    buf_append(&out_b, &c, 1);
                } else if (s == 256) {
                    break;
                } else {
                    int li = s - 257;
                    if (li < 0 || li > 28) { buf_free(&out_b); return -1; }
                    int length = lbase[li] + (lext[li] ? (int)br_bits(&br, lext[li]) : 0);
                    int ds = huff_decode(&br, dist, dist_n);
                    if (ds < 0 || ds > 29 || br.err) { buf_free(&out_b); return -1; }
                    int distance = dbase[ds] + (dext[ds] ? (int)br_bits(&br, dext[ds]) : 0);
                    if (distance <= 0 || (size_t)distance > out_b.len) { buf_free(&out_b); return -1; }
                    size_t pos = out_b.len - (size_t)distance;
                    for (int k = 0; k < length; k++) {
                        unsigned char c = out_b.data[pos + k];
                        buf_append(&out_b, &c, 1);
                    }
                }
            }
        } else {
            buf_free(&out_b); return -1;
        }
        if (bfinal) done = 1;
    }
    if (br.err) { buf_free(&out_b); return -1; }
    *out = out_b.data;
    *olen = out_b.len;
    return 0;
}

static int zlib_inflate(const unsigned char *in, size_t ilen,
                        unsigned char **out, size_t *olen) {
    if (ilen < 2) return -1;
    /* cabecalho zlib: CMF FLG */
    if ((in[0] & 0x0F) != 8) return -1;
    if ((((unsigned)in[0] << 8 | in[1]) % 31) != 0) return -1;
    return inflate_raw(in + 2, ilen - 2, out, olen);
}

/* ============ PDF text scan (Fase 7.4: operadores + filtros) ============ */

/* ASCIIHexDecode: ignora whitespace, '>' termina; nibble solitario = *16. */
static int pdf_ahx_decode(const unsigned char *in, size_t ilen,
                          unsigned char **out, size_t *olen) {
    ByteBuf b; buf_init(&b);
    int hi = -1;
    for (size_t i = 0; i < ilen; i++) {
        unsigned char c = in[i];
        if (c == '>') break;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f') continue;
        int v = -1;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else { buf_free(&b); return -1; }
        if (hi < 0) hi = v;
        else { unsigned char o = (unsigned char)((hi << 4) | v); buf_append(&b, &o, 1); hi = -1; }
    }
    if (hi >= 0) { unsigned char o = (unsigned char)(hi << 4); buf_append(&b, &o, 1); }
    *out = b.data; *olen = b.len;
    return 0;
}

/* ASCII85Decode: 'z' = 4 zeros, '~>' = fim, whitespace ignorado. */
static int pdf_a85_decode(const unsigned char *in, size_t ilen,
                          unsigned char **out, size_t *olen) {
    ByteBuf b; buf_init(&b);
    unsigned int tup = 0;
    int cnt = 0;
    for (size_t i = 0; i < ilen; i++) {
        unsigned char c = in[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f') continue;
        if (c == 'z') {
            if (cnt != 0) { buf_free(&b); return -1; }
            unsigned char z[4] = {0, 0, 0, 0};
            buf_append(&b, z, 4);
            continue;
        }
        if (c == '~') break; /* espera '>' em seguida */
        if (c < '!' || c > 'u') { buf_free(&b); return -1; }
        tup = tup * 85u + (unsigned int)(c - '!');
        cnt++;
        if (cnt == 5) {
            unsigned char o[4];
            o[0] = (unsigned char)(tup >> 24); o[1] = (unsigned char)(tup >> 16);
            o[2] = (unsigned char)(tup >> 8); o[3] = (unsigned char)tup;
            buf_append(&b, o, 4);
            tup = 0; cnt = 0;
        }
    }
    if (cnt > 0) {
        if (cnt == 1) { buf_free(&b); return -1; }
        for (int k = cnt; k < 5; k++) tup = tup * 85u + 84u;
        int nbytes = cnt - 1;
        unsigned char o[4];
        o[0] = (unsigned char)(tup >> 24); o[1] = (unsigned char)(tup >> 16);
        o[2] = (unsigned char)(tup >> 8); o[3] = (unsigned char)tup;
        buf_append(&b, o, (size_t)nbytes);
    }
    *out = b.data; *olen = b.len;
    return 0;
}

/* Converte UTF-16BE (com BOM FE FF) para UTF-8 aproximado. */
static void pdf_append_utf16be_as_utf8(ByteBuf *dst, const unsigned char *s, size_t n) {
    for (size_t i = 0; i + 1 < n; i += 2) {
        unsigned int u = ((unsigned int)s[i] << 8) | s[i + 1];
        if (u == 0) { buf_append(dst, " ", 1); continue; }
        if (u < 0x80) { char c = (char)u; buf_append(dst, &c, 1); }
        else if (u < 0x800) {
            char o[2] = {(char)(0xC0 | (u >> 6)), (char)(0x80 | (u & 0x3F))};
            buf_append(dst, o, 2);
        } else {
            char o[3] = {(char)(0xE0 | (u >> 12)), (char)(0x80 | ((u >> 6) & 0x3F)), (char)(0x80 | (u & 0x3F))};
            buf_append(dst, o, 3);
        }
    }
}

/* WinAnsi (CP1252) -> UTF-8. Bytes 0x00-0x7F identicos; 0xA0-0xFF = U+00A0-U+00FF. */
static unsigned int pdf_cp1252(unsigned char c) {
    static const unsigned int tab[32] = {
        0x20AC, 0x003F, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x003F, 0x017D, 0x003F,
        0x003F, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x003F, 0x017E, 0x0178
    };
    if (c < 0x80) return c;
    if (c < 0xA0) return tab[c - 0x80];
    return (unsigned int)c;
}

static void pdf_emit_token(ByteBuf *out, const unsigned char *s, size_t n) {
    if (n >= 2 && s[0] == 0xFE && s[1] == 0xFF) {
        pdf_append_utf16be_as_utf8(out, s + 2, n - 2);
    } else {
        /* filtra lixo binario: exige maioria imprimivel */
        int print = 0;
        for (size_t k = 0; k < n; k++)
            if (s[k] == '\n' || s[k] == '\t' || (s[k] >= 32 && s[k] < 127) || s[k] >= 128) print++;
        if (n < 1 || print * 2 < (int)n) return;
        /* converte WinAnsi para UTF-8 (acentos, travessao, etc.) */
        for (size_t k = 0; k < n; k++) {
            unsigned int u = pdf_cp1252(s[k]);
            if (u < 0x80) { char c = (char)u; buf_append(out, &c, 1); }
            else if (u < 0x800) {
                char o[2] = {(char)(0xC0 | (u >> 6)), (char)(0x80 | (u & 0x3F))};
                buf_append(out, o, 2);
            } else {
                char o[3] = {(char)(0xE0 | (u >> 12)), (char)(0x80 | ((u >> 6) & 0x3F)), (char)(0x80 | (u & 0x3F))};
                buf_append(out, o, 3);
            }
        }
    }
}

/* Separa blocos de texto com espaco ou quebra de linha. */
static void pdf_sep(ByteBuf *out, int newline) {
    if (out->len == 0) return;
    char last = (char)out->data[out->len - 1];
    if (newline) {
        if (last != '\n') buf_append(out, "\n", 1);
    } else {
        if (last != ' ' && last != '\n' && last != '\t') buf_append(out, " ", 1);
    }
}

/* Decodifica string literal PDF a partir de d[*pi] == '('.
   Retorna buffer malloc (ByteBuf derivado) e avanca *pi apos ')'. */
static int pdf_parse_literal(const unsigned char *d, size_t n, size_t *pi,
                             unsigned char **tok, size_t *toklen) {
    size_t i = *pi;
    if (i >= n || d[i] != '(') return -1;
    i++;
    int depth = 1;
    ByteBuf s; buf_init(&s);
    while (i < n && depth > 0) {
        if (d[i] == '\\' && i + 1 < n) {
            char e = (char)d[i + 1];
            if (e == 'n') { buf_append(&s, "\n", 1); i += 2; }
            else if (e == 'r') { buf_append(&s, "\n", 1); i += 2; }
            else if (e == 't') { buf_append(&s, "\t", 1); i += 2; }
            else if (e == '\r') { i += 2; if (i < n && d[i] == '\n') i++; buf_append(&s, "\n", 1); }
            else if (e == '\n') { i += 2; }
            else if (e >= '0' && e <= '7') {
                int v = 0, k = 0;
                while (k < 3 && i + 1 + (size_t)k < n && d[i+1+k] >= '0' && d[i+1+k] <= '7') {
                    v = v * 8 + (d[i+1+k] - '0'); k++;
                }
                char c = (char)v;
                buf_append(&s, &c, 1);
                i += (size_t)k + 1;
            } else { buf_append(&s, &e, 1); i += 2; }
        } else if (d[i] == '(') { depth++; buf_append(&s, "(", 1); i++; }
        else if (d[i] == ')') { depth--; i++; if (depth != 0) buf_append(&s, ")", 1); }
        else { buf_append(&s, &d[i], 1); i++; }
    }
    if (depth != 0) { buf_free(&s); return -1; }
    *pi = i;
    *tok = s.data; *toklen = s.len;
    return 0;
}

/* Decodifica string hexadecimal <...> a partir de d[*pi] == '<' (nao '<<'). */
static int pdf_parse_hexstr(const unsigned char *d, size_t n, size_t *pi,
                            unsigned char **tok, size_t *toklen) {
    size_t i = *pi;
    if (i >= n || d[i] != '<') return -1;
    i++;
    ByteBuf s; buf_init(&s);
    char hex[3] = {0, 0, 0};
    int hi = 0, have = 0;
    while (i < n && d[i] != '>') {
        if (isspace(d[i])) { i++; continue; }
        if (isxdigit(d[i])) {
            hex[hi++] = (char)d[i];
            if (hi == 2) {
                char c = (char)strtol(hex, NULL, 16);
                buf_append(&s, &c, 1);
                hi = 0; have = 1;
            }
        } else { buf_free(&s); return -1; }
        i++;
    }
    if (i >= n) { buf_free(&s); return -1; }
    i++; /* consome '>' */
    if (hi == 1) { hex[1] = '0'; char c = (char)strtol(hex, NULL, 16); buf_append(&s, &c, 1); have = 1; }
    (void)have;
    *pi = i;
    *tok = s.data; *toklen = s.len;
    return 0;
}

/* Extrai texto respeitando operadores Tj / TJ / ' / " e quebras Td / TD / Tm / T*.
   Strings fora desses operadores sao ignoradas (metadados), o que reduz lixo. */
static void extract_text_from_content(const unsigned char *d, size_t n, ByteBuf *out) {
    size_t i = 0;
    unsigned char *pend = NULL;
    size_t pendlen = 0;
    int tem_pend = 0;
    /* array TJ pendente: lista de strings + espacos */
    typedef struct { unsigned char *s; size_t len; int is_space; } TJItem;
    TJItem *arr = NULL;
    size_t arr_n = 0, arr_cap = 0;
    int em_array = 0;

    /* liberta pendente sem emitir */
    /* percorre tokens */
    while (i < n) {
        unsigned char c = d[i];
        if (c == '%') { while (i < n && d[i] != '\n' && d[i] != '\r') i++; continue; }
        if (isspace(c)) { i++; continue; }
        if (c == '(') {
            unsigned char *t = NULL; size_t tl = 0;
            size_t j = i;
            if (pdf_parse_literal(d, n, &j, &t, &tl) == 0) {
                i = j;
                if (em_array) {
                    if (arr_n == arr_cap) {
                        arr_cap = arr_cap ? arr_cap * 2 : 8;
                        arr = (TJItem *)xrealloc(arr, arr_cap * sizeof(*arr));
                    }
                    arr[arr_n].s = t; arr[arr_n].len = tl; arr[arr_n].is_space = 0;
                    arr_n++;
                } else {
                    free(pend); pend = t; pendlen = tl; tem_pend = 1;
                }
                continue;
            }
            i++;
            continue;
        }
        if (c == '<' && !(i + 1 < n && d[i + 1] == '<')) {
            unsigned char *t = NULL; size_t tl = 0;
            size_t j = i;
            if (pdf_parse_hexstr(d, n, &j, &t, &tl) == 0) {
                i = j;
                if (em_array) {
                    if (arr_n == arr_cap) {
                        arr_cap = arr_cap ? arr_cap * 2 : 8;
                        arr = (TJItem *)xrealloc(arr, arr_cap * sizeof(*arr));
                    }
                    arr[arr_n].s = t; arr[arr_n].len = tl; arr[arr_n].is_space = 0;
                    arr_n++;
                } else {
                    free(pend); pend = t; pendlen = tl; tem_pend = 1;
                }
                continue;
            }
            i++;
            continue;
        }
        if (c == '[') { em_array = 1; i++; continue; }
        if (c == ']') {
            em_array = 0; i++;
            /* fecha array sem operador TJ: descarta (metadado) */
            if (arr_n > 0) {
                /* mantem para o caso de "TJ" vir em seguida; marca posicao */
            }
            continue;
        }
        if (c == '/' ) {
            /* nome: /XXXX — consome e descarta pendente solto */
            i++;
            while (i < n && !isspace(d[i]) && d[i] != '/' && d[i] != '(' && d[i] != '<' &&
                   d[i] != '[' && d[i] != ']' && d[i] != '%' && d[i] != '(' ) {
                if (strchr("()<>[]{}/%", (char)d[i])) break;
                i++;
            }
            continue;
        }
        if (c == '\'' || c == '"') {
            /* ' e " mostram a string pendente com quebra */
            if (tem_pend && pend) { pdf_sep(out, 0); pdf_emit_token(out, pend, pendlen); }
            pdf_sep(out, 1);
            free(pend); pend = NULL; pendlen = 0; tem_pend = 0;
            for (size_t k = 0; k < arr_n; k++) free(arr[k].s);
            arr_n = 0;
            i++;
            continue;
        }
        /* numero (possivel espacamento dentro de TJ) */
        if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.') {
            size_t j = i;
            if (c == '-' || c == '+') j++;
            while (j < n && ((d[j] >= '0' && d[j] <= '9') || d[j] == '.')) j++;
            if (em_array) {
                double v = 0;
                { char nb[64]; size_t nl = j - i < sizeof(nb) - 1 ? j - i : sizeof(nb) - 1;
                  memcpy(nb, d + i, nl); nb[nl] = '\0'; v = atof(nb); }
                /* TJ: numero muito negativo = espaco entre palavras;
                   positivo muito grande = salto de coluna/linha */
                if (v < -100.0 || v > 500.0) {
                    if (arr_n == arr_cap) {
                        arr_cap = arr_cap ? arr_cap * 2 : 8;
                        arr = (TJItem *)xrealloc(arr, arr_cap * sizeof(*arr));
                    }
                    arr[arr_n].s = NULL; arr[arr_n].len = 0; arr[arr_n].is_space = 1;
                    arr_n++;
                }
            }
            i = j;
            continue;
        }
        /* palavra de operador: sequencia de letras (e '*' / '\'' / '"') */
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '*' || c == '\'') {
            size_t j = i;
            while (j < n && ((d[j] >= 'A' && d[j] <= 'Z') || (d[j] >= 'a' && d[j] <= 'z') ||
                             d[j] == '*' || d[j] == '\'' || d[j] == '"')) j++;
            size_t wl = j - i;
            char w[16];
            size_t cl = wl < sizeof(w) - 1 ? wl : sizeof(w) - 1;
            memcpy(w, d + i, cl); w[cl] = '\0';
            i = j;
            if (strcmp(w, "Tj") == 0 || strcmp(w, "TJ") == 0) {
                if (strcmp(w, "Tj") == 0) {
                    if (tem_pend && pend) { pdf_sep(out, 0); pdf_emit_token(out, pend, pendlen); }
                } else {
                    /* TJ: concatena direto (fragmentos com kerning sao a
                       mesma palavra); espacos so via ajuste grande */
                    for (size_t k = 0; k < arr_n; k++) {
                        if (arr[k].is_space) { pdf_sep(out, 0); }
                        else if (arr[k].s) { pdf_emit_token(out, arr[k].s, arr[k].len); }
                    }
                }
                pdf_sep(out, 0);
                free(pend); pend = NULL; pendlen = 0; tem_pend = 0;
                for (size_t k = 0; k < arr_n; k++) free(arr[k].s);
                arr_n = 0;
            } else if (strcmp(w, "T") == 0 || strcmp(w, "Td") == 0 || strcmp(w, "TD") == 0 ||
                       strcmp(w, "Tm") == 0 || strcmp(w, "T*") == 0 || strcmp(w, "ET") == 0) {
                /* quebra de linha/posicionamento: descarta pendente solto */
                free(pend); pend = NULL; pendlen = 0; tem_pend = 0;
                for (size_t k = 0; k < arr_n; k++) free(arr[k].s);
                arr_n = 0;
                pdf_sep(out, 1);
            } else {
                /* outro operador (Tf, Tc, Tw, BT, etc.): pendente solto
                   sem Tj = metadado; descarta */
                if (strcmp(w, "BT") != 0) {
                    free(pend); pend = NULL; pendlen = 0; tem_pend = 0;
                    if (strcmp(w, "Q") == 0 || strcmp(w, "q") == 0) {
                        for (size_t k = 0; k < arr_n; k++) free(arr[k].s);
                        arr_n = 0;
                    }
                }
            }
            continue;
        }
        /* dicionario << >> ou outros simbolos: avanca */
        if (c == '<' || c == '>' || c == '{' || c == '}') { i++; continue; }
        i++;
    }
    free(pend);
    for (size_t k = 0; k < arr_n; k++) free(arr[k].s);
    free(arr);
}

static int has_bytes(const unsigned char *d, size_t n, const char *needle, size_t from, size_t to) {
    size_t nl = strlen(needle);
    if (to > n) to = n;
    if (from >= to || nl == 0 || to - from < nl) return 0;
    for (size_t i = from; i + nl <= to; i++)
        if (memcmp(d + i, needle, nl) == 0) return 1;
    return 0;
}

/* Fase 7.4: coleta filtros do /Filter do dicionario PROPRIO do stream.
   ids: 0=Flate, 1=ASCIIHex, 2=ASCII85, 5=RunLength (suportados);
   3=LZW, 4=outros (DCT/JPX/CCITT/JBIG2: imagem, pula silencioso).
   Retorna nf; *silencioso=1 se houver filtro de imagem/nao-texto. */
static int pdf_coleta_filtros(const unsigned char *dict, size_t dlen,
                              int *filtros, int maxf, int *silencioso) {
    if (silencioso) *silencioso = 0;
    /* ultimo /Filter da regiao (o do proprio dict; anteriores sao de
       objetos vizinhos e NAO podem entrar na cadeia) */
    const char *last = NULL;
    for (size_t i = 0; i + 7 < dlen; i++) {
        if (dict[i] == '/' && memcmp(dict + i, "/Filter", 7) == 0) {
            char nx = (i + 7 < dlen) ? (char)dict[i + 7] : '\0';
            if (nx == '\0' || isspace((unsigned char)nx) || nx == '/' || nx == '[')
                last = (const char *)(dict + i);
        }
    }
    if (!last) return 0;
    size_t off = (size_t)(last - (const char *)dict) + 7;
    while (off < dlen && isspace(dict[off])) off++;
    int nf = 0;
    int em_array = 0;
    if (off < dlen && dict[off] == '[') { em_array = 1; off++; }
    for (;;) {
        while (off < dlen && isspace(dict[off])) off++;
        if (off >= dlen) break;
        if (em_array && dict[off] == ']') break;
        if (dict[off] != '/') break; /* fim do valor (/Length etc.) */
        off++;
        size_t ns = off;
        while (off < dlen && !isspace(dict[off]) && dict[off] != '/' &&
               dict[off] != '[' && dict[off] != ']' && dict[off] != '<' &&
               dict[off] != '>' && dict[off] != '(') off++;
        size_t nl = off - ns;
        int id = 4;
        if ((nl == 11 && memcmp(dict + ns, "FlateDecode", 11) == 0) ||
            (nl == 2 && memcmp(dict + ns, "Fl", 2) == 0)) id = 0;
        else if ((nl == 14 && memcmp(dict + ns, "ASCIIHexDecode", 14) == 0) ||
                 (nl == 3 && memcmp(dict + ns, "AHx", 3) == 0)) id = 1;
        else if ((nl == 13 && memcmp(dict + ns, "ASCII85Decode", 13) == 0) ||
                 (nl == 3 && memcmp(dict + ns, "A85", 3) == 0)) id = 2;
        else if ((nl == 9 && memcmp(dict + ns, "LZWDecode", 9) == 0) ||
                 (nl == 3 && memcmp(dict + ns, "LZW", 3) == 0)) id = 3;
        else if ((nl == 15 && memcmp(dict + ns, "RunLengthDecode", 15) == 0) ||
                 (nl == 2 && memcmp(dict + ns, "RL", 2) == 0)) id = 5;
        else id = 4;
        if (id == 3 || id == 4) { if (silencioso) *silencioso = 1; return 0; }
        if (nf < maxf) filtros[nf++] = id;
        else break;
        if (!em_array) break; /* nome unico fora de array */
    }
    return nf;
}

/* RunLengthDecode (Fase 7.4): simples e comum em PDFs. */
static int pdf_rle_decode(const unsigned char *in, size_t ilen,
                          unsigned char **out, size_t *olen) {
    ByteBuf b; buf_init(&b);
    size_t i = 0;
    while (i < ilen) {
        unsigned char lb = in[i++];
        if (lb == 128) break; /* EOD */
        if (lb < 128) {
            size_t n = (size_t)lb + 1;
            if (i + n > ilen) { buf_free(&b); return -1; }
            buf_append(&b, in + i, n);
            i += n;
        } else {
            size_t n = 257 - (size_t)lb;
            if (i >= ilen) { buf_free(&b); return -1; }
            for (size_t k = 0; k < n; k++) buf_append(&b, in + i, 1);
            i++;
        }
    }
    *out = b.data; *olen = b.len;
    return 0;
}

/* Aplica cadeia de filtros com tolerancia: 0 ok, -1 falha (pular stream). */
static int pdf_aplica_filtros(const unsigned char *in, size_t ilen,
                              const int *filtros, int nf,
                              unsigned char **out, size_t *olen,
                              int *falha_flate, int *falha_decode) {
    if (nf == 0) { *out = NULL; *olen = 0; return 1; /* sem filtro: usa original */ }
    unsigned char *cur = (unsigned char *)xmalloc(ilen ? ilen : 1);
    if (ilen) memcpy(cur, in, ilen);
    size_t curlen = ilen;
    for (int f = 0; f < nf; f++) {
        unsigned char *nx = NULL;
        size_t nxlen = 0;
        int rc = -1;
        if (filtros[f] == 0) {
            rc = zlib_inflate(cur, curlen, &nx, &nxlen);
            if (rc != 0) rc = inflate_raw(cur, curlen, &nx, &nxlen);
            if (rc != 0 && falha_flate) *falha_flate = 1;
        } else if (filtros[f] == 1) {
            rc = pdf_ahx_decode(cur, curlen, &nx, &nxlen);
            if (rc != 0 && falha_decode) *falha_decode = 1;
        } else if (filtros[f] == 2) {
            rc = pdf_a85_decode(cur, curlen, &nx, &nxlen);
            if (rc != 0 && falha_decode) *falha_decode = 1;
        } else if (filtros[f] == 5) {
            rc = pdf_rle_decode(cur, curlen, &nx, &nxlen);
            if (rc != 0 && falha_decode) *falha_decode = 1;
        } else {
            rc = -1; /* LZW nao suportado */
            if (falha_decode) *falha_decode = 1;
        }
        free(cur);
        cur = NULL;
        if (rc != 0) { free(nx); return -1; }
        cur = nx; curlen = nxlen;
    }
    *out = cur; *olen = curlen;
    return 0;
}

DocumentoExtraido *extrair_pdf(const char *caminho, char **erro) {
    unsigned char *raw = NULL;
    size_t n = 0;
    if (read_file_bytes(caminho, &raw, &n) != 0) {
        if (erro) *erro = xstrdup("nao foi possivel ler arquivo pdf");
        return NULL;
    }
    if (n < 5 || memcmp(raw, "%PDF", 4) != 0) {
        /* tenta como texto puro */
        free(raw);
        return extrair_txt(caminho, erro);
    }
    DocumentoExtraido *d = doc_novo();
    d->titulo = xstrdup(caminho);
    d->stats.bytes_raw = n;

    ByteBuf **pages = NULL;
    int npages = 0;

    /* varre objetos stream..endstream com tolerancia (stream ruim nao aborta) */
    size_t pos = 0;
    while (pos < n) {
        /* acha "stream" */
        size_t s = pos;
        int found = 0;
        while (s + 6 < n) {
            if (memcmp(raw + s, "stream", 6) == 0 &&
                (s + 6 >= n || raw[s+6] == '\r' || raw[s+6] == '\n')) { found = 1; break; }
            s++;
        }
        if (!found) break;
        size_t data_start = s + 6;
        while (data_start < n && (raw[data_start] == '\r' || raw[data_start] == '\n')) data_start++;
        /* acha "endstream" */
        size_t e = data_start;
        int found_e = 0;
        while (e + 9 < n) {
            if (memcmp(raw + e, "endstream", 9) == 0) { found_e = 1; break; }
            e++;
        }
        if (!found_e) break;
        size_t data_end = e;
        while (data_end > data_start && (raw[data_end-1] == '\r' || raw[data_end-1] == '\n')) data_end--;

        /* dicionario proprio: apos o ultimo "endobj" (ou inicio da janela),
           para nao contaminar com /Filter de objetos vizinhos */
        size_t dict_from = (s > 4096) ? s - 4096 : 0;
        {
            size_t k = dict_from;
            while (k + 7 < s) {
                if (memcmp(raw + k, "endobj", 6) == 0 &&
                    (k + 6 >= s || isspace(raw[k + 6]))) {
                    size_t apos = k + 6;
                    if (apos < s) dict_from = apos;
                }
                k++;
            }
        }
        size_t dict_len = (s > dict_from) ? s - dict_from : 0;
        int filtros[8];
        int silencioso = 0;
        int nf = pdf_coleta_filtros(raw + dict_from, dict_len, filtros, 8, &silencioso);
        if (silencioso) { pos = e + 9; continue; } /* imagem/objeto: pula sem contar falha */
        const unsigned char *content = raw + data_start;
        size_t clen = (data_end > data_start) ? data_end - data_start : 0;
        unsigned char *dec = NULL;
        size_t dlen = 0;
        const unsigned char *use = content;
        size_t uselen = clen;
        int dec_owned = 0;
        int f_flate = 0, f_dec = 0;
        d->stats.total_streams++;
        if (nf > 0 && clen > 0) {
            int rc = pdf_aplica_filtros(content, clen, filtros, nf, &dec, &dlen,
                                        &f_flate, &f_dec);
            if (rc == 0) {
                use = dec; uselen = dlen; dec_owned = 1;
            } else {
                /* stream com filtro quebrado: tolera e pula sem abortar */
                if (f_flate) d->stats.failed_inflate++;
                if (f_dec) d->stats.failed_decode++;
                if (dec) free(dec);
                pos = e + 9;
                continue;
            }
        } else if (nf == 0) {
            /* compat: Flate sem /Filter explicito cai aqui; tenta inflate
               direto apenas se parecer zlib/deflate valido */
            int has_flate = has_bytes(raw, n, "FlateDecode", dict_from, s);
            if (has_flate && clen > 0) {
                if (zlib_inflate(content, clen, &dec, &dlen) == 0 ||
                    inflate_raw(content, clen, &dec, &dlen) == 0) {
                    use = dec; uselen = dlen; dec_owned = 1;
                }
            }
        }
        /* so considera streams que parecem conteudo de pagina */
        if (uselen > 0 && uselen < 8 * 1024 * 1024) {
            int looks_text = has_bytes(use, uselen, "BT", 0, uselen) ||
                             has_bytes(use, uselen, "Tj", 0, uselen) ||
                             has_bytes(use, uselen, "TJ", 0, uselen) ||
                             has_bytes(use, uselen, "Tf", 0, uselen) ||
                             has_bytes(use, uselen, "'", 0, uselen);
            if (looks_text) {
                ByteBuf out; buf_init(&out);
                extract_text_from_content(use, uselen, &out);
                if (out.len > 0) {
                    d->stats.text_streams++;
                    d->stats.chars_extraidos += out.len;
                    buf_reserve(&out, 1);
                    out.data[out.len] = '\0';
                    ByteBuf *nb = (ByteBuf *)xmalloc(sizeof(ByteBuf));
                    buf_init(nb);
                    buf_append(nb, out.data, out.len);
                    pages = (ByteBuf **)xrealloc(pages, sizeof(ByteBuf *) * (size_t)(npages + 1));
                    pages[npages++] = nb;
                }
                buf_free(&out);
            }
        }
        if (dec_owned) free(dec);
        pos = e + 9;
    }

    /* fallback: varre arquivo inteiro por strings com operadores */
    if (npages == 0) {
        d->stats.used_fallback = 1;
        ByteBuf out; buf_init(&out);
        extract_text_from_content(raw, n, &out);
        if (out.len > 0) {
            d->stats.chars_extraidos += out.len;
            buf_reserve(&out, 1);
            out.data[out.len] = '\0';
            ByteBuf *nb = (ByteBuf *)xmalloc(sizeof(ByteBuf));
            buf_init(nb);
            buf_append(nb, out.data, out.len);
            pages = (ByteBuf **)xrealloc(pages, sizeof(ByteBuf *));
            pages[0] = nb;
            npages = 1;
        }
        buf_free(&out);
    }

    if (npages == 0) {
        free(raw);
        for (int i = 0; i < npages; i++) { buf_free(pages[i]); free(pages[i]); }
        free(pages);
        if (erro) *erro = xstrdup("pdf sem texto extraivel (pode ser escaneado/imagem)");
        liberar_documento(d);
        return NULL;
    }

    /* conta paginas pelo marcador /Type /Page (aproximacao) */
    int page_marks = 0;
    for (size_t i = 0; i + 5 < n; i++)
        if (memcmp(raw + i, "/Type", 5) == 0) {
            size_t j = i + 5;
            while (j < n && isspace(raw[j])) j++;
            if (j + 5 < n && memcmp(raw + j, "/Page", 5) == 0) {
                if (!(j + 6 < n && raw[j+5] == 's')) page_marks++;
            }
        }
    d->num_paginas = page_marks > 0 ? page_marks : npages;

    for (int i = 0; i < npages; i++) {
        buf_reserve(pages[i], 1);
        pages[i]->data[pages[i]->len] = '\0';
        char *t = str_trim((char *)pages[i]->data);
        if (strlen(t) == 0) continue;
        int pagina = (d->num_paginas == npages) ? (i + 1)
                     : (int)((long long)i * d->num_paginas / npages) + 1;
        /* divide pagina em paragrafos por linhas duplas */
        char *copy = xstrdup(t);
        char *line = copy;
        ByteBuf para; buf_init(&para);
        for (char *p = copy; ; p++) {
            int fim = (*p == '\0');
            int nl = (*p == '\n');
            if (!fim) buf_append(&para, p, 1);
            if (fim || (nl && (*(p+1) == '\n' || *(p+1) == '\r' || *(p+1) == '\0'))) {
                buf_reserve(&para, 1); para.data[para.len] = '\0';
                char *pt = str_trim((char *)para.data);
                if (strlen(pt) > 0) doc_add_bloco(d, pt, pagina);
                para.len = 0;
            }
            if (fim) break;
        }
        buf_free(&para);
        free(copy);
        (void)line;
    }

    for (int i = 0; i < npages; i++) { buf_free(pages[i]); free(pages[i]); }
    free(pages);
    free(raw);

    if (d->num_blocos == 0) {
        if (erro) *erro = xstrdup("pdf sem texto extraivel");
        liberar_documento(d);
        return NULL;
    }
    return d;
}

DocumentoExtraido *extrair_documento(const char *caminho, char **erro) {
    char lower[1024];
    size_t i = 0;
    for (; caminho[i] && i < sizeof(lower) - 1; i++)
        lower[i] = (char)tolower((unsigned char)caminho[i]);
    lower[i] = '\0';
    if (str_ends_with(lower, ".pdf")) return extrair_pdf(caminho, erro);
    if (str_ends_with(lower, ".csv")) return extrair_csv(caminho, erro);
    if (str_ends_with(lower, ".json")) return extrair_json(caminho, erro);
    /* .txt e demais: tenta pdf se assinatura, senao txt */
    unsigned char *raw = NULL;
    size_t n = 0;
    if (read_file_bytes(caminho, &raw, &n) == 0) {
        int ispdf = (n >= 4 && memcmp(raw, "%PDF", 4) == 0);
        free(raw);
        if (ispdf) return extrair_pdf(caminho, erro);
    }
    return extrair_txt(caminho, erro);
}
