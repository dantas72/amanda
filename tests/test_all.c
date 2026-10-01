#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "amanda.h"
#include "pdf_extractor.h"
#include "chunker.h"
#include "embedder.h"
#include "question_gen.h"
#include "decision_engine.h"
#include "laya_backend.h"
#include "packager.h"
#include "eval.h"
#include "calibra.h"
#include "config.h"
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
    ch.texto = "A fotossintese ocorre nos Cloroplastos das plantas. A clorofila absorve luz solar. A glicose e produzida a partir de agua e dioxido de carbono.";
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
    CHECK(has_choice, "gera choice");
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

static void test_calibra(void) {
    printf("[calibra]\n");
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
    Embeddings *e = gerar_embeddings(ch, nc);
    QuestionGenConfig qc;
    memset(&qc, 0, sizeof qc);
    qc.max_choice = 1; qc.max_score = 1; qc.max_noul = 2;
    int nq = 0;
    PerguntaTipada *qs = gerar_perguntas(ch, 2, &qc, &nq);
    CHECK(qs && nq > 0, "perguntas para calibra");
    AmandaPackage pkg;
    memset(&pkg, 0, sizeof pkg);
    pkg.chunks = ch; pkg.num_chunks = nc;
    pkg.embeddings = e;
    pkg.perguntas = qs; pkg.num_perguntas = nq;
    CalibraConfig cc;
    memset(&cc, 0, sizeof cc);
    cc.sample = 1.0; cc.seed = 42u;
    CalibraReport r;
    char *erro = NULL;
    int rc = calibra_run(&pkg, &cc, &r, &erro);
    CHECK(rc == 0, "calibra_run ok");
    if (rc == 0) {
        CHECK(r.n_pos == nq && r.n_neg == 3, "pos=todas, neg=3 probes");
        CHECK(r.sug_bal >= r.cur_bal - 1e-9, "sugerido nao piora bal");
        CHECK(r.sug_center >= 0.05f - 1e-4f && r.sug_center <= 0.60f + 1e-3f, "center na grade");
        CHECK(r.sug_slope >= 4.0f - 1e-4f && r.sug_slope <= 30.0f + 1e-3f, "slope na grade");
        CHECK(r.sug_limiar >= 0.10f - 1e-4f && r.sug_limiar <= 0.90f + 1e-3f, "limiar na grade");
        char *j = calibra_to_json(&r, "mem");
        CHECK(j && strstr(j, "sugerido") != NULL, "json contem sugerido");
        free(j);
    } else { printf("  erro: %s\n", erro ? erro : "?"); free(erro); }
    /* motor parametrizavel: slope custom muda a confianca; zeros = padrao */
    DecisionConfig d0, d1;
    memset(&d0, 0, sizeof d0);
    d0.limiar_recusa = 0.0f; d0.top_k = 2;
    memset(&d1, 0, sizeof d1);
    d1.limiar_recusa = 0.0f; d1.top_k = 2;
    d1.conf_center = 0.30f; d1.conf_slope = 6.0f;
    Decisao *a = executar_decisao("Qual e a capital do Brasil?", ch, nc, e, &d0);
    Decisao *b = executar_decisao("Qual e a capital do Brasil?", ch, nc, e, &d1);
    CHECK(a && b && fabsf(a->confianca - b->confianca) > 1e-4f, "slope custom muda confianca");
    Decisao *c = executar_decisao("Qual e a capital do Brasil?", ch, nc, e, NULL);
    CHECK(c && fabsf(a->confianca - c->confianca) < 1e-6f, "zeros = padrao historico");
    liberar_decisao(a); liberar_decisao(b); liberar_decisao(c);
    liberar_chunks(ch, nc);
    liberar_embeddings(e);
    liberar_perguntas(qs, nq);
}

