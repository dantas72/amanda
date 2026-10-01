#include "packager.h"
#include "amanda.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

int empacotar_amanda(AmandaPackage *pkg, const char *saida, char **erro) {
    ByteBuf b;
    buf_init(&b);
    buf_append(&b, AMANDA_MAGIC, AMANDA_MAGIC_LEN);
    buf_append_u32(&b, AMANDA_FORMAT_VERSION);
    /* versao app: major minor patch */
    int maj = 1, min = 0, pat = 1;
    if (pkg->versao_app) sscanf(pkg->versao_app, "%d.%d.%d", &maj, &min, &pat);
    buf_append_u32(&b, (uint32_t)maj);
    buf_append_u32(&b, (uint32_t)min);
    buf_append_u32(&b, (uint32_t)pat);
    buf_append_str(&b, pkg->titulo ? pkg->titulo : "");
    buf_append_str(&b, pkg->autor ? pkg->autor : "");
    buf_append_str(&b, pkg->data ? pkg->data : "");
    buf_append_str(&b, pkg->idioma ? pkg->idioma : "pt-BR");
    /* Fase 7.4 (formato v2): cobertura da extracao */
    buf_append_i32(&b, pkg->num_paginas);
    buf_append_i32(&b, pkg->extra_blocos);
    buf_append_i32(&b, pkg->extra_total_streams);
    buf_append_i32(&b, pkg->extra_text_streams);
    buf_append_i32(&b, pkg->extra_failed);
    buf_append_i32(&b, pkg->extra_fallback);
    /* chunks */
    buf_append_u32(&b, (uint32_t)(pkg->num_chunks < 0 ? 0 : pkg->num_chunks));
    for (int i = 0; i < pkg->num_chunks; i++) {
        buf_append_str(&b, pkg->chunks[i].texto);
        buf_append_i32(&b, pkg->chunks[i].pagina_inicio);
        buf_append_i32(&b, pkg->chunks[i].pagina_fim);
        buf_append_str(&b, pkg->chunks[i].hash ? pkg->chunks[i].hash : "");
        buf_append_i32(&b, pkg->chunks[i].num_tokens);
    }
    /* embeddings */
    int dim = (pkg->embeddings) ? pkg->embeddings->dimensao : AMANDA_EMBED_DIM;
    buf_append_u32(&b, (uint32_t)dim);
    buf_append_u32(&b, (uint32_t)(pkg->num_chunks));
    for (int i = 0; i < pkg->num_chunks; i++) {
        float *v = pkg->embeddings ? pkg->embeddings->vetores + (size_t)i * pkg->embeddings->dimensao : NULL;
        for (int k = 0; k < dim; k++) {
            float f = (v && k < pkg->embeddings->dimensao) ? v[k] : 0;
            buf_append_f32(&b, f);
        }
    }
    /* perguntas */
    buf_append_u32(&b, (uint32_t)(pkg->num_perguntas < 0 ? 0 : pkg->num_perguntas));
    for (int i = 0; i < pkg->num_perguntas; i++) {
        PerguntaTipada *q = &pkg->perguntas[i];
        buf_append_u32(&b, (uint32_t)q->tipo);
        buf_append_str(&b, q->enunciado ? q->enunciado : "");
        buf_append_u32(&b, (uint32_t)(q->num_opcoes < 0 ? 0 : q->num_opcoes));
        for (int k = 0; k < q->num_opcoes; k++)
            buf_append_str(&b, q->opcoes[k] ? q->opcoes[k] : "");
        buf_append_f32(&b, q->min);
        buf_append_f32(&b, q->max);
        buf_append_str(&b, q->afirmacao ? q->afirmacao : "");
        buf_append_i32(&b, q->pagina_fonte);
        buf_append_str(&b, q->chunk_hash ? q->chunk_hash : "");
    }
    uint32_t crc = crc32_ieee(b.data, b.len);
    buf_append_u32(&b, crc);
    int rc = write_file_bytes(saida, b.data, b.len);
    buf_free(&b);
    if (rc != 0 && erro) *erro = xstrdup("falha ao escrever arquivo .amanda");
    return rc == 0 ? 0 : -1;
}

