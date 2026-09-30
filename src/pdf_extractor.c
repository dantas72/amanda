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

/* ============ PDF text scan ============ */

static void pdf_emit(ByteBuf *page, ByteBuf *all_pages, int *page_count,
                     ByteBuf ***pages, int *npages) {
    (void)all_pages; (void)page_count;
    if (page->len == 0) return;
    buf_reserve(page, 1);
    page->data[page->len] = '\0';
    ByteBuf *nb = (ByteBuf *)xmalloc(sizeof(ByteBuf));
    buf_init(nb);
    buf_append(nb, page->data, page->len);
    *pages = (ByteBuf **)xrealloc(*pages, sizeof(ByteBuf *) * (size_t)(*npages + 1));
    (*pages)[(*npages)++] = nb;
    page->len = 0;
}

/* extrai strings de um fluxo de conteudo ja descomprimido */
static void extract_text_from_content(const unsigned char *d, size_t n, ByteBuf *out) {
    size_t i = 0;
    while (i < n) {
        if (d[i] == '(') {
            i++;
            int depth = 1;
            ByteBuf s; buf_init(&s);
            while (i < n && depth > 0) {
                if (d[i] == '\\' && i + 1 < n) {
                    char e = (char)d[i+1];
                    if (e == 'n') buf_append(&s, "\n", 1);
                    else if (e == 'r') buf_append(&s, "\n", 1);
                    else if (e == 't') buf_append(&s, "\t", 1);
                    else if (e >= '0' && e <= '7') {
                        int v = 0, k = 0;
                        while (k < 3 && i + 1 + k < n && d[i+1+k] >= '0' && d[i+1+k] <= '7') {
                            v = v * 8 + (d[i+1+k] - '0'); k++;
                        }
                        char c = (char)v;
                        buf_append(&s, &c, 1);
                        i += (size_t)k + 1;
                        continue;
                    } else buf_append(&s, &e, 1);
                    i += 2;
                } else if (d[i] == '(') { depth++; buf_append(&s, "(", 1); i++; }
                else if (d[i] == ')') { depth--; i++; if (depth == 0) break; else buf_append(&s, ")", 1); }
                else { buf_append(&s, &d[i], 1); i++; }
            }
            /* filtra lixo binario: exige maioria imprimivel */
            int print = 0;
            for (size_t k = 0; k < s.len; k++)
                if (s.data[k] == '\n' || s.data[k] == '\t' || (s.data[k] >= 32 && s.data[k] < 127) || s.data[k] >= 128) print++;
            if (s.len >= 1 && print * 2 >= (int)s.len) {
                buf_append(out, s.data, s.len);
                buf_append(out, " ", 1);
            }
            buf_free(&s);
        } else if (d[i] == '<' && i + 1 < n && d[i+1] != '<') {
            i++;
            ByteBuf s; buf_init(&s);
            char hex[3] = {0,0,0};
            int hi = 0, have = 0;
            while (i < n && d[i] != '>') {
                if (isxdigit(d[i])) {
                    hex[hi++] = (char)d[i];
                    if (hi == 2) {
                        char c = (char)strtol(hex, NULL, 16);
                        if (c == 0) c = ' ';
                        buf_append(&s, &c, 1);
                        hi = 0; have = 1;
                    }
                }
                i++;
            }
            if (hi == 1) { hex[1] = '0'; char c = (char)strtol(hex, NULL, 16); buf_append(&s, &c, 1); have = 1; }
            if (i < n) i++;
            if (have && s.len) { buf_append(out, s.data, s.len); buf_append(out, " ", 1); }
            buf_free(&s);
        } else {
            i++;
        }
    }
}

static int has_bytes(const unsigned char *d, size_t n, const char *needle, size_t from, size_t to) {
    size_t nl = strlen(needle);
    if (to > n) to = n;
    if (from >= to || nl == 0 || to - from < nl) return 0;
    for (size_t i = from; i + nl <= to; i++)
        if (memcmp(d + i, needle, nl) == 0) return 1;
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

    ByteBuf **pages = NULL;
    int npages = 0;

    /* varre objetos stream..endstream */
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

        /* verifica dicionario anterior (ate 4KB antes) para FlateDecode e Page */
        size_t dict_from = (s > 4096) ? s - 4096 : 0;
        int is_flate = has_bytes(raw, n, "FlateDecode", dict_from, s);
        const unsigned char *content = raw + data_start;
        size_t clen = (data_end > data_start) ? data_end - data_start : 0;
        unsigned char *dec = NULL;
        size_t dlen = 0;
        const unsigned char *use = content;
        size_t uselen = clen;
        int dec_ok = 0;
        if (is_flate && clen > 0) {
            if (zlib_inflate(content, clen, &dec, &dlen) == 0 ||
                inflate_raw(content, clen, &dec, &dlen) == 0) {
                use = dec; uselen = dlen; dec_ok = 1;
            }
        }
        /* so considera streams que parecem conteudo de pagina */
        if (uselen > 0 && uselen < 8 * 1024 * 1024) {
            int looks_text = has_bytes(use, uselen, "BT", 0, uselen) ||
                             has_bytes(use, uselen, "Tj", 0, uselen) ||
                             has_bytes(use, uselen, "TJ", 0, uselen) ||
                             has_bytes(use, uselen, "Tf", 0, uselen);
            if (looks_text) {
                ByteBuf out; buf_init(&out);
                extract_text_from_content(use, uselen, &out);
                if (out.len > 0) {
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
        if (dec_ok) free(dec);
        pos = e + 9;
    }

    /* fallback: varre arquivo inteiro por strings */
    if (npages == 0) {
        ByteBuf out; buf_init(&out);
        extract_text_from_content(raw, n, &out);
        if (out.len > 0) {
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
    (void)pdf_emit;
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