static void test_serve_calib(void) {
    printf("[serve_calib]\n");
    DocumentoExtraido doc;
    memset(&doc, 0, sizeof doc);
    BlocoTexto bs[1];
    bs[0].texto = "A capital do Brasil e Brasilia, inaugurada em 1960.";
    bs[0].pagina = 1; bs[0].x = bs[0].y = bs[0].largura = bs[0].altura = 0;
    doc.blocos = bs; doc.num_blocos = 1; doc.num_paginas = 1;
    int nc = 0;
    Chunk *ch = dividir_em_chunks(&doc, 180, 0, &nc);
    Embeddings *e = gerar_embeddings(ch, nc);
    DecisionConfig base;
    memset(&base, 0, sizeof base);
    base.limiar_confianca = 0.7f; base.limiar_recusa = 0.3f; base.top_k = 3;
    float c0 = 0; int pg0 = 0;
    char *r0 = montar_resposta_chat("Qual e a capital do Brasil?", ch, nc, e, &base, &c0, &pg0);
    CHECK(r0 && c0 > 0.9f, "serve default: confianca alta in-scope");
    free(r0);
    /* limiar 1.0 recusa tudo; slope custom muda a confianca */
    DecisionConfig bloqueia = base;
    bloqueia.limiar_recusa = 1.0f;
    float c1 = 0; int pg1 = 0;
    char *r1 = montar_resposta_chat("Qual e a capital do Brasil?", ch, nc, e, &bloqueia, &c1, &pg1);
    CHECK(r1 && strstr(r1, "Nao encontrei") != NULL, "serve limiar 1.0 recusa");
    free(r1);
    DecisionConfig custom = base;
    custom.conf_center = 0.30f; custom.conf_slope = 6.0f;
    float c2 = 0; int pg2 = 0;
    char *r2 = montar_resposta_chat("Qual e a capital do Brasil?", ch, nc, e, &custom, &c2, &pg2);
    CHECK(r2 && fabsf(c2 - c0) > 1e-4f, "serve slope custom muda confianca");
    free(r2);
    liberar_embeddings(e);
    liberar_chunks(ch, nc);
}

static void test_config_templates(void) {
    printf("[config_templates]\n");
    const char *yp = "amanda_test_cfg.tmp";
    FILE *f = fopen(yp, "w");
    CHECK(f != NULL, "cria yaml temporario");
    if (f) {
        fputs("# comentario\nprojeto: \"X\"  # inline\ntitulo: Meu Titulo\n", f);
        fputs("pdf:\n  caminho: \"examples/exemplo.txt\"\n  idioma: \"pt-BR\"\n", f);
        fputs("chunking:\n  tamanho_max: 100\n  sobreposicao: 10\n", f);
        fputs("question_gen:\n  max_choice: 1\n  max_score: 1\n  max_noul: 2\n", f);
        fputs("decision_engine:\n  limiar_recusa: 0.5\n  chave_desconhecida: 99\n", f);
        fputs("templates:\n  dir: \"templates\"\n", f);
        fclose(f);
    }
    AmandaConfig ac;
    char *erro = NULL;
    int rc = config_ler(yp, &ac, &erro);
    CHECK(rc == 0, "config_ler ok");
    if (rc == 0) {
        CHECK(strcmp(ac.input, "examples/exemplo.txt") == 0, "input com aspas");
        CHECK(strcmp(ac.title, "Meu Titulo") == 0, "titulo nivel raiz");
        CHECK(ac.chunk_words == 100 && ac.overlap == 10, "chunking");
        CHECK(ac.max_choice == 1 && ac.max_noul == 2, "question_gen");
        CHECK(ac.tem_limiar && fabsf(ac.limiar_recusa - 0.5f) < 1e-6f, "limiar_recusa");
        CHECK(strcmp(ac.templates_dir, "templates") == 0, "templates dir");
    } else { printf("  erro: %s\n", erro ? erro : "?"); free(erro); }
    remove(yp);
    AmandaConfig dflt;
    CHECK(config_ler("arquivo-que-nao-existe.yaml", &dflt, &erro) != 0, "ausente = erro");
    free(erro); erro = NULL;

    /* templates: dir inexistente = fallback silencioso */
    QuestionTemplates t0;
    CHECK(carregar_templates("dir-que-nao-existe", &t0) == 0, "dir ausente = fallback");
    CHECK(t0.choice[0] != '\0' && t0.noul[0] != '\0', "fallback tem embutidos");

    /* templates vivos: arquivos temporarios com marcadores */
#ifdef _WIN32
    system("mkdir amanda_test_tpl >nul 2>nul");
#else
    system("mkdir -p amanda_test_tpl");
#endif
    FILE *t1 = fopen("amanda_test_tpl/question_choice.tpl", "w");
    FILE *t2 = fopen("amanda_test_tpl/question_score.tpl", "w");
    FILE *t3 = fopen("amanda_test_tpl/question_noul.tpl", "w");
    CHECK(t1 && t2 && t3, "cria tpl temporarios");
    if (t1) { fputs("{\"enunciado\": \"[C:{{trecho}}|p{{pagina}}]\"}", t1); fclose(t1); }
    if (t2) { fputs("formato solto [S:{{min}}-{{max}}]", t2); fclose(t2); }
    if (t3) { fputs("{\"enunciado\": \"[N:{{afirmacao}}]\"}", t3); fclose(t3); }
    QuestionTemplates tv;
    CHECK(carregar_templates("amanda_test_tpl", &tv) == 1, "carrega 3 tpl");
    Chunk ch;
    memset(&ch, 0, sizeof ch);
    ch.texto = "A fotossintese ocorre nos Cloroplastos das plantas verdes.";
    ch.hash = "h7654321"; ch.pagina_inicio = 3;
    QuestionGenConfig qc;
    memset(&qc, 0, sizeof qc);
    qc.max_choice = 1; qc.max_score = 1; qc.max_noul = 1;
    int nq = 0;
    PerguntaTipada *qs = gerar_perguntas_tpl(&ch, 1, &qc, &tv, &nq);
    CHECK(qs && nq == 3, "3 perguntas com templates vivos");
    if (qs && nq == 3) {
        int fc = 0, fs = 0, fn = 0;
        for (int i = 0; i < nq; i++) {
            if (strstr(qs[i].enunciado, "[C:")) fc = 1;
            if (strstr(qs[i].enunciado, "[S:0-10]")) fs = 1;
            if (strstr(qs[i].enunciado, "[N:")) fn = 1;
        }
        CHECK(fc && fs && fn, "marcadores renderizados por tipo");
    }
    liberar_perguntas(qs, nq);
    remove("amanda_test_tpl/question_choice.tpl");
    remove("amanda_test_tpl/question_score.tpl");
    remove("amanda_test_tpl/question_noul.tpl");
#ifdef _WIN32
    system("rmdir amanda_test_tpl >nul 2>nul");
#else
    system("rmdir amanda_test_tpl");
#endif
}