AmandaPackage *carregar_amanda(const char *caminho, char **erro) {
    unsigned char *raw = NULL;
    size_t n = 0;
    if (read_file_bytes(caminho, &raw, &n) != 0) {
        if (erro) *erro = xstrdup("nao foi possivel ler pacote .amanda");
        return NULL;
    }
    if (n < AMANDA_MAGIC_LEN + 4 + 4) {
        free(raw);
        if (erro) *erro = xstrdup("pacote .amanda muito pequeno");
        return NULL;
    }
    if (memcmp(raw, AMANDA_MAGIC, AMANDA_MAGIC_LEN) != 0) {
        free(raw);
        if (erro) *erro = xstrdup("assinatura .amanda invalida");
        return NULL;
    }
    uint32_t stored_crc = read_u32_le(raw + n - 4);
    uint32_t calc = crc32_ieee(raw, n - 4);
    if (stored_crc != calc) {
        free(raw);
        if (erro) *erro = xstrdup("crc do pacote .amanda invalido (arquivo corrompido)");
        return NULL;
    }
    ByteReader r;
    reader_init(&r, raw, n - 4);
    r.pos = 4;
    uint32_t fmt = reader_u32(&r);
    if (r.err || (fmt != 1u && fmt != AMANDA_FORMAT_VERSION)) {
        free(raw);
        if (erro) *erro = xstrdup("versao de formato .amanda nao suportada");
        return NULL;
    }
    uint32_t maj = reader_u32(&r), min = reader_u32(&r), pat = reader_u32(&r);
    AmandaPackage *pkg = (AmandaPackage *)xcalloc(1, sizeof(*pkg));
    char vb[64];
    snprintf(vb, sizeof vb, "%u.%u.%u", maj, min, pat);
    pkg->versao_app = xstrdup(vb);
    pkg->titulo = reader_str(&r);
    pkg->autor = reader_str(&r);
    pkg->data = reader_str(&r);
    pkg->idioma = reader_str(&r);
    if (r.err) goto fail;
    if (fmt >= 2u) {
        pkg->num_paginas = reader_i32(&r);
        pkg->extra_blocos = reader_i32(&r);
        pkg->extra_total_streams = reader_i32(&r);
        pkg->extra_text_streams = reader_i32(&r);
        pkg->extra_failed = reader_i32(&r);
        pkg->extra_fallback = reader_i32(&r);
        if (r.err) goto fail;
    }
    uint32_t nc = reader_u32(&r);
    if (r.err || nc > 100000) goto fail;
    pkg->num_chunks = (int)nc;
    if (nc) pkg->chunks = (Chunk *)xcalloc(nc, sizeof(Chunk));
    for (uint32_t i = 0; i < nc; i++) {
        pkg->chunks[i].texto = reader_str(&r);
        pkg->chunks[i].pagina_inicio = reader_i32(&r);
        pkg->chunks[i].pagina_fim = reader_i32(&r);
        pkg->chunks[i].hash = reader_str(&r);
        pkg->chunks[i].num_tokens = reader_i32(&r);
        if (r.err) goto fail;
    }
    {
        uint32_t dim = reader_u32(&r);
        uint32_t nv = reader_u32(&r);
        if (r.err || dim == 0 || dim > 4096 || nv != nc) goto fail;
        pkg->embeddings = (Embeddings *)xcalloc(1, sizeof(Embeddings));
        pkg->embeddings->dimensao = (int)dim;
        pkg->embeddings->num_vetores = (int)nv;
        if (nv) {
            pkg->embeddings->vetores = (float *)xcalloc((size_t)nv * dim, sizeof(float));
            for (uint32_t i = 0; i < nv; i++)
                for (uint32_t k = 0; k < dim; k++) {
                    pkg->embeddings->vetores[(size_t)i * dim + k] = reader_f32(&r);
                    if (r.err) goto fail;
                }
        }
    }
    {
        uint32_t nq = reader_u32(&r);
        if (r.err || nq > 1000000) goto fail;
        pkg->num_perguntas = (int)nq;
        if (nq) pkg->perguntas = (PerguntaTipada *)xcalloc(nq, sizeof(PerguntaTipada));
        for (uint32_t i = 0; i < nq; i++) {
            PerguntaTipada *q = &pkg->perguntas[i];
            q->tipo = (TipoPergunta)reader_u32(&r);
            q->enunciado = reader_str(&r);
            uint32_t no = reader_u32(&r);
            if (r.err || no > 64) goto fail;
            q->num_opcoes = (int)no;
            if (no) q->opcoes = (char **)xcalloc(no, sizeof(char *));
            for (uint32_t k = 0; k < no; k++) {
                q->opcoes[k] = reader_str(&r);
                if (r.err) goto fail;
            }
            q->min = reader_f32(&r);
            q->max = reader_f32(&r);
            q->afirmacao = reader_str(&r);
            q->pagina_fonte = reader_i32(&r);
            q->chunk_hash = reader_str(&r);
            if (r.err) goto fail;
        }
    }
    free(raw);
    return pkg;
fail:
    free(raw);
    liberar_package(pkg);
    if (erro) *erro = xstrdup("pacote .amanda truncado ou invalido");
    return NULL;
}

void liberar_package(AmandaPackage *pkg) {
    if (!pkg) return;
    free(pkg->titulo);
    free(pkg->autor);
    free(pkg->data);
    free(pkg->idioma);
    free(pkg->versao_app);
    liberar_chunks(pkg->chunks, pkg->num_chunks);
    liberar_embeddings(pkg->embeddings);
    liberar_perguntas(pkg->perguntas, pkg->num_perguntas);
    free(pkg);
}

int package_stats(const AmandaPackage *pkg, char *buf, size_t bufsz) {
    size_t total_chars = 0;
    for (int i = 0; i < pkg->num_chunks; i++)
        total_chars += strlen(pkg->chunks[i].texto);
    int nchoice = 0, nscore = 0, nnoul = 0;
    for (int i = 0; i < pkg->num_perguntas; i++) {
        if (pkg->perguntas[i].tipo == TIPO_CHOICE) nchoice++;
        else if (pkg->perguntas[i].tipo == TIPO_SCORE) nscore++;
        else nnoul++;
    }
    return snprintf(buf, bufsz,
        "titulo: %s\nautor: %s\ndata: %s\nidioma: %s\nversao_app: %s\n"
        "paginas: %d\nblocos: %d\n"
        "chunks: %d\nperguntas: %d (choice=%d score=%d noul=%d)\n"
        "dimensao_embedding: %d\ntotal_caracteres: %llu\n"
        "extracao: streams=%d texto=%d falhas=%d fallback=%s\n",
        pkg->titulo ? pkg->titulo : "",
        pkg->autor ? pkg->autor : "",
        pkg->data ? pkg->data : "",
        pkg->idioma ? pkg->idioma : "",
        pkg->versao_app ? pkg->versao_app : "",
        pkg->num_paginas, pkg->extra_blocos,
        pkg->num_chunks, pkg->num_perguntas, nchoice, nscore, nnoul,
        pkg->embeddings ? pkg->embeddings->dimensao : 0,
        (unsigned long long)total_chars,
        pkg->extra_total_streams, pkg->extra_text_streams,
        pkg->extra_failed, pkg->extra_fallback ? "sim" : "nao");
}
