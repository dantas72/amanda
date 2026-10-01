#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "amanda.h"
#include "pdf_extractor.h"
#include "chunker.h"
#include "embedder.h"
#include "question_gen.h"
#include "decision_engine.h"
#include "laya_backend.h"
#include "packager.h"
#include "eval.h"
#include "utils.h"

static int passes = 0, fails = 0;

#define CHECK(cond, msg) do { \
    if (cond) { passes++; printf("  ok: %s\n", msg); } \
    else { fails++; printf("  FALHA: %s\n", msg); } \
} while (0)

static void test_chunker(void) {
    printf("[chunker]\n");
    DocumentoExtraido doc;
    memset(&doc, 0, sizeof doc);
    BlocoTexto b;
    b.texto = "A entropia e uma medida da desordem de um sistema. Ela cresce com o numero de microestados. A segunda lei da termodinamica afirma que a entropia aumenta.";
    b.pagina = 1; b.x = b.y = b.largura = b.altura = 0;
    doc.blocos = &b; doc.num_blocos = 1; doc.num_paginas = 1;
    int n = 0;
    Chunk *c = dividir_em_chunks(&doc, 10, 2, &n);
    CHECK(c != NULL && n >= 2, "chunking gera multiplos chunks com overlap");
    if (c) {
        CHECK(c[0].pagina_inicio == 1, "pagina preservada");
        CHECK(c[0].hash && strlen(c[0].hash) == 8, "hash de 8 chars");
        liberar_chunks(c, n);
    }
}

static void test_embedder(void) {
    printf("[embedder]\n");
    Chunk ch[2];
    memset(ch, 0, sizeof ch);
    ch[0].texto = "o gato sentou no tapete";
    ch[0].hash = "a1"; ch[0].pagina_inicio = 1;
    ch[1].texto = "o cachorro correu no parque";
    ch[1].hash = "b2"; ch[1].pagina_inicio = 1;
    Embeddings *e = gerar_embeddings(ch, 2);
    CHECK(e && e->dimensao == 384, "dimensao 384");
    float s0 = cos_sim(e->vetores, e->vetores, 384);
    CHECK(s0 > 0.99f, "auto-similaridade ~1");
    Embeddings *q = embed_query("gato tapete");
    float s_a = cos_sim(q->vetores, e->vetores, 384);
    float s_b = cos_sim(q->vetores, e->vetores + 384, 384);
    CHECK(s_a > s_b, "query 'gato tapete' mais proxima do chunk do gato");
    liberar_embeddings(e);
    liberar_embeddings(q);
}

static void test_question_gen(void) {
    printf("[question_gen]\n");
    Chunk ch;
    memset(&ch, 0, sizeof ch);
    ch.texto = "A fotossintese ocorre nos cloroplastos das plantas. A clorofila absorve luz solar. A glicose e produzida a partir de agua e dioxido de carbono.";
    ch.hash = "h1234567"; ch.pagina_inicio = 2;
    QuestionGenConfig cfg = {3, 2, 5};
    int nq = 0;
    PerguntaTipada *qs = gerar_perguntas(&ch, 1, &cfg, &nq);
    CHECK(qs != NULL && nq > 0, "gera perguntas");
    int has_choice = 0, has_noul = 0, has_score = 0;
    for (int i = 0; i < nq; i++) {
        if (qs[i].tipo == TIPO_CHOICE) { has_choice = 1; CHECK(qs[i].num_opcoes == 4, "choice tem 4 opcoes"); }
        if (qs[i].tipo == TIPO_NOUL) has_noul = 1;
        if (qs[i].tipo == TIPO_SCORE) has_score = 1;
    }
    CHECK(has_noul, "gera noul");
    CHECK(has_score, "gera score");
    liberar_perguntas(qs, nq);
}

static void test_decision(void) {
    printf("[decision_engine]\n");
    DocumentoExtraido doc;
    memset(&doc, 0, sizeof doc);
    BlocoTexto bs[2];
    bs[0].texto = "A capital do Brasil e Brasilia, inaugurada em 1960.";
    bs[0].pagina = 1; bs[0].x = bs[0].y = bs[0].largura = bs[0].altura = 0;
    bs[1].texto = "A fotossintese produz glicose nas plantas verdes.";
    bs[1].pagina = 2; bs[1].x = bs[1].y = bs[1].largura = bs[1].altura = 0;
    doc.blocos = bs; doc.num_blocos = 2; doc.num_paginas = 2;
    int nc = 0;
    Chunk *ch = dividir_em_chunks(&doc, 180, 0, &nc);
    CHECK(ch && nc >= 1, "chunks para decisao");
    Embeddings *e = gerar_embeddings(ch, nc);
    DecisionConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.limiar_confianca = 0.7f; cfg.limiar_recusa = 0.3f; cfg.top_k = 2;
    Decisao *d = executar_decisao("Qual e a capital do Brasil?", ch, nc, e, &cfg);
    CHECK(d && d->resposta && strstr(d->resposta, "Brasilia") != NULL, "resposta cita Brasilia");
    liberar_decisao(d);
    liberar_embeddings(e);
    liberar_chunks(ch, nc);
}