static void test_pdf_plus(void) {
    printf("[pdf_plus]\n");
    /* 1. TJ com espacamento + ' com quebra */
    {
        const char *p = "amanda_test_pdf_tj.tmp";
        FILE *f = fopen(p, "wb");
        CHECK(f != NULL, "cria pdf tj temporario");
        if (f) {
            const char *stream = "BT /F1 12 Tf [(Ola) -250 (mundo)] TJ ET\n"
                                 "BT (linha um) ' (linha dois) ' ET\n"
                                 "BT [(H)-20 (e)15 (llo)] TJ ET\n";
            fprintf(f, "%%PDF-1.4\n1 0 obj\n<< /Type /Page >>\nendobj\n"
                       "2 0 obj\n<< /Length %d >>\nstream\n", (int)strlen(stream));
            fwrite(stream, 1, strlen(stream), f);
            fputs("endstream\nendobj\ntrailer\n<< >>\n", f);
            fclose(f);
        }
        char *erro = NULL;
        DocumentoExtraido *d = extrair_pdf(p, &erro);
        CHECK(d && d->num_blocos >= 1, "tj extrai blocos");
        if (d) {
            char *full = documento_texto_completo(d);
            CHECK(full && strstr(full, "Ola") && strstr(full, "mundo"), "tj com espaco preserva palavras");
            CHECK(full && strstr(full, "linha um") && strstr(full, "linha dois"), "' extrai duas linhas");
            CHECK(full && strstr(full, "Hello") != NULL, "tj concatena fragmentos com kerning");
            CHECK(d->stats.text_streams >= 1, "stats contam streams de texto");
            free(full);
            liberar_documento(d);
        } else { printf("  erro: %s\n", erro ? erro : "?"); free(erro); }
        remove(p);
    }
    /* 2. ASCIIHexDecode via cadeia /Filter */
    {
        const char *p = "amanda_test_pdf_ahx.tmp";
        const char *plain = "BT (Ola HexAqui) Tj ET";
        FILE *f = fopen(p, "wb");
        CHECK(f != NULL, "cria pdf ahx temporario");
        if (f) {
            fprintf(f, "%%PDF-1.4\n1 0 obj\n<< /Type /Page >>\nendobj\n"
                       "2 0 obj\n<< /Length 999 /Filter /ASCIIHexDecode >>\nstream\n");
            for (const char *q = plain; *q; q++) fprintf(f, "%02X", (unsigned char)*q);
            fputs(">\nendstream\nendobj\ntrailer\n<< >>\n", f);
            fclose(f);
        }
        char *erro = NULL;
        DocumentoExtraido *d = extrair_pdf(p, &erro);
        CHECK(d != NULL, "ahx decodifica");
        if (d) {
            char *full = documento_texto_completo(d);
            CHECK(full && strstr(full, "Ola HexAqui"), "ahx preserva texto");
            free(full);
            liberar_documento(d);
        } else { printf("  erro: %s\n", erro ? erro : "?"); free(erro); }
        remove(p);
    }
    /* 3. ASCII85Decode gerado em runtime + tolerancia a stream quebrada */
    {
        const char *p = "amanda_test_pdf_a85.tmp";
        const char *plain = "BT (Ola A85Vivo) Tj ET";
        /* codifica ASCII85 aqui para nao depender de vetor fixo */
        char enc[1024]; size_t epos = 0;
        size_t plen = strlen(plain);
        for (size_t k = 0; k < plen; k += 4) {
            unsigned int tup = 0;
            int nb = 0;
            for (int b = 0; b < 4; b++) {
                tup <<= 8;
                if (k + (size_t)b < plen) { tup |= (unsigned char)plain[k + b]; nb++; }
            }
            if (nb == 4 && tup == 0) { enc[epos++] = 'z'; }
            else {
                char grp[5];
                for (int b = 4; b >= 0; b--) { grp[b] = (char)(tup % 85u + '!'); tup /= 85u; }
                for (int b = 0; b < nb + 1; b++) enc[epos++] = grp[b];
            }
        }
        enc[epos] = '\0';
        FILE *f = fopen(p, "wb");
        CHECK(f != NULL, "cria pdf a85 temporario");
        if (f) {
            const char *good_fmt = "%%PDF-1.4\n1 0 obj\n<< /Type /Page >>\nendobj\n"
                "2 0 obj\n<< /Length 999 /Filter /ASCII85Decode >>\nstream\n%s~>\nendstream\nendobj\n";
            fprintf(f, good_fmt, enc);
            /* stream quebrada com FlateDecode invalido: deve ser tolerada */
            fputs("3 0 obj\n<< /Length 20 /Filter /FlateDecode >>\nstream\n", f);
            fwrite("isto-nao-e-deflate-valido", 1, 26, f);
            fputs("\nendstream\nendobj\ntrailer\n<< >>\n", f);
            fclose(f);
        }
        char *erro = NULL;
        DocumentoExtraido *d = extrair_pdf(p, &erro);
        CHECK(d != NULL, "a85 + stream quebrada tolerada");
        if (d) {
            char *full = documento_texto_completo(d);
            CHECK(full && strstr(full, "Ola A85Vivo"), "a85 preserva texto");
            CHECK((d->stats.failed_inflate + d->stats.failed_decode) >= 1, "falha contabilizada");
            free(full);
            liberar_documento(d);
        } else { printf("  erro: %s\n", erro ? erro : "?"); free(erro); }
        remove(p);
    }
    /* 4. cobertura chega ao eval json */
    {
        Chunk *ch = (Chunk *)xcalloc(1, sizeof(Chunk));
        ch[0].texto = xstrdup("cobertura de extracao com pagina e streams");
        ch[0].hash = xstrdup("cccc3333"); ch[0].pagina_inicio = 1; ch[0].pagina_fim = 2; ch[0].num_tokens = 6;
        Embeddings *e = gerar_embeddings(ch, 1);
        QuestionGenConfig qc = {1, 1, 1};
        int nq = 0;
        PerguntaTipada *qs = gerar_perguntas(ch, 1, &qc, &nq);
        AmandaPackage pkg;
        memset(&pkg, 0, sizeof pkg);
        pkg.titulo = xstrdup("cob"); pkg.autor = xstrdup("t");
        pkg.data = xstrdup("2026-10-01"); pkg.idioma = xstrdup("pt-BR");
        pkg.versao_app = xstrdup("1.0.11");
        pkg.chunks = ch; pkg.num_chunks = 1;
        pkg.embeddings = e; pkg.perguntas = qs; pkg.num_perguntas = nq;
        pkg.num_paginas = 2; pkg.extra_blocos = 3;
        pkg.extra_total_streams = 5; pkg.extra_text_streams = 4;
        pkg.extra_failed = 1; pkg.extra_fallback = 0;
        const char *tmp = "amanda_test_cov.tmp";
        char *erro = NULL;
        CHECK(empacotar_amanda(&pkg, tmp, &erro) == 0, "empacota v2 com cobertura");
        AmandaPackage *back = carregar_amanda(tmp, &erro);
        CHECK(back && back->num_paginas == 2, "roundtrip v2 preserva paginas");
        if (back) {
            EvalConfig cfg; memset(&cfg, 0, sizeof cfg);
            cfg.sample = 1.0; cfg.seed = 42u; cfg.top_k = 2;
            EvalReport r;
            CHECK(eval_run(back, &cfg, &r, &erro) == 0, "eval com cobertura ok");
            char *j = eval_to_json(&r, tmp);
            CHECK(j && strstr(j, "cobertura") && strstr(j, "streams_texto") , "json tem cobertura");
            free(j);
            liberar_package(back);
        }
        remove(tmp);
        free(pkg.titulo); free(pkg.autor); free(pkg.data);
        free(pkg.idioma); free(pkg.versao_app);
        liberar_chunks(ch, 1);
        liberar_embeddings(e);
        liberar_perguntas(qs, nq);
    }
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
    test_calibra();
    test_serve_calib();
    test_config_templates();
    test_pdf_plus();
    printf("\nresultado: %d ok, %d falhas\n", passes, fails);
    return fails ? 1 : 0;
}