static void test_packager(void) {
    printf("[packager]\n");
    Chunk *ch = (Chunk *)xcalloc(2, sizeof(Chunk));
    ch[0].texto = xstrdup("chunk um sobre entropia");
    ch[0].hash = xstrdup("aaaa1111"); ch[0].pagina_inicio = 1; ch[0].pagina_fim = 1; ch[0].num_tokens = 4;
    ch[1].texto = xstrdup("chunk dois sobre energia");
    ch[1].hash = xstrdup("bbbb2222"); ch[1].pagina_inicio = 2; ch[1].pagina_fim = 2; ch[1].num_tokens = 4;
    Embeddings *e = gerar_embeddings(ch, 2);
    QuestionGenConfig qc = {1, 1, 1};
    int nq = 0;
    PerguntaTipada *qs = gerar_perguntas(ch, 2, &qc, &nq);
    AmandaPackage pkg;
    memset(&pkg, 0, sizeof pkg);
    pkg.titulo = xstrdup("teste");
    pkg.autor = xstrdup("amanda-tests");
    pkg.data = xstrdup("2026-09-30");
    pkg.idioma = xstrdup("pt-BR");
    pkg.versao_app = xstrdup("1.0.1");
    pkg.chunks = ch; pkg.num_chunks = 2;
    pkg.embeddings = e;
    pkg.perguntas = qs; pkg.num_perguntas = nq;
    char *erro = NULL;
    const char *tmp = "amanda_test_roundtrip.tmp";
    int rc = empacotar_amanda(&pkg, tmp, &erro);
    CHECK(rc == 0, "empacota .amanda");
    AmandaPackage *back = carregar_amanda(tmp, &erro);
    CHECK(back != NULL, "carrega .amanda");
    if (back) {
        CHECK(back->num_chunks == 2, "roundtrip preserva chunks");
        CHECK(strcmp(back->chunks[0].texto, "chunk um sobre entropia") == 0, "texto intacto");
        liberar_package(back);
    }
    remove(tmp);
    free(pkg.titulo); free(pkg.autor); free(pkg.data);
    free(pkg.idioma); free(pkg.versao_app);
    liberar_chunks(ch, 2);
    liberar_embeddings(e);
    liberar_perguntas(qs, nq);
}

static void test_extractor(void) {
    printf("[extractor]\n");
    const char *p = "amanda_test_input.tmp";
    FILE *f = fopen(p, "w");
    CHECK(f != NULL, "cria txt temporario");
    if (f) {
        fputs("Primeiro paragrafo sobre termodinamica.\n\nSegundo paragrafo sobre entropia.\n", f);
        fclose(f);
    }
    char *erro = NULL;
    DocumentoExtraido *d = extrair_documento(p, &erro);
    CHECK(d && d->num_blocos >= 1, "extrai txt em blocos");
    if (d) liberar_documento(d);
    else { printf("  erro: %s\n", erro ? erro : "?"); free(erro); }
    remove(p);
}

static void test_eval(void) {
    printf("[eval]\n");
    DocumentoExtraido doc;
    memset(&doc, 0, sizeof doc);
    BlocoTexto bs[2];
    bs[0].texto = "A capital do Brasil e Brasilia, inaugurada em 1960. O congresso fica em Brasilia.";
    bs[0].pagina = 1; bs[0].x = bs[0].y = bs[0].largura = bs[0].altura = 0;
    bs[1].texto = "A fotossintese produz glicose nas plantas verdes com clorofila.";
    bs[1].pagina = 2; bs[1].x = bs[1].y = bs[1].largura = bs[1].altura = 0;
    doc.blocos = bs; doc.num_blocos = 2; doc.num_paginas = 2;
    int nc = 0;
    Chunk *ch = dividir_em_chunks(&doc, 180, 0, &nc);
    CHECK(ch && nc >= 1, "chunks para eval");
    Embeddings *e = gerar_embeddings(ch, nc);
    QuestionGenConfig qc = {1, 1, 2};
    int nq = 0;
    PerguntaTipada *qs = gerar_perguntas(ch, 2, &qc, &nq);
    CHECK(qs && nq > 0, "perguntas para eval");
    AmandaPackage pkg;
    memset(&pkg, 0, sizeof pkg);
    pkg.chunks = ch; pkg.num_chunks = nc;
    pkg.embeddings = e;
    pkg.perguntas = qs; pkg.num_perguntas = nq;
    EvalConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.sample = 1.0; cfg.seed = 42u; cfg.top_k = 2;
    EvalReport r;
    char *erro = NULL;
    int rc = eval_run(&pkg, &cfg, &r, &erro);
    CHECK(rc == 0, "eval_run ok");
    if (rc == 0) {
        CHECK(r.amostradas == nq, "amostra 100% cobre todas");
        CHECK(r.fidelidade >= 0.0 && r.fidelidade <= 1.0, "fidelidade em [0,1]");
        CHECK(r.lat_media_ms <= 500.0, "latencia media <=500ms");
        CHECK(r.n_probes == 3, "3 probes fora-escopo");
        char *j = eval_to_json(&r, "mem");
        CHECK(j && strstr(j, "fidelidade") != NULL, "json contem metricas");
        free(j);
    } else { printf("  erro: %s\n", erro ? erro : "?"); free(erro); }
    liberar_chunks(ch, nc);
    liberar_embeddings(e);
    liberar_perguntas(qs, nq);
}

static void test_laya_backend(void) {
    printf("[laya_backend]\n");
    const char *fake = "{\"message\":{\"message_id\":\"m1\",\"role\":\"assistant\","
                       "\"content\":\"A entropia cresce.\\nSegunda linha.\"}}";
    char *c = laya_extract_content(fake);
    CHECK(c && strstr(c, "A entropia cresce.") != NULL, "extrai content com unescape");
    CHECK(c && strstr(c, "\nSegunda linha.") != NULL, "unescape \\n vira quebra real");
    free(c);
    CHECK(laya_extract_content("{\"sem\":\"campo\"}") == NULL, "sem content retorna NULL");
    CHECK(laya_extract_content(NULL) == NULL, "NULL retorna NULL");
    /* engine inexistente: falha rapido com timeout curto, sem travar */
    char *out = NULL;
    char *err = NULL;
    long long t0 = now_ms();
    LayaStatus st = laya_chat("http://127.0.0.1:59999", "oi", 2000, &out, &err);
    long long dt = now_ms() - t0;
    CHECK(st == LAYA_UNAVAILABLE, "porta fechada = UNAVAILABLE");
    CHECK(dt < 15000, "fallback rapido (<15s)");
    CHECK(out == NULL, "sem conteudo em falha");
    free(err);
    CHECK(laya_providers_ready("http://127.0.0.1:59999", 1500) == 0, "providers_ready=0 sem engine");
    CHECK(laya_chat(NULL, NULL, 1000, &out, &err) == LAYA_ERR_ARGS, "args nulos = ERR_ARGS");
}

static void test_hibrida_fallback(void) {
    printf("[hibrida]\n");
    DocumentoExtraido doc;
    memset(&doc, 0, sizeof doc);
    BlocoTexto bs[1];
    bs[0].texto = "A capital do Brasil e Brasilia, inaugurada em 1960.";
    bs[0].pagina = 1; bs[0].x = bs[0].y = bs[0].largura = bs[0].altura = 0;
    doc.blocos = bs; doc.num_blocos = 1; doc.num_paginas = 1;
    int nc = 0;
    Chunk *ch = dividir_em_chunks(&doc, 180, 0, &nc);
    Embeddings *e = gerar_embeddings(ch, nc);
    DecisionConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.limiar_confianca = 0.7f; cfg.limiar_recusa = 0.3f; cfg.top_k = 2;
    cfg.backend = DECISION_BACKEND_LAYA_HTTP;
    strncpy(cfg.laya_url, "http://127.0.0.1:59999", sizeof cfg.laya_url - 1);
    cfg.laya_timeout_ms = 2000;
    int via = -1;
    Decisao *d = executar_decisao_hibrida("Qual e a capital do Brasil?", ch, nc, e, &cfg, &via);
    CHECK(d && via == 0, "sem engine laya: cai para local (via=0)");
    CHECK(d && d->resposta && strstr(d->resposta, "Brasilia") != NULL, "fallback local responde Brasilia");
    liberar_decisao(d);
    liberar_embeddings(e);
    liberar_chunks(ch, nc);
}

int main(void) {
    printf("amanda_tests %s\n", amanda_version());
    test_chunker();
    test_embedder();
    test_question_gen();
    test_decision();
    test_packager();
    test_extractor();
    test_eval();
    test_laya_backend();
    test_hibrida_fallback();
    printf("\nresultado: %d ok, %d falhas\n", passes, fails);
    return fails ? 1 : 0;
}
