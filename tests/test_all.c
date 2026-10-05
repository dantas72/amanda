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
#include "mcp.h"
#include "typesafe_backend.h"
#include "deepseek_backend.h"
#include "server.h"
#include "utils.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>
typedef SOCKET t75_sock;
#define T75_INVALID INVALID_SOCKET
#define t75_close closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
typedef int t75_sock;
#define T75_INVALID -1
#define t75_close close
#endif

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
        char *sj = package_stats_json(back);
        CHECK(sj && strstr(sj, "\"chunks\":2") != NULL, "inspect json tem chunks");
        CHECK(sj && strstr(sj, "\"formato\":3") != NULL, "inspect json tem formato");
        CHECK(sj && strstr(sj, "\"extracao\"") != NULL, "inspect json tem extracao");
        free(sj);
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
        fputs("servidor:\n  porta: 8181\n  host: \"0.0.0.0\"\n  workers: 4\n  backend: \"local\"\n  api_key: \"srvK\"\n", f);
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
        CHECK(ac.tem_servidor && ac.srv_port == 8181, "servidor porta");
        CHECK(strcmp(ac.srv_host, "0.0.0.0") == 0, "servidor host");
        CHECK(ac.srv_workers == 4, "servidor workers");
        CHECK(strcmp(ac.srv_backend, "local") == 0, "servidor backend");
        CHECK(strcmp(ac.srv_api_key, "srvK") == 0, "servidor api_key");
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

/* ============ Fase 7.5: servidor robusto com sockets reais ============ */

#define T75_PORT 18081
#define T75_KEY "k-teste-75"

static int g_t75_port = T75_PORT;

static char *t75_request(const char *req, size_t reqlen) {
    t75_sock s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == T75_INVALID) return NULL;
#ifdef _WIN32
    {
        DWORD ms = 10000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&ms, sizeof ms);
    }
#else
    {
        struct timeval tv;
        tv.tv_sec = 10; tv.tv_usec = 0;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    }
#endif
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)g_t75_port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(s, (struct sockaddr *)&a, sizeof a) != 0) { t75_close(s); return NULL; }
    size_t sent = 0;
    while (sent < reqlen) {
        int r = send(s, req + sent, (int)(reqlen - sent), 0);
        if (r <= 0) { t75_close(s); return NULL; }
        sent += (size_t)r;
    }
    ByteBuf b; buf_init(&b);
    char tmp[8192];
    for (;;) {
        int r = recv(s, tmp, sizeof tmp, 0);
        if (r <= 0) break;
        buf_append(&b, tmp, (size_t)r);
    }
    t75_close(s);
    buf_reserve(&b, 1);
    b.data[b.len] = '\0';
    return (char *)b.data;
}

static char *t75_call(const char *method, const char *path, const char *auth, const char *json) {
    ByteBuf req; buf_init(&req);
    char head[1024];
    size_t bl = json ? strlen(json) : 0;
    snprintf(head, sizeof head,
        "%s %s HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n"
        "Content-Type: application/json\r\nContent-Length: %lu\r\n%s%s\r\n\r\n",
        method, path, (unsigned long)bl,
        auth ? "Authorization: Bearer " : "", auth ? auth : "");
    buf_append_cstr(&req, head);
    if (bl) buf_append(&req, json, bl);
    char *resp = t75_request((char *)req.data, req.len);
    buf_free(&req);
    return resp;
}

#ifdef _WIN32
static unsigned __stdcall t75_srv(void *p) { server_run((const ServerConfig *)p); return 0; }
static unsigned __stdcall t75_cli(void *p) {
    char **slot = (char **)p;
    slot[0] = t75_call("POST", "/v1/chat/completions", T75_KEY,
        "{\"model\":\"amanda\",\"messages\":[{\"role\":\"user\",\"content\":\"capital?\"}]}");
    return 0;
}
#else
static void *t75_srv(void *p) { server_run((const ServerConfig *)p); return NULL; }
static void *t75_cli(void *p) {
    char **slot = (char **)p;
    slot[0] = t75_call("POST", "/v1/chat/completions", T75_KEY,
        "{\"model\":\"amanda\",\"messages\":[{\"role\":\"user\",\"content\":\"capital?\"}]}");
    return NULL;
}
#endif

static void test_serve_75(void) {
    printf("[serve_75]\n");
    /* pacote em memoria com 60 perguntas (testa teto do /v1/eval) */
    Chunk *ch = (Chunk *)xcalloc(1, sizeof(Chunk));
    ch[0].texto = xstrdup("A capital do Brasil e Brasilia, inaugurada em 1960. O congresso fica em Brasilia.");
    ch[0].hash = xstrdup("eeee4444"); ch[0].pagina_inicio = 1; ch[0].pagina_fim = 1; ch[0].num_tokens = 14;
    Embeddings *e = gerar_embeddings(ch, 1);
    int nq = 60;
    PerguntaTipada *qs = (PerguntaTipada *)xcalloc((size_t)nq, sizeof(PerguntaTipada));
    for (int i = 0; i < nq; i++) {
        qs[i].tipo = (TipoPergunta)(i % 3);
        qs[i].enunciado = xstrdup("Qual e a capital do Brasil segundo o documento de teste?");
        qs[i].pagina_fonte = 1;
        qs[i].chunk_hash = xstrdup("eeee4444");
    }
    AmandaPackage *pkg = (AmandaPackage *)xcalloc(1, sizeof(*pkg));
    pkg->titulo = xstrdup("t75"); pkg->autor = xstrdup("t");
    pkg->data = xstrdup("2026-10-01"); pkg->idioma = xstrdup("pt-BR");
    pkg->versao_app = xstrdup("1.0.15");
    pkg->chunks = ch; pkg->num_chunks = 1;
    pkg->embeddings = e; pkg->perguntas = qs; pkg->num_perguntas = nq;
    pkg->num_paginas = 1; pkg->extra_blocos = 1;

    volatile int stop = 0;
    ServerConfig sc;
    memset(&sc, 0, sizeof sc);
    sc.host = "127.0.0.1"; sc.port = T75_PORT; sc.pkg = pkg; sc.stop_flag = &stop;
    sc.cors_origin = "http://teste.local";
    sc.api_key = T75_KEY;
    sc.max_body = 1024;
    sc.max_conns = 8;
    sc.eval_max = 50;

#ifdef _WIN32
    uintptr_t th = _beginthreadex(NULL, 0, t75_srv, &sc, 0, NULL);
    CHECK(th != 0, "sobe servidor 7.5 em thread");
#else
    pthread_t th;
    CHECK(pthread_create(&th, NULL, t75_srv, &sc) == 0, "sobe servidor 7.5 em thread");
#endif
    /* espera pronto (ate ~10s) */
    char *ready = NULL;
    for (int i = 0; i < 100 && !ready; i++) {
        char *r = t75_call("GET", "/v1/models", T75_KEY, NULL);
        if (r && strstr(r, "200 OK")) ready = r;
        else { free(r); sleep_ms(100); }
    }
    CHECK(ready && strstr(ready, "\"amanda\""), "models com auth ok");
    CHECK(ready && strstr(ready, "Access-Control-Allow-Origin: http://teste.local"), "cors configurado ecoa");
    free(ready);

    {
        char *r = t75_call("GET", "/v1/models", NULL, NULL);
        CHECK(r && strstr(r, "401"), "sem auth = 401");
        free(r);
    }
    {
        char *r = t75_call("GET", "/v1/models", "chave-errada", NULL);
        CHECK(r && strstr(r, "401"), "auth errada = 401");
        free(r);
    }
    {
        char *r = t75_call("OPTIONS", "/v1/chat/completions", NULL, NULL);
        CHECK(r && strstr(r, "204") && strstr(r, "Authorization"), "preflight 204 com Authorization");
        free(r);
    }
    {
        /* corpo ~2KB > max_body 1024 -> 413 */
        ByteBuf big; buf_init(&big);
        buf_append_cstr(&big, "{\"model\":\"amanda\",\"messages\":[{\"role\":\"user\",\"content\":\"");
        for (int i = 0; i < 2000; i++) buf_append(&big, "x", 1);
        buf_append_cstr(&big, "\"}]}");
        buf_reserve(&big, 1); big.data[big.len] = '\0';
        char *r = t75_call("POST", "/v1/chat/completions", T75_KEY, (char *)big.data);
        CHECK(r && strstr(r, "413"), "body gigante = 413");
        free(r);
        buf_free(&big);
    }
    {
        char *r = t75_call("POST", "/v1/chat/completions", T75_KEY,
            "{\"model\":\"amanda\",\"messages\":[{\"role\":\"user\",\"content\":\"Qual e a capital?\"}]}");
        CHECK(r && strstr(r, "200 OK") && strstr(r, "Brasilia"), "chat com auth responde");
        free(r);
    }
    {
        char *r = t75_call("POST", "/v1/eval", T75_KEY, "{\"sample\":0.05}");
        CHECK(r && strstr(r, "200 OK") && strstr(r, "cobertura"), "eval pequeno = 200 com cobertura");
        free(r);
    }
    {
        /* 60 amostradas > eval_max 50 -> 400 honesto */
        char *r = t75_call("POST", "/v1/eval", T75_KEY, "{\"sample\":1.0}");
        CHECK(r && strstr(r, "400") && strstr(r, "teto"), "eval gigante = 400 com teto");
        free(r);
    }
    {
        /* concorrencia: 4 chats paralelos, todos 200 */
        char *rs[4] = {NULL, NULL, NULL, NULL};
#ifdef _WIN32
        uintptr_t hs[4];
        for (int i = 0; i < 4; i++) hs[i] = _beginthreadex(NULL, 0, t75_cli, &rs[i], 0, NULL);
        for (int i = 0; i < 4; i++) {
            CHECK(hs[i] != 0, "thread cliente sobe");
            if (hs[i]) { WaitForSingleObject((HANDLE)hs[i], 15000); CloseHandle((HANDLE)hs[i]); }
        }
#else
        pthread_t hs[4];
        for (int i = 0; i < 4; i++) hs[i] = 0;
        for (int i = 0; i < 4; i++)
            CHECK(pthread_create(&hs[i], NULL, t75_cli, &rs[i]) == 0, "thread cliente sobe");
        for (int i = 0; i < 4; i++) if (hs[i]) pthread_join(hs[i], NULL);
#endif
        int allok = 1;
        for (int i = 0; i < 4; i++) {
            if (!rs[i] || !strstr(rs[i], "200 OK")) allok = 0;
            free(rs[i]);
        }
        CHECK(allok, "4 chats paralelos = 200");
    }

    stop = 1;
    {
        /* acorda o accept para o loop ver o stop */
        char *r = t75_call("GET", "/v1/models", T75_KEY, NULL);
        free(r);
    }
#ifdef _WIN32
    WaitForSingleObject((HANDLE)th, 15000);
    CloseHandle((HANDLE)th);
#else
    pthread_join(th, NULL);
#endif
    liberar_package(pkg);
}

/* ============ Fase 10: limiares TJ + teto do eval ============ */

static char *f10_extract(const char *stream_content) {
    const char *p = "amanda_test_f10.tmp";
    FILE *f = fopen(p, "wb");
    if (!f) return NULL;
    fprintf(f, "%%PDF-1.4\n1 0 obj\n<< /Type /Page >>\nendobj\n"
               "2 0 obj\n<< /Length %d >>\nstream\n",
            (int)strlen(stream_content));
    fwrite(stream_content, 1, strlen(stream_content), f);
    fputs("endstream\nendobj\ntrailer\n<< >>\n", f);
    fclose(f);
    char *erro = NULL;
    DocumentoExtraido *d = extrair_pdf(p, &erro);
    free(erro);
    remove(p);
    if (!d) return NULL;
    char *full = documento_texto_completo(d);
    liberar_documento(d);
    return full;
}

static void test_fase10(void) {
    printf("[fase10]\n");
    pdf_tj_config(-100.0f, 500.0f);
    {
        char *t = f10_extract("BT [(A) -50 (B)] TJ ET\n");
        CHECK(t && strstr(t, "AB") != NULL, "tj default: kern -50 junta");
        free(t);
    }
    {
        char *t = f10_extract("BT [(A) -50 (B)] TJ ET\n");
        pdf_tj_config(-10.0f, 500.0f);
        char *t2 = f10_extract("BT [(A) -50 (B)] TJ ET\n");
        pdf_tj_config(-100.0f, 500.0f);
        CHECK(t2 && strstr(t2, "A B") != NULL, "tj espaco custom separa");
        free(t); free(t2);
    }
    {
        char *t = f10_extract("BT [(A) 600 (B)] TJ ET\n");
        CHECK(t && strstr(t, "A B") != NULL, "tj default: salto 600 separa");
        free(t);
        pdf_tj_config(-100.0f, 10000.0f);
        t = f10_extract("BT [(A) 600 (B)] TJ ET\n");
        pdf_tj_config(-100.0f, 500.0f);
        CHECK(t && strstr(t, "AB") != NULL, "tj salto custom junta");
        free(t);
    }
    /* teto do eval: sample 1.0, max 4 -> 4 */
    {
        Chunk *ch = (Chunk *)xcalloc(1, sizeof(Chunk));
        ch[0].texto = xstrdup("A capital do Brasil e Brasilia, inaugurada em 1960. O congresso nacional fica em Brasilia. A cidade tem palacios e monumentos famosos.");
        ch[0].hash = xstrdup("ffff5555"); ch[0].pagina_inicio = 1; ch[0].pagina_fim = 1; ch[0].num_tokens = 24;
        Embeddings *e = gerar_embeddings(ch, 1);
        QuestionGenConfig qc = {2, 2, 6};
        int nq = 0;
        PerguntaTipada *qs = gerar_perguntas(ch, 1, &qc, &nq);
        CHECK(qs && nq >= 5, "perguntas para teto");
        AmandaPackage pkg;
        memset(&pkg, 0, sizeof pkg);
        pkg.chunks = ch; pkg.num_chunks = 1;
        pkg.embeddings = e; pkg.perguntas = qs; pkg.num_perguntas = nq;
        EvalConfig cfg;
        memset(&cfg, 0, sizeof cfg);
        cfg.sample = 1.0; cfg.seed = 42u; cfg.top_k = 2; cfg.max_amostras = 4;
        EvalReport r;
        char *erro = NULL;
        CHECK(eval_run(&pkg, &cfg, &r, &erro) == 0, "eval com teto ok");
        CHECK(r.amostradas == 4, "teto limita amostradas");
        free(erro);
        liberar_chunks(ch, 1);
        liberar_embeddings(e);
        liberar_perguntas(qs, nq);
    }
}

/* ============ Fase 11: serve com LLM vivo (stub Laya) ============ */

#define T11_SERVE_PORT 18082
#define T11_STUB_PORT 18083

static volatile int stub_stop = 0;
static int stub_delay_ms = 0;
static int stub_cur = 0, stub_max = 0;
#ifdef _WIN32
static CRITICAL_SECTION stub_cs;
static int stub_once = 0;
static void stub_lock(void) {
    if (!stub_once) { InitializeCriticalSection(&stub_cs); stub_once = 1; }
    EnterCriticalSection(&stub_cs);
}
static void stub_unlock(void) { LeaveCriticalSection(&stub_cs); }
#else
static pthread_mutex_t stub_mtx = PTHREAD_MUTEX_INITIALIZER;
static void stub_lock(void) { pthread_mutex_lock(&stub_mtx); }
static void stub_unlock(void) { pthread_mutex_unlock(&stub_mtx); }
#endif

/* Atende um POST /chat do stub com a resposta fixa (conta concorrencia). */
#ifdef _WIN32
static unsigned __stdcall stub_conn(void *p) {
#else
static void *stub_conn(void *p) {
#endif
    t75_sock fd = (t75_sock)(intptr_t)p;
    stub_lock(); stub_cur++; if (stub_cur > stub_max) stub_max = stub_cur; stub_unlock();
    /* le cabecalho + corpo (Content-Length) sem interpretar */
    char hb[32768];
    int got = 0;
    int clen = 0;
    while (got < (int)sizeof(hb) - 1) {
        int r = recv(fd, hb + got, (int)sizeof(hb) - 1 - got, 0);
        if (r <= 0) break;
        got += r;
        hb[got] = '\0';
        char *he = strstr(hb, "\r\n\r\n");
        if (he) {
            char *cl = strstr(hb, "Content-Length:");
            if (cl) clen = atoi(cl + 15);
            int hlen = (int)(he + 4 - hb);
            int have = got - hlen;
            while (have < clen) {
                r = recv(fd, hb, sizeof(hb), 0);
                if (r <= 0) break;
                have += r;
            }
            break;
        }
    }
    if (stub_delay_ms > 0) sleep_ms(stub_delay_ms);
    static const char body[] =
        "{\"message\":{\"message_id\":\"m1\",\"role\":\"assistant\","
        "\"content\":\"RESPOSTA-STUB-VIVA\"}}";
    char head[256];
    snprintf(head, sizeof head,
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
        "Content-Length: %lu\r\nConnection: close\r\n\r\n",
        (unsigned long)(sizeof(body) - 1));
    send(fd, head, (int)strlen(head), 0);
    send(fd, body, (int)(sizeof(body) - 1), 0);
    t75_close(fd);
    stub_lock(); if (stub_cur > 0) stub_cur--; stub_unlock();
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

#ifdef _WIN32
static unsigned __stdcall stub_srv(void *p) {
#else
static void *stub_srv(void *p) {
#endif
    (void)p;
    t75_sock srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv == T75_INVALID) return 0;
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof opt);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)T11_STUB_PORT);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(srv, (struct sockaddr *)&a, sizeof a) != 0) { t75_close(srv); return 0; }
    if (listen(srv, 16) != 0) { t75_close(srv); return 0; }
    while (!stub_stop) {
        struct sockaddr_in cli;
#ifdef _WIN32
        int cl = sizeof cli;
#else
        socklen_t cl = sizeof cli;
#endif
        t75_sock fd = accept(srv, (struct sockaddr *)&cli, &cl);
        if (fd == T75_INVALID) {
            if (stub_stop) break;
            continue;
        }
        if (stub_stop) { t75_close(fd); break; }
#ifdef _WIN32
        uintptr_t h = _beginthreadex(NULL, 0, stub_conn, (void *)(intptr_t)fd, 0, NULL);
        if (h == 0) { t75_close(fd); } else CloseHandle((HANDLE)h);
#else
        pthread_t th;
        if (pthread_create(&th, NULL, stub_conn, (void *)(intptr_t)fd) != 0) t75_close(fd);
        else pthread_detach(th);
#endif
    }
    t75_close(srv);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static void stub_start(void) {
    stub_stop = 0;
#ifdef _WIN32
    _beginthreadex(NULL, 0, stub_srv, NULL, 0, NULL);
#else
    pthread_t th;
    pthread_create(&th, NULL, stub_srv, NULL);
    pthread_detach(th);
#endif
    /* espera o listen (ate ~5s): sem isso o 1o chat cai em fallback */
    for (int i = 0; i < 50; i++) {
        t75_sock s = socket(AF_INET, SOCK_STREAM, 0);
        if (s != T75_INVALID) {
            struct sockaddr_in a;
            memset(&a, 0, sizeof a);
            a.sin_family = AF_INET;
            a.sin_port = htons((unsigned short)T11_STUB_PORT);
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            int ok = connect(s, (struct sockaddr *)&a, sizeof a);
            t75_close(s);
            if (ok == 0) break;
        }
        sleep_ms(100);
    }
}

static void stub_halt(void) {
    stub_stop = 1;
    /* acorda o accept */
    t75_sock s = socket(AF_INET, SOCK_STREAM, 0);
    if (s != T75_INVALID) {
        struct sockaddr_in a;
        memset(&a, 0, sizeof a);
        a.sin_family = AF_INET;
        a.sin_port = htons((unsigned short)T11_STUB_PORT);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        connect(s, (struct sockaddr *)&a, sizeof a);
        t75_close(s);
    }
    sleep_ms(400);
}

static void test_serve_llm(void) {
    printf("[serve_llm]\n");
    Chunk *ch = (Chunk *)xcalloc(1, sizeof(Chunk));
    ch[0].texto = xstrdup("A capital do Brasil e Brasilia, inaugurada em 1960.");
    ch[0].hash = xstrdup("dddd1111"); ch[0].pagina_inicio = 1; ch[0].pagina_fim = 1; ch[0].num_tokens = 9;
    Embeddings *e = gerar_embeddings(ch, 1);
    QuestionGenConfig qc = {1, 1, 1};
    int nq = 0;
    PerguntaTipada *qs = gerar_perguntas(ch, 1, &qc, &nq);
    AmandaPackage *pkg = (AmandaPackage *)xcalloc(1, sizeof(*pkg));
    pkg->titulo = xstrdup("t11"); pkg->autor = xstrdup("t");
    pkg->data = xstrdup("2026-10-01"); pkg->idioma = xstrdup("pt-BR");
    pkg->versao_app = xstrdup("1.0.18");
    pkg->chunks = ch; pkg->num_chunks = 1;
    pkg->embeddings = e; pkg->perguntas = qs; pkg->num_perguntas = nq;
    pkg->num_paginas = 1; pkg->extra_blocos = 1;

    g_t75_port = T11_SERVE_PORT;

    /* Serve primeiro (server_run faz WSAStartup no Windows); o stub
       precisa do WSA ativo para socket()/connect(). */
    volatile int stop = 0;
    ServerConfig sc;
    memset(&sc, 0, sizeof sc);
    sc.host = "127.0.0.1"; sc.port = T11_SERVE_PORT; sc.pkg = pkg; sc.stop_flag = &stop;
    sc.backend = DECISION_BACKEND_LAYA_HTTP;
    snprintf(sc.laya_url, sizeof sc.laya_url, "http://127.0.0.1:%d", T11_STUB_PORT);
    sc.laya_timeout_ms = 8000;
    sc.laya_max = 2;
#ifdef _WIN32
    uintptr_t th = _beginthreadex(NULL, 0, t75_srv, &sc, 0, NULL);
    CHECK(th != 0, "serve laya sobe");
#else
    pthread_t th;
    CHECK(pthread_create(&th, NULL, t75_srv, &sc) == 0, "serve laya sobe");
#endif
    char *ready = NULL;
    for (int i = 0; i < 100 && !ready; i++) {
        char *r = t75_call("GET", "/v1/models", NULL, NULL);
        if (r && strstr(r, "200 OK")) ready = r;
        else { free(r); sleep_ms(100); }
    }
    CHECK(ready != NULL, "serve laya responde");
    free(ready);

    /* --- fase A: stub vivo com atraso (testa caminho vivo + teto) --- */
    stub_delay_ms = 400;
    stub_cur = 0; stub_max = 0;
    stub_start();

    {
        char *r = t75_call("POST", "/v1/chat/completions", NULL,
            "{\"model\":\"amanda\",\"messages\":[{\"role\":\"user\",\"content\":\"Qual e a capital?\"}]}");
        CHECK(r && strstr(r, "RESPOSTA-STUB-VIVA"), "chat usa LLM vivo");
        CHECK(r && strstr(r, "laya-http"), "chat informa backend laya-http");
        free(r);
    }
    {
        char *r = t75_call("POST", "/v1/decisions", NULL, "{\"pergunta\":\"Qual e a capital?\"}");
        CHECK(r && strstr(r, "laya-http"), "decisions informa backend laya-http");
        free(r);
    }
    {
        /* 4 paralelos com stub lento: todos 200 e stub viu no max 2 */
        char *rs[4] = {NULL, NULL, NULL, NULL};
#ifdef _WIN32
        uintptr_t hs[4];
        for (int i = 0; i < 4; i++) hs[i] = _beginthreadex(NULL, 0, t75_cli, &rs[i], 0, NULL);
        for (int i = 0; i < 4; i++) {
            if (hs[i]) { WaitForSingleObject((HANDLE)hs[i], 30000); CloseHandle((HANDLE)hs[i]); }
        }
#else
        pthread_t hs[4];
        for (int i = 0; i < 4; i++) hs[i] = 0;
        for (int i = 0; i < 4; i++) pthread_create(&hs[i], NULL, t75_cli, &rs[i]);
        for (int i = 0; i < 4; i++) if (hs[i]) pthread_join(hs[i], NULL);
#endif
        int allok = 1;
        for (int i = 0; i < 4; i++) {
            if (!rs[i] || !strstr(rs[i], "200 OK")) allok = 0;
            free(rs[i]);
        }
        CHECK(allok, "4 chats paralelos com LLM = 200");
        stub_lock();
        int mx = stub_max;
        stub_unlock();
        CHECK(mx <= 2, "teto LLM respeitado no stub");
    }

    /* --- fase B: stub morto -> fallback local honesto --- */
    stub_halt();
    {
        char *r = t75_call("POST", "/v1/chat/completions", NULL,
            "{\"model\":\"amanda\",\"messages\":[{\"role\":\"user\",\"content\":\"Qual e a capital?\"}]}");
        CHECK(r && strstr(r, "Brasilia"), "sem engine: fallback local");
        CHECK(r && strstr(r, "\"backend\":\"local\""), "fallback informa backend local");
        free(r);
    }

    stop = 1;
    {
        char *r = t75_call("GET", "/v1/models", NULL, NULL);
        free(r);
    }
#ifdef _WIN32
    WaitForSingleObject((HANDLE)th, 20000);
    CloseHandle((HANDLE)th);
#else
    pthread_join(th, NULL);
#endif
    liberar_package(pkg);
    g_t75_port = T75_PORT;
}

static void test_fase12_retrieval(void) {
    printf("[fase12-retrieval]\n");
    {
        int n = -1;
        char **t = tokenizar("o de para com", &n);
        CHECK(n == 0, "stopwords puras geram zero tokens");
        liberar_tokens(t, n);
    }
    {
        int n = 0;
        char **t = tokenizar("Coracao e arvore", &n);
        int has_cor = 0, has_arv = 0;
        for (int i = 0; i < n; i++) {
            if (strcmp(t[i], "coracao") == 0) has_cor = 1;
            if (strcmp(t[i], "arvore") == 0) has_arv = 1;
        }
        CHECK(has_cor && has_arv, "acentos dobrados para ascii");
        liberar_tokens(t, n);
    }
    {
        int n = 0;
        /* acento de verdade em UTF-8: cora\xc3\xa7\xc3\xa3o */
        char **t = tokenizar("cora\xc3\xa7\xc3\xa3o cl\xc3\xa1usula", &n);
        int has_cor = 0, has_cl = 0;
        for (int i = 0; i < n; i++) {
            if (strcmp(t[i], "coracao") == 0) has_cor = 1;
            if (strcmp(t[i], "clausula") == 0) has_cl = 1;
        }
        CHECK(has_cor && has_cl, "utf-8 acentuado normaliza");
        liberar_tokens(t, n);
    }
    {
        Chunk ch[3];
        memset(ch, 0, sizeof ch);
        ch[0].texto = "contrato de adesao clausula penal multa rescisoria"; ch[0].pagina_inicio = 1;
        ch[1].texto = "o gato sentou no tapete fofo"; ch[1].pagina_inicio = 2;
        ch[2].texto = "o gato correu no parque verde"; ch[2].pagina_inicio = 3;
        Embeddings *e = gerar_embeddings(ch, 3);
        int nout = 0;
        RankItem *rk = recuperar_chunks("clausula penal", ch, 3, e, 3, &nout);
        CHECK(rk && nout == 3 && rk[0].indice_chunk == 0, "bm25 prefere termo raro");
        if (rk) free(rk);
        /* query com acento casa com indice sem acento */
        rk = recuperar_chunks("cl\xc3\xa1usula penal", ch, 3, e, 3, &nout);
        CHECK(rk && rk[0].indice_chunk == 0, "query acentuada casa com indice");
        if (rk) free(rk);
        rk = recuperar_chunks("o de e", ch, 3, e, 3, &nout);
        CHECK(rk && rk[0].score < 0.001f, "stopwords puras zeram score");
        if (rk) free(rk);
        liberar_embeddings(e);
    }
    {
        Chunk ch[2];
        memset(ch, 0, sizeof ch);
        ch[0].texto = "contrato assinado entre partes com clausula penal expressa"; ch[0].pagina_inicio = 1;
        ch[1].texto = "contrato registrado em cartorio com testemunhas presentes"; ch[1].pagina_inicio = 2;
        Embeddings *e = gerar_embeddings(ch, 2);
        DecisionConfig cfg;
        memset(&cfg, 0, sizeof cfg);
        cfg.limiar_confianca = 0.7f; cfg.limiar_recusa = 0.0f; cfg.top_k = 2;
        Decisao *d = executar_decisao("contrato", ch, 2, e, &cfg);
        CHECK(d && d->citacao && strstr(d->citacao, "[p.1]") && strstr(d->citacao, "[p.2]"),
              "citacao multi top-k com 2 paginas");
        CHECK(d && d->resposta && (strstr(d->resposta, "p. 1, 2") != NULL || strstr(d->resposta, "p. 2, 1") != NULL),
              "resposta indica 2 paginas");
        liberar_decisao(d);
        liberar_embeddings(e);
    }
}

static void test_fase123_rerank(void) {
    printf("[fase123-rerank]\n");
    {
        int n = 0;
        char **t = tokenizar("profissionais acoes investidores", &n);
        int hp = 0, ha = 0, hi = 0;
        for (int i = 0; i < n; i++) {
            if (strcmp(t[i], "profissional") == 0) hp = 1;
            if (strcmp(t[i], "acao") == 0) ha = 1;
            if (strcmp(t[i], "investidor") == 0) hi = 1;
        }
        CHECK(hp && ha && hi, "stemming dobra plural p/ singular");
        liberar_tokens(t, n);
    }
    {
        int n = 0;
        char **t = tokenizar("falando assinado funcoes", &n);
        int hf = 0, ha = 0, hfu = 0;
        for (int i = 0; i < n; i++) {
            if (strcmp(t[i], "fala") == 0) hf = 1;
            if (strcmp(t[i], "assina") == 0) ha = 1;
            if (strcmp(t[i], "funcao") == 0) hfu = 1;
        }
        CHECK(hf && ha && hfu, "stemming dobra verbo/particula");
        liberar_tokens(t, n);
    }
    {
        char junk[64];
        memset(junk, 'A', 50); junk[50] = '\0';
        int n = -1;
        char **t = tokenizar(junk, &n);
        CHECK(n == 0, "token gigante junk descartado");
        liberar_tokens(t, n);
    }
    {
        Chunk ch[2];
        memset(ch, 0, sizeof ch);
        ch[0].texto = "titulos sustentaveis green bonds guia ICMA principios"; ch[0].pagina_inicio = 5;
        ch[1].texto = "individuos encontraram de AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA 2/28"; ch[1].pagina_inicio = 61;
        Embeddings *e = gerar_embeddings(ch, 2);
        int nout = 0;
        RankItem *rk = recuperar_chunks("O que sao titulos sustentaveis?", ch, 2, e, 2, &nout);
        CHECK(rk && rk[0].indice_chunk == 0, "chunk junk nao supera conteudo");
        if (rk) free(rk);
        liberar_embeddings(e);
    }
    {
        Chunk ch[2];
        memset(ch, 0, sizeof ch);
        ch[0].texto = "contrato registrado em cartorio de titulos"; ch[0].pagina_inicio = 1;
        ch[1].texto = "contrato assinado entre as partes presentes"; ch[1].pagina_inicio = 2;
        Embeddings *e = gerar_embeddings(ch, 2);
        int nout = 0;
        RankItem *rk = recuperar_chunks("contrato assinado", ch, 2, e, 2, &nout);
        CHECK(rk && rk[0].indice_chunk == 1, "frase exata ranqueia primeiro");
        if (rk) free(rk);
        liberar_embeddings(e);
    }
    {
        Chunk ch[1];
        memset(ch, 0, sizeof ch);
        ch[0].texto = "oi de"; ch[0].pagina_inicio = 9;
        Embeddings *e = gerar_embeddings(ch, 1);
        int nout = 0;
        RankItem *rk = recuperar_chunks("contrato assinado", ch, 1, e, 1, &nout);
        CHECK(rk && rk[0].score == 0.0f, "chunk pobre (<4 termos) zera score");
        if (rk) free(rk);
        liberar_embeddings(e);
    }
}

static void test_fase124(void) {    printf("[fase124]\n");
    {
        Chunk ch[3];
        memset(ch, 0, sizeof ch);
        ch[0].texto = "contrato de adesao clausula penal multa rescisoria"; ch[0].pagina_inicio = 1;
        ch[1].texto = "o gato sentou no tapete fofo amarelo"; ch[1].pagina_inicio = 2;
        ch[2].texto = "o gato correu no parque verde amplo"; ch[2].pagina_inicio = 3;
        Embeddings *e = gerar_embeddings(ch, 3);
        RetrievalIndex *ix = indice_criar(ch, 3);
        CHECK(ix != NULL, "indice cria");
        int n1 = 0, n2 = 0;
        RankItem *rleg = recuperar_chunks("clausula penal", ch, 3, e, 3, &n1);
        RankItem *ridx = indice_recuperar(ix, "clausula penal", e, 3, &n2);
        CHECK(rleg && ridx && n1 == n2, "indice e legado retornam mesmo n");
        if (rleg && ridx && n1 == n2) {
            int mesma_ordem = 1;
            for (int i = 0; i < n1; i++) {
                if (rleg[i].indice_chunk != ridx[i].indice_chunk) mesma_ordem = 0;
                if (fabsf(rleg[i].score - ridx[i].score) > 1e-4f) mesma_ordem = 0;
            }
            CHECK(mesma_ordem, "indice e legado: mesma ordem e score");
            CHECK(ridx[0].indice_chunk == 0, "indice prefere termo raro");
        }
        free(rleg); free(ridx);
        /* mesma query 2x: mesmo resultado (deterministico) */
        RankItem *ra = indice_recuperar(ix, "gato tapete", e, 3, &n1);
        RankItem *rb = indice_recuperar(ix, "gato tapete", e, 3, &n2);
        CHECK(ra && rb && ra[0].indice_chunk == rb[0].indice_chunk, "indice deterministico");
        free(ra); free(rb);
        indice_liberar(ix);
        liberar_embeddings(e);
    }
    {
        query_cache_limpar();
        Embeddings *a = embed_query_cached("contrato assinado");
        long h1 = 0, m1 = 0;
        query_cache_stats(&h1, &m1);
        Embeddings *b = embed_query_cached("contrato assinado");
        long h2 = 0, m2 = 0;
        query_cache_stats(&h2, &m2);
        CHECK(a && b, "cache retorna embeddings");
        CHECK(m1 == 1 && h1 == 0, "1a query = miss");
        CHECK(h2 == 1 && m2 == 1, "2a query igual = hit");
        if (a && b) {
            int iguais = 1;
            for (int i = 0; i < 384; i++) {
                if (fabsf(a->vetores[i] - b->vetores[i]) > 1e-6f) { iguais = 0; break; }
            }
            CHECK(iguais, "hit devolve mesmo vetor");
        }
        liberar_embeddings(a);
        liberar_embeddings(b);
        query_cache_limpar();
    }
    {
        /* v3: calibracao sobrevive ao roundtrip */
        Chunk *ch = (Chunk *)xcalloc(1, sizeof(Chunk));
        ch[0].texto = xstrdup("A capital do Brasil e Brasilia, inaugurada em 1960.");
        ch[0].hash = xstrdup("eeee4444"); ch[0].pagina_inicio = 1; ch[0].pagina_fim = 1; ch[0].num_tokens = 9;
        Embeddings *e = gerar_embeddings(ch, 1);
        QuestionGenConfig qc = {1, 1, 1};
        int nq = 0;
        PerguntaTipada *qs = gerar_perguntas(ch, 1, &qc, &nq);
        AmandaPackage pkg;
        memset(&pkg, 0, sizeof pkg);
        pkg.titulo = xstrdup("v3"); pkg.autor = xstrdup("t");
        pkg.data = xstrdup("2026-10-02"); pkg.idioma = xstrdup("pt-BR");
        pkg.versao_app = xstrdup("1.0.32");
        pkg.chunks = ch; pkg.num_chunks = 1;
        pkg.embeddings = e; pkg.perguntas = qs; pkg.num_perguntas = nq;
        pkg.tem_calib = 1; pkg.cal_center = 0.2f; pkg.cal_slope = 16.0f; pkg.cal_limiar = 0.85f;
        const char *tmp = "amanda_test_v3.tmp";
        char *erro = NULL;
        CHECK(empacotar_amanda(&pkg, tmp, &erro) == 0, "empacota v3 com calib");
        AmandaPackage *back = carregar_amanda(tmp, &erro);
        CHECK(back && back->tem_calib == 1, "roundtrip v3 preserva flag");
        if (back) {
            CHECK(fabsf(back->cal_center - 0.2f) < 1e-6f, "roundtrip preserva center");
            CHECK(fabsf(back->cal_slope - 16.0f) < 1e-6f, "roundtrip preserva slope");
            CHECK(fabsf(back->cal_limiar - 0.85f) < 1e-6f, "roundtrip preserva limiar");
            char *sj = package_stats_json(back);
            CHECK(sj && strstr(sj, "calibracao") != NULL, "inspect json expoe calibracao");
            free(sj);
            liberar_package(back);
        }
        remove(tmp);
        free(pkg.titulo); free(pkg.autor); free(pkg.data);
        free(pkg.idioma); free(pkg.versao_app);
        liberar_chunks(ch, 1);
        liberar_embeddings(e);
        liberar_perguntas(qs, nq);
    }
    {
        /* precedencia: zeros -> pacote; explicito -> mantido */
        DecisionConfig c;
        memset(&c, 0, sizeof c);
        decisao_usar_calib_pacote(&c, 1, 0.2f, 16.0f, 0.85f);
        CHECK(fabsf(c.conf_center - 0.2f) < 1e-6f && fabsf(c.conf_slope - 16.0f) < 1e-6f,
              "zeros herdam pacote");
        CHECK(fabsf(c.limiar_recusa - 0.85f) < 1e-6f, "limiar zero herda pacote");
        DecisionConfig e2;
        memset(&e2, 0, sizeof e2);
        e2.conf_center = 0.5f; e2.conf_slope = 20.0f; e2.limiar_recusa = 0.9f;
        decisao_usar_calib_pacote(&e2, 1, 0.2f, 16.0f, 0.85f);
        CHECK(fabsf(e2.conf_center - 0.5f) < 1e-6f && fabsf(e2.limiar_recusa - 0.9f) < 1e-6f,
              "flag CLI vence pacote");
        DecisionConfig e3;
        memset(&e3, 0, sizeof e3);
        decisao_usar_calib_pacote(&e3, 0, 0.2f, 16.0f, 0.85f);
        CHECK(e3.conf_center == 0.0f && e3.conf_slope == 0.0f, "sem pacote: zeros intactos");
    }
}

static void test_fase124b(void) {
    printf("[fase124b-validacao]\n");
    /* loader: gold em miniatura com pagina antes da pergunta */
    const char *vp = "amanda_test_val.tmp";
    FILE *f = fopen(vp, "w");
    CHECK(f != NULL, "cria gold temporario");
    if (f) {
        fputs("{\"perguntas\":[{\"pagina_esperada\":7,\"pergunta\":\"O que e Brasilia?\"},"
              "{\"pagina_esperada\":9,\"pergunta\":\"O que e fotossintese?\"}]}", f);
        fclose(f);
    }
    CalibraValQ *vq = NULL;
    int nvq = 0;
    char *verr = NULL;
    CHECK(calibra_carregar_validacao(vp, &vq, &nvq, &verr) == 0, "carrega validacao");
    if (vq) {
        CHECK(nvq == 2, "2 naturais carregadas");
        CHECK(vq[0].pagina == 7 && strstr(vq[0].pergunta, "Brasilia") != NULL, "pagina+pergunta associadas");
        CHECK(vq[1].pagina == 9, "segunda pagina correta");
        calibra_liberar_validacao(vq, nvq);
    } else { printf("  erro: %s\n", verr ? verr : "?"); free(verr); }
    CHECK(calibra_carregar_validacao("arquivo-que-nao-existe.json", &vq, &nvq, &verr) != 0, "ausente = erro");
    free(verr); verr = NULL;
    remove(vp);
    /* calibrate com validacao: n_val entra no relatorio + recall */
    {
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
        AmandaPackage pkg;
        memset(&pkg, 0, sizeof pkg);
        pkg.chunks = ch; pkg.num_chunks = nc;
        pkg.embeddings = e;
        pkg.perguntas = qs; pkg.num_perguntas = nq;
        FILE *g = fopen(vp, "w");
        CHECK(g != NULL, "recria gold temporario");
        if (g) {
            fputs("{\"perguntas\":[{\"pagina_esperada\":1,\"pergunta\":\"Qual e a capital do Brasil?\"}]}", g);
            fclose(g);
        }
        CalibraConfig cc;
        memset(&cc, 0, sizeof cc);
        cc.sample = 1.0; cc.seed = 42u;
        snprintf(cc.validacao[0], sizeof cc.validacao[0], "%s", vp);
        cc.n_validacao = 1;
        CalibraReport r;
        char *erro = NULL;
        CHECK(calibra_run(&pkg, &cc, &r, &erro) == 0, "calibra com validacao ok");
        if (erro) { printf("  erro: %s\n", erro); free(erro); }
        CHECK(r.n_val == 1, "n_val=1 no relatorio");
        CHECK(r.val_recall >= 0.0 && r.val_recall <= 1.0, "recall em [0,1]");
        char *j = calibra_to_json(&r, "mem");
        CHECK(j && strstr(j, "validacao") != NULL, "json expoe validacao");
        free(j);
        remove(vp);
        liberar_chunks(ch, nc);
        liberar_embeddings(e);
        liberar_perguntas(qs, nq);
    }
}

#define T13_PORT 18084

#ifdef _WIN32
static unsigned __stdcall t13_srv(void *p) { server_run((const ServerConfig *)p); return 0; }
#else
static void *t13_srv(void *p) { server_run((const ServerConfig *)p); return NULL; }
#endif

static AmandaPackage *t13_mkpkg(const char *titulo, const char *texto, int pagina) {
    Chunk *ch = (Chunk *)xcalloc(1, sizeof(Chunk));
    ch[0].texto = xstrdup(texto);
    ch[0].hash = xstrdup("eeee4444"); ch[0].pagina_inicio = pagina; ch[0].pagina_fim = pagina; ch[0].num_tokens = 14;
    Embeddings *e = gerar_embeddings(ch, 1);
    QuestionGenConfig qc = {1, 1, 1};
    int nq = 0;
    PerguntaTipada *qs = gerar_perguntas(ch, 1, &qc, &nq);
    AmandaPackage *pkg = (AmandaPackage *)xcalloc(1, sizeof(*pkg));
    pkg->titulo = xstrdup(titulo); pkg->autor = xstrdup("t");
    pkg->data = xstrdup("2026-10-02"); pkg->idioma = xstrdup("pt-BR");
    pkg->versao_app = xstrdup("1.0.33");
    pkg->chunks = ch; pkg->num_chunks = 1;
    pkg->embeddings = e; pkg->perguntas = qs; pkg->num_perguntas = nq;
    pkg->num_paginas = pagina; pkg->extra_blocos = 1;
    return pkg;
}

static void test_serve_multi(void) {
    printf("[serve_multi]\n");
    /* chave: flag > env > arquivo > aberto */
    {
#ifdef _WIN32
        _putenv("AMANDA_API_KEY=");
#else
        unsetenv("AMANDA_API_KEY");
#endif
        char *k0 = amanda_resolve_api_key(NULL, NULL);
        CHECK(k0 == NULL, "sem nada = aberto");
        free(k0);
        char *k1 = amanda_resolve_api_key("flagK", NULL);
        CHECK(k1 && strcmp(k1, "flagK") == 0, "flag vence");
        free(k1);
#ifdef _WIN32
        _putenv("AMANDA_API_KEY=envK");
#else
        setenv("AMANDA_API_KEY", "envK", 1);
#endif
        char *k2 = amanda_resolve_api_key(NULL, NULL);
        CHECK(k2 && strcmp(k2, "envK") == 0, "env lido");
        free(k2);
        char *k3 = amanda_resolve_api_key("flagK", NULL);
        CHECK(k3 && strcmp(k3, "flagK") == 0, "flag vence env");
        free(k3);
#ifdef _WIN32
        _putenv("AMANDA_API_KEY=");
#else
        unsetenv("AMANDA_API_KEY");
#endif
        FILE *kf = fopen("amanda_test_key.tmp", "w");
        CHECK(kf != NULL, "cria keyfile");
        if (kf) { fputs("  fileK123\n", kf); fclose(kf); }
        char *k4 = amanda_resolve_api_key(NULL, "amanda_test_key.tmp");
        CHECK(k4 && strcmp(k4, "fileK123") == 0, "arquivo com trim");
        free(k4);
        char *k5 = amanda_resolve_api_key(NULL, "arquivo-que-nao-existe.key");
        CHECK(k5 == NULL, "arquivo ausente = aberto");
        free(k5);
        remove("amanda_test_key.tmp");
    }
    /* dois pacotes, roteamento por model */
    AmandaPackage *pa = t13_mkpkg("livroA", "A capital do Brasil e Brasilia, inaugurada em 1960.", 11);
    AmandaPackage *pb = t13_mkpkg("livroB", "A fotossintese produz glicose nas plantas verdes.", 22);
    AmandaPackage *arr[2] = {pa, pb};
    const char *nms[2] = {"livroA", "livroB"};
    g_t75_port = T13_PORT;
    volatile int stop = 0;
    ServerConfig sc;
    memset(&sc, 0, sizeof sc);
    sc.host = "127.0.0.1"; sc.port = T13_PORT; sc.stop_flag = &stop;
    sc.pkgs = arr; sc.pkg_names = nms; sc.n_pkgs = 2;
    sc.max_conns = 8; sc.workers = 4; sc.eval_max = 50;
#ifdef _WIN32
    uintptr_t th = _beginthreadex(NULL, 0, t13_srv, &sc, 0, NULL);
    CHECK(th != 0, "serve multi sobe (pool)");
#else
    pthread_t th;
    CHECK(pthread_create(&th, NULL, t13_srv, &sc) == 0, "serve multi sobe (pool)");
#endif
    char *ready = NULL;
    for (int i = 0; i < 100 && !ready; i++) {
        char *r = t75_call("GET", "/v1/models", NULL, NULL);
        if (r && strstr(r, "200 OK")) ready = r;
        else { free(r); sleep_ms(100); }
    }
    CHECK(ready && strstr(ready, "livroA") && strstr(ready, "livroB"), "models lista os 2");
    free(ready);
    {
        char *r = t75_call("POST", "/v1/chat/completions", NULL,
            "{\"model\":\"livroA\",\"messages\":[{\"role\":\"user\",\"content\":\"Qual e a capital?\"}]}");
        CHECK(r && strstr(r, "Brasilia") && strstr(r, "\"model\":\"livroA\""), "chat model=livroA roteia");
        free(r);
    }
    {
        char *r = t75_call("POST", "/v1/chat/completions", NULL,
            "{\"model\":\"livroB\",\"messages\":[{\"role\":\"user\",\"content\":\"O que e fotossintese?\"}]}");
        CHECK(r && strstr(r, "glicose") && strstr(r, "\"model\":\"livroB\""), "chat model=livroB roteia");
        free(r);
    }
    {
        char *r = t75_call("POST", "/v1/chat/completions", NULL,
            "{\"messages\":[{\"role\":\"user\",\"content\":\"Qual e a capital?\"}]}");
        CHECK(r && strstr(r, "Brasilia"), "sem model = 1o pacote");
        free(r);
    }
    {
        char *r = t75_call("POST", "/v1/decisions", NULL,
            "{\"model\":\"livroB\",\"pergunta\":\"O que e fotossintese?\"}");
        CHECK(r && strstr(r, "glicose"), "decisions com model roteia");
        free(r);
    }
    {
        char *r = t75_call("POST", "/v1/chat/completions", NULL,
            "{\"model\":\"nope\",\"messages\":[{\"role\":\"user\",\"content\":\"oi\"}]}");
        CHECK(r && strstr(r, "404") && strstr(r, "livroA"), "model desconhecido = 404 com lista");
        free(r);
    }
    {
        char *r = t75_call("GET", "/v1/amanda/info", NULL, NULL);
        CHECK(r && strstr(r, "pacotes") && strstr(r, "livroB"), "info expoe pacotes");
        free(r);
    }
    stop = 1;
    {
        char *r = t75_call("GET", "/v1/models", NULL, NULL);
        free(r);
    }
#ifdef _WIN32
    WaitForSingleObject((HANDLE)th, 20000);
    CloseHandle((HANDLE)th);
#else
    pthread_join(th, NULL);
#endif
    liberar_package(pa);
    liberar_package(pb);
    g_t75_port = T75_PORT;
}

typedef struct {
    LlmPool *pool;
    int prio;
    int espera;
    int got;
    int id;
    int *ordem;
} PoolJob;

#ifdef _WIN32
static unsigned __stdcall pool_job(void *p) {
    PoolJob *j = (PoolJob *)p;
    j->got = llm_pool_adquirir(j->pool, j->prio, j->espera);
    if (j->got) {
        if (j->ordem) *j->ordem = j->id;
        llm_pool_devolver(j->pool);
    }
    return 0;
}
#else
static void *pool_job(void *p) {
    PoolJob *j = (PoolJob *)p;
    j->got = llm_pool_adquirir(j->pool, j->prio, j->espera);
    if (j->got) {
        if (j->ordem) *j->ordem = j->id;
        llm_pool_devolver(j->pool);
    }
    return NULL;
}
#endif

static void test_llm_pool(void) {
    printf("[llm_pool]\n");
    {
        LlmPool *p = llm_pool_criar(0, 0);
        CHECK(p != NULL, "pool: criar com defaults");
        CHECK(llm_pool_adquirir(p, LLM_PRIO_NORMAL, 0) == 1, "pool: slot imediato");
        llm_pool_devolver(p);
        long at = 0, ff = 0, ft = 0;
        llm_pool_stats(p, &at, &ff, &ft);
        CHECK(at == 1 && ff == 0 && ft == 0, "pool: stats iniciais");
        llm_pool_liberar(p);
    }
    CHECK(llm_pool_adquirir(NULL, 0, 0) == 0, "pool: adquirir NULL = 0 sem crash");
    llm_pool_liberar(NULL);
    {
        /* Espera 0 com slot ocupado: fallback por tempo, sem bloquear. */
        LlmPool *p = llm_pool_criar(1, 8);
        CHECK(llm_pool_adquirir(p, 0, 0) == 1, "pool: ocupa unico slot");
        long long t0 = now_ms();
        CHECK(llm_pool_adquirir(p, 0, 0) == 0, "pool: espera 0 sem slot = 0");
        CHECK(now_ms() - t0 < 2000, "pool: espera 0 retorna rapido");
        long at = 0, ff = 0, ft = 0;
        llm_pool_stats(p, &at, &ff, &ft);
        CHECK(ft == 1 && ff == 0, "pool: espera 0 conta fb_tempo");
        llm_pool_devolver(p);
        llm_pool_liberar(p);
    }
    {
        /* Timeout real: espera 150ms sem slot. */
        LlmPool *p = llm_pool_criar(1, 8);
        llm_pool_adquirir(p, 0, 0);
        long long t0 = now_ms();
        CHECK(llm_pool_adquirir(p, 0, 150) == 0, "pool: timeout sem slot = 0");
        long long dt = now_ms() - t0;
        CHECK(dt >= 100 && dt < 5000, "pool: espera respeita o prazo");
        long at = 0, ff = 0, ft = 0;
        llm_pool_stats(p, &at, &ff, &ft);
        CHECK(ft == 1, "pool: timeout conta fb_tempo");
        llm_pool_devolver(p);
        llm_pool_liberar(p);
    }
    {
        /* Fila cheia: 1 slot + 1 espera; 2a espera = fb_fila. */
        LlmPool *p = llm_pool_criar(1, 1);
        llm_pool_adquirir(p, 0, 0);
        PoolJob j1;
        memset(&j1, 0, sizeof j1);
        j1.pool = p; j1.prio = LLM_PRIO_NORMAL; j1.espera = 5000;
#ifdef _WIN32
        uintptr_t h1 = _beginthreadex(NULL, 0, pool_job, &j1, 0, NULL);
        CHECK(h1 != 0, "pool: waiter entra na fila");
#else
        pthread_t h1 = 0;
        CHECK(pthread_create(&h1, NULL, pool_job, &j1) == 0, "pool: waiter entra na fila");
#endif
        sleep_ms(300);
        CHECK(llm_pool_adquirir(p, 0, 5000) == 0, "pool: fila cheia = 0 imediato");
        long at = 0, ff = 0, ft = 0;
        llm_pool_stats(p, &at, &ff, &ft);
        CHECK(ff == 1, "pool: fila cheia conta fb_fila");
        llm_pool_devolver(p); /* transfere ao waiter */
#ifdef _WIN32
        WaitForSingleObject((HANDLE)h1, 15000);
        CloseHandle((HANDLE)h1);
#else
        pthread_join(h1, NULL);
#endif
        CHECK(j1.got == 1, "pool: waiter recebe o slot ao liberar");
        llm_pool_liberar(p);
    }
    {
        /* Prioridade: ALTA passa na frente de NORMAL ja enfileirado. */
        LlmPool *p = llm_pool_criar(1, 8);
        llm_pool_adquirir(p, 0, 0);
        int ordem_low = 0, ordem_high = 0;
        PoolJob jl, jh;
        memset(&jl, 0, sizeof jl);
        memset(&jh, 0, sizeof jh);
        jl.pool = p; jl.prio = LLM_PRIO_NORMAL; jl.espera = 8000; jl.id = 2; jl.ordem = &ordem_low;
        jh.pool = p; jh.prio = LLM_PRIO_ALTA; jh.espera = 8000; jh.id = 1; jh.ordem = &ordem_high;
#ifdef _WIN32
        uintptr_t hl = _beginthreadex(NULL, 0, pool_job, &jl, 0, NULL);
        sleep_ms(300);
        uintptr_t hh = _beginthreadex(NULL, 0, pool_job, &jh, 0, NULL);
        sleep_ms(300);
        CHECK(hl != 0 && hh != 0, "pool: dois waiters enfileirados");
#else
        pthread_t hl = 0, hh = 0;
        int r1 = pthread_create(&hl, NULL, pool_job, &jl);
        sleep_ms(300);
        int r2 = pthread_create(&hh, NULL, pool_job, &jh);
        sleep_ms(300);
        CHECK(r1 == 0 && r2 == 0, "pool: dois waiters enfileirados");
#endif
        llm_pool_devolver(p); /* deve entregar ao ALTA primeiro */
#ifdef _WIN32
        WaitForSingleObject((HANDLE)hh, 15000);
        WaitForSingleObject((HANDLE)hl, 15000);
        CloseHandle((HANDLE)hh);
        CloseHandle((HANDLE)hl);
#else
        pthread_join(hh, NULL);
        pthread_join(hl, NULL);
#endif
        CHECK(jh.got == 1 && jl.got == 1, "pool: ambos atendidos");
        CHECK(ordem_high == 1 && ordem_low == 2, "pool: ALTA antes de NORMAL");
        llm_pool_liberar(p);
    }
    {
        /* FIFO entre iguais: ordem de chegada. */
        LlmPool *p = llm_pool_criar(1, 8);
        llm_pool_adquirir(p, 0, 0);
        int o1 = 0, o2 = 0;
        PoolJob j1, j2;
        memset(&j1, 0, sizeof j1);
        memset(&j2, 0, sizeof j2);
        j1.pool = p; j1.espera = 8000; j1.id = 1; j1.ordem = &o1;
        j2.pool = p; j2.espera = 8000; j2.id = 2; j2.ordem = &o2;
#ifdef _WIN32
        uintptr_t h1 = _beginthreadex(NULL, 0, pool_job, &j1, 0, NULL);
        sleep_ms(300);
        uintptr_t h2 = _beginthreadex(NULL, 0, pool_job, &j2, 0, NULL);
        sleep_ms(300);
        llm_pool_devolver(p);
        WaitForSingleObject((HANDLE)h1, 15000);
        WaitForSingleObject((HANDLE)h2, 15000);
        CloseHandle((HANDLE)h1);
        CloseHandle((HANDLE)h2);
#else
        pthread_t h1 = 0, h2 = 0;
        pthread_create(&h1, NULL, pool_job, &j1);
        sleep_ms(300);
        pthread_create(&h2, NULL, pool_job, &j2);
        sleep_ms(300);
        llm_pool_devolver(p);
        pthread_join(h1, NULL);
        pthread_join(h2, NULL);
#endif
        CHECK(o1 == 1 && o2 == 2, "pool: FIFO entre mesma prioridade");
        llm_pool_liberar(p);
    }
    {
        /* Config: laya_queue/laya_queue_ms na secao servidor. */
        const char *cf = "amanda_test_pool_cfg.tmp";
        FILE *f = fopen(cf, "w");
        CHECK(f != NULL, "pool: cria yaml temporario");
        if (f) {
            fputs("servidor:\n  laya_queue: 4\n  laya_queue_ms: 2500\n", f);
            fclose(f);
        }
        AmandaConfig ac;
        config_defaults(&ac);
        char *cerr = NULL;
        CHECK(config_ler(cf, &ac, &cerr) == 0, "pool: config le laya_queue*");
        CHECK(ac.srv_laya_queue == 4 && ac.srv_laya_queue_ms == 2500, "pool: valores da fila no config");
        free(cerr);
        remove(cf);
    }
}

/* ============ Backends reais: SystemOne + DeepSeek (stubs) ============ */

#define TTS_PORT 18091
#define TDS_PORT 18092

typedef struct {
    int port;
    volatile int *stop;
    const char *body;
    int code;
} MiniStub;

#ifdef _WIN32
static unsigned __stdcall mini_stub(void *p) {
#else
static void *mini_stub(void *p) {
#endif
    MiniStub *m = (MiniStub *)p;
#ifdef _WIN32
    {
        /* O processo pode estar com a contagem WSA zerada aqui
         * (testes de serve fazem Startup/Cleanup balanceados). */
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
    }
#endif
    t75_sock srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv == T75_INVALID) {
#ifdef _WIN32
        return 0;
#else
        return NULL;
#endif
    }
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof opt);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)m->port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(srv, (struct sockaddr *)&a, sizeof a) != 0 || listen(srv, 8) != 0) {
        t75_close(srv);
#ifdef _WIN32
        return 0;
#else
        return NULL;
#endif
    }
    while (!*m->stop) {
        struct sockaddr_in cli;
#ifdef _WIN32
        int cl = sizeof cli;
#else
        socklen_t cl = sizeof cli;
#endif
        t75_sock fd = accept(srv, (struct sockaddr *)&cli, &cl);
        if (fd == T75_INVALID) {
            if (*m->stop) break;
            continue;
        }
        if (*m->stop) { t75_close(fd); break; }
        /* consome pedido (cabecalho + corpo) sem interpretar;
         * testa completude ANTES de bloquear no recv (senao deadlock:
         * o cliente espera a resposta enquanto esperamos mais corpo). */
        char hb[65536];
        int got = 0, clen = 0, hdone = 0;
        while (got < (int)sizeof(hb) - 1) {
            if (hdone && got - hdone >= clen) break;
            int r = recv(fd, hb + got, (int)sizeof(hb) - 1 - got, 0);
            if (r <= 0) break;
            got += r;
            hb[got] = '\0';
            if (!hdone) {
                char *he = strstr(hb, "\r\n\r\n");
                if (he) {
                    char *clp = strstr(hb, "Content-Length:");
                    if (clp) clen = atoi(clp + 15);
                    hdone = (int)(he + 4 - hb);
                }
            }
        }
        const char *reason = (m->code == 200) ? "OK" : "Unauthorized";
        size_t blen = m->body ? strlen(m->body) : 0;
        char head[256];
        snprintf(head, sizeof head,
                 "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\n"
                 "Content-Length: %lu\r\nConnection: close\r\n\r\n",
                 m->code, reason, (unsigned long)blen);
        send(fd, head, (int)strlen(head), 0);
        if (blen) send(fd, m->body, (int)blen, 0);
        t75_close(fd);
    }
    t75_close(srv);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static void mini_wait_port(int port) {
    for (int i = 0; i < 50; i++) {
        t75_sock s = socket(AF_INET, SOCK_STREAM, 0);
        if (s != T75_INVALID) {
            struct sockaddr_in a;
            memset(&a, 0, sizeof a);
            a.sin_family = AF_INET;
            a.sin_port = htons((unsigned short)port);
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            int ok = connect(s, (struct sockaddr *)&a, sizeof a);
            t75_close(s);
            if (ok == 0) break;
        }
        sleep_ms(100);
    }
}

static void test_typesafe_deepseek(void) {
    printf("[typesafe_deepseek]\n");
    {
        char ep[1024];
        typesafe_endpoint("https://api.typesafe.ai", ep, sizeof ep);
        CHECK(strcmp(ep, "https://api.typesafe.ai/v1/systemone") == 0, "ts: base nuvem + path");
        typesafe_endpoint("http://127.0.0.1:11434/", ep, sizeof ep);
        CHECK(strcmp(ep, "http://127.0.0.1:11434/v1/systemone") == 0, "ts: base local sem // duplo");
        typesafe_endpoint("http://127.0.0.1:11434/v1/systemone", ep, sizeof ep);
        CHECK(strcmp(ep, "http://127.0.0.1:11434/v1/systemone") == 0, "ts: URL completa intacta");
        typesafe_endpoint(NULL, ep, sizeof ep);
        CHECK(strcmp(ep, "https://api.typesafe.ai/v1/systemone") == 0, "ts: default nuvem");
    }
    CHECK(strcmp(decision_backend_nome(0), "local") == 0, "via 0 = local");
    CHECK(strcmp(decision_backend_nome(1), "laya-http") == 0, "via 1 = laya-http");
    CHECK(strcmp(decision_backend_nome(2), "typesafe-http") == 0, "via 2 = typesafe-http");
    CHECK(strcmp(decision_backend_nome(3), "deepseek-http") == 0, "via 3 = deepseek-http");
    CHECK(strcmp(decision_backend_nome(99), "local") == 0, "via invalido = local");
    {
        int code = 0;
        char *rb = NULL;
        char *err = NULL;
        CHECK(http_post_json("ftp://x/y", NULL, "{}", 1000, &code, &rb, &err) != 0, "http: scheme invalido recusa");
        free(rb);
        free(err);
    }
    static const char ts_body[] =
        "{\"model\":\"stub\",\"answers\":{\"suporte\":{\"type\":\"noul\",\"noul\":0.92}},"
        "\"usage\":{\"input_tokens\":10,\"output_tokens\":1}}";
    static const char ds_body[] =
        "{\"id\":\"chatcmpl-stub\",\"object\":\"chat.completion\","
        "\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\","
        "\"content\":\"RESPOSTA-STUB-DEEPSEEK\"},\"finish_reason\":\"stop\"}],"
        "\"usage\":{\"prompt_tokens\":10,\"completion_tokens\":5}}";
    static volatile int tts_stop = 0, tds_stop = 0;
    static MiniStub mts;
    memset(&mts, 0, sizeof mts);
    mts.port = TTS_PORT; mts.stop = &tts_stop; mts.body = ts_body; mts.code = 200;
    static MiniStub mds;
    memset(&mds, 0, sizeof mds);
    mds.port = TDS_PORT; mds.stop = &tds_stop; mds.body = ds_body; mds.code = 200;
#ifdef _WIN32
    _beginthreadex(NULL, 0, mini_stub, &mts, 0, NULL);
    _beginthreadex(NULL, 0, mini_stub, &mds, 0, NULL);
#else
    {
        pthread_t th;
        pthread_create(&th, NULL, mini_stub, &mts);
        pthread_detach(th);
        pthread_create(&th, NULL, mini_stub, &mds);
        pthread_detach(th);
    }
#endif
    mini_wait_port(TTS_PORT);
    mini_wait_port(TDS_PORT);
    {
        double noul = -1.0;
        char *err = NULL;
        TsStatus s = typesafe_judge("http://127.0.0.1:18091", "stub", NULL,
                                    "contexto de teste", "pergunta?",
                                    8000, &noul, &err);
        CHECK(s == TS_OK && noul > 0.91 && noul < 0.93, "ts: judge stub noul 0.92");
        free(err);
    }
    {
        double noul = -1.0;
        char *err = NULL;
        TsStatus s = typesafe_judge("http://127.0.0.1:18099", "stub", NULL,
                                    "ctx", "q?", 1500, &noul, &err);
        CHECK(s == TS_UNAVAILABLE, "ts: porta fechada = UNAVAILABLE (fallback)");
        free(err);
    }
    {
        /* HTTP 401 (ex.: chave invalida) tambem e fallback honesto. */
        static const char e401[] = "{\"error\":\"unauthorized\"}";
        mts.code = 401;
        mts.body = e401;
        double noul = -1.0;
        char *err = NULL;
        TsStatus s = typesafe_judge("http://127.0.0.1:18091", "stub", NULL,
                                    "ctx", "q?", 8000, &noul, &err);
        CHECK(s == TS_UNAVAILABLE, "ts: HTTP 401 = UNAVAILABLE");
        CHECK(err && strstr(err, "401") != NULL, "ts: erro cita o HTTP 401");
        free(err);
        mts.code = 200;
        mts.body = ts_body;
    }
    {
        char *content = NULL;
        char *err = NULL;
        DsStatus s = deepseek_redact("http://127.0.0.1:18092", "stub", NULL,
                                     "responda: oi", 8000, &content, &err);
        CHECK(s == DS_OK && content && strstr(content, "STUB-DEEPSEEK") != NULL, "ds: redact stub responde");
        free(content);
        free(err);
    }
    {
        char *content = NULL;
        char *err = NULL;
        DsStatus s = deepseek_redact("http://127.0.0.1:18099", "stub", NULL,
                                     "oi", 1500, &content, &err);
        CHECK(s == DS_UNAVAILABLE, "ds: porta fechada = UNAVAILABLE (fallback)");
        free(content);
        free(err);
    }
    {
        /* hibrida typesafe ponta a ponta contra o stub (via=2). */
        DocumentoExtraido doc;
        memset(&doc, 0, sizeof doc);
        BlocoTexto bs;
        bs.texto = "A capital do Brasil e Brasilia, inaugurada em 1960.";
        bs.pagina = 7; bs.x = bs.y = bs.largura = bs.altura = 0;
        doc.blocos = &bs; doc.num_blocos = 1; doc.num_paginas = 7;
        int nc = 0;
        Chunk *ch = dividir_em_chunks(&doc, 180, 0, &nc);
        Embeddings *e = gerar_embeddings(ch, nc);
        DecisionConfig cfg;
        memset(&cfg, 0, sizeof cfg);
        cfg.limiar_confianca = 0.7f;
        cfg.top_k = 3;
        cfg.limiar_recusa = 0.3f;
        cfg.backend = DECISION_BACKEND_TYPESAFE_HTTP;
        snprintf(cfg.typesafe_url, sizeof cfg.typesafe_url, "http://127.0.0.1:%d", TTS_PORT);
        snprintf(cfg.typesafe_model, sizeof cfg.typesafe_model, "stub");
        cfg.typesafe_timeout_ms = 8000;
        RetrievalIndex *rix = indice_criar(ch, nc);
        int via = -1;
        Decisao *d = executar_decisao_hibrida_idx("Qual e a capital do Brasil?",
                                                  rix, ch, nc, e, &cfg, &via);
        CHECK(d && via == 2, "ts: hibrida via typesafe (2)");
        if (d) {
            CHECK(d->confianca > 0.91 && d->confianca < 0.93, "ts: confianca = noul do JEV");
            CHECK(d->pagina == 7, "ts: grounding segue local (pagina)");
            liberar_decisao(d);
        }
        /* stub fora do ar: fallback local honesto (via=0). */
        snprintf(cfg.typesafe_url, sizeof cfg.typesafe_url, "http://127.0.0.1:18099");
        d = executar_decisao_hibrida_idx("Qual e a capital do Brasil?",
                                         rix, ch, nc, e, &cfg, &via);
        CHECK(d && via == 0, "ts: falha = fallback local (0)");
        if (d) liberar_decisao(d);
        indice_liberar(rix);
        liberar_chunks(ch, nc);
        liberar_embeddings(e);
    }
    {
        /* resolve_secret: flag > env > arquivo. */
#ifdef _WIN32
        _putenv("AMANDA_TEST_SECRET=do-env");
#else
        setenv("AMANDA_TEST_SECRET", "do-env", 1);
#endif
        char *k = amanda_resolve_secret("da-flag", "AMANDA_TEST_SECRET", NULL);
        CHECK(k && strcmp(k, "da-flag") == 0, "secret: flag vence env");
        free(k);
        k = amanda_resolve_secret(NULL, "AMANDA_TEST_SECRET", NULL);
        CHECK(k && strcmp(k, "do-env") == 0, "secret: env sem flag");
        free(k);
        const char *kf = "amanda_test_key.tmp";
        FILE *f = fopen(kf, "w");
        if (f) { fputs("  do-arquivo \n", f); fclose(f); }
        k = amanda_resolve_secret(NULL, "AMANDA_TEST_AUSENTE", kf);
        CHECK(k && strcmp(k, "do-arquivo") == 0, "secret: arquivo com trim");
        free(k);
        k = amanda_resolve_secret(NULL, "AMANDA_TEST_AUSENTE", NULL);
        CHECK(k == NULL, "secret: nada = NULL");
        remove(kf);
#ifdef _WIN32
        _putenv("AMANDA_TEST_SECRET=");
#else
        unsetenv("AMANDA_TEST_SECRET");
#endif
    }
    {
        /* amanda.json: secoes + overlay sem reset. */
        const char *jf = "amanda_test_cfg.tmp";
        FILE *f = fopen(jf, "w");
        CHECK(f != NULL, "json: cria temporario");
        if (f) {
            fputs("{\"servidor\":{\"backend\":\"typesafe-http\",\"typesafe_model\":\"jev-latest\"},"
                  "\"typesafe\":{\"url\":\"https://api.typesafe.ai\",\"model\":\"jev-1\"},"
                  "\"decision_engine\":{\"limiar_recusa\":0.5}}", f);
            fclose(f);
        }
        AmandaConfig ac;
        config_defaults(&ac);
        snprintf(ac.srv_laya_url, sizeof ac.srv_laya_url, "http://ja-existia:1");
        char *cerr = NULL;
        CHECK(config_ler_json(jf, &ac, &cerr) == 0, "json: le amanda.json");
        CHECK(strcmp(ac.srv_backend, "typesafe-http") == 0, "json: backend do servidor");
        CHECK(strcmp(ac.srv_typesafe_url, "https://api.typesafe.ai") == 0, "json: typesafe.url");
        CHECK(strcmp(ac.srv_typesafe_model, "jev-1") == 0, "json: typesafe.model vence");
        CHECK(ac.limiar_recusa == 0.5f && ac.tem_limiar == 1, "json: decision_engine.limiar");
        CHECK(strcmp(ac.srv_laya_url, "http://ja-existia:1") == 0, "json: overlay preserva ausentes");
        free(cerr);
        CHECK(config_ler_json("amanda_test_ausente.tmp", &ac, &cerr) != 0, "json: arquivo ausente = erro");
        free(cerr);
        remove(jf);
    }
    tts_stop = 1;
    tds_stop = 1;
}

static void test_mcp(void) {
    printf("[mcp]\n");
    Chunk *ch = (Chunk *)xcalloc(2, sizeof(Chunk));
    ch[0].texto = xstrdup("chunk um sobre entropia e termodinamica");
    ch[0].hash = xstrdup("mcp11111"); ch[0].pagina_inicio = 1; ch[0].pagina_fim = 1; ch[0].num_tokens = 5;
    ch[1].texto = xstrdup("chunk dois sobre energia e trabalho");
    ch[1].hash = xstrdup("mcp22222"); ch[1].pagina_inicio = 2; ch[1].pagina_fim = 2; ch[1].num_tokens = 5;
    Embeddings *e = gerar_embeddings(ch, 2);
    QuestionGenConfig qc = {1, 1, 1};
    int nq = 0;
    PerguntaTipada *qs = gerar_perguntas(ch, 2, &qc, &nq);
    AmandaPackage pkg;
    memset(&pkg, 0, sizeof pkg);
    pkg.titulo = xstrdup("mcp-teste");
    pkg.autor = xstrdup("amanda-tests");
    pkg.data = xstrdup("2026-10-05");
    pkg.idioma = xstrdup("pt-BR");
    pkg.versao_app = xstrdup("1.0.1");
    pkg.chunks = ch; pkg.num_chunks = 2;
    pkg.embeddings = e;
    pkg.perguntas = qs; pkg.num_perguntas = nq;
    char *erro = NULL;
    const char *tmp = "amanda_test_mcp.tmp";
    CHECK(empacotar_amanda(&pkg, tmp, &erro) == 0, "mcp: empacota pacote de teste");
    DecisionConfig base;
    memset(&base, 0, sizeof base);
    base.limiar_confianca = 0.7f;
    base.top_k = 3;
    base.limiar_recusa = 0.3f;
    const char *specs[1];
    specs[0] = tmp;
    McpCtx ctx;
    memset(&ctx, 0, sizeof ctx);
    char *merr = NULL;
    CHECK(mcp_ctx_init(&ctx, specs, 1, &base, 3, &merr) == 0, "mcp: ctx init carrega pacote");
    if (ctx.n_pkgs == 1) {
        char *r = mcp_handle_line(&ctx, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}");
        CHECK(r && strstr(r, "protocolVersion") && strstr(r, "\"id\":1") && strstr(r, "amandac"), "mcp: initialize com id numerico");
        free(r);
        r = mcp_handle_line(&ctx, "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\",\"params\":{}}");
        CHECK(r == NULL, "mcp: notificacao sem resposta");
        r = mcp_handle_line(&ctx, "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\",\"params\":{}}");
        CHECK(r && strstr(r, "\"ask\"") && strstr(r, "\"decisions\"") && strstr(r, "\"inspect\"") && strstr(r, "\"version\""), "mcp: tools/list expoe 4 ferramentas");
        free(r);
        r = mcp_handle_line(&ctx, "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"ask\",\"arguments\":{\"pergunta\":\"o que e entropia?\"}}}");
        CHECK(r && strstr(r, "\"text\"") && strstr(r, "confianca") && strstr(r, "entropia"), "mcp: ask responde com texto e confianca");
        free(r);
        r = mcp_handle_line(&ctx, "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\",\"params\":{\"name\":\"decisions\",\"arguments\":{\"pergunta\":\"o que e entropia?\"}}}");
        CHECK(r && strstr(r, "probabilidade") && strstr(r, "pagina"), "mcp: decisions com JSON completo");
        free(r);
        r = mcp_handle_line(&ctx, "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/call\",\"params\":{\"name\":\"ask\",\"arguments\":{}}}");
        CHECK(r && strstr(r, "-32602"), "mcp: ask sem pergunta = invalid params");
        free(r);
        r = mcp_handle_line(&ctx, "{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"tools/call\",\"params\":{\"name\":\"inexistente\",\"arguments\":{}}}");
        CHECK(r && strstr(r, "-32602"), "mcp: ferramenta desconhecida = invalid params");
        free(r);
        r = mcp_handle_line(&ctx, "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"naoexiste\",\"params\":{}}");
        CHECK(r && strstr(r, "-32601"), "mcp: metodo desconhecido = method not found");
        free(r);
        r = mcp_handle_line(&ctx, "isto nao e json");
        CHECK(r && strstr(r, "-32700"), "mcp: lixo = parse error");
        free(r);
        r = mcp_handle_line(&ctx, "{\"jsonrpc\":\"2.0\",\"id\":\"a-b\",\"method\":\"ping\"}");
        CHECK(r && strstr(r, "\"id\":\"a-b\""), "mcp: id string com eco verbatim");
        free(r);
        r = mcp_handle_line(&ctx, "   ");
        CHECK(r == NULL, "mcp: linha vazia sem resposta");
        r = mcp_handle_line(&ctx, "{\"jsonrpc\":\"2.0\",\"id\":8,\"method\":\"tools/call\",\"params\":{\"name\":\"ask\",\"arguments\":{\"pergunta\":\"entropia\",\"model\":\"nope\"}}}");
        CHECK(r && strstr(r, "isError") && strstr(r, "desconhecido"), "mcp: model desconhecido = isError com lista");
        free(r);
        r = mcp_handle_line(&ctx, "{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"tools/call\",\"params\":{\"name\":\"inspect\",\"arguments\":{}}}");
        CHECK(r && strstr(r, "chunks"), "mcp: inspect expoe chunks");
        free(r);
        r = mcp_handle_line(&ctx, "{\"jsonrpc\":\"2.0\",\"id\":10,\"method\":\"tools/call\",\"params\":{\"name\":\"version\",\"arguments\":{}}}");
        CHECK(r && strstr(r, "amandac"), "mcp: version informa amandac");
        free(r);
        mcp_ctx_free(&ctx);
    } else {
        printf("  erro mcp ctx: %s\n", merr ? merr : "?");
        free(merr);
    }
    {
        McpCtx bad;
        memset(&bad, 0, sizeof bad);
        const char *bs[1];
        bs[0] = "amanda_test_inexistente.tmp";
        char *be = NULL;
        CHECK(mcp_ctx_init(&bad, bs, 1, &base, 3, &be) != 0, "mcp: ctx init falha com pacote inexistente");
        free(be);
    }
    remove(tmp);
    free(pkg.titulo); free(pkg.autor); free(pkg.data);
    free(pkg.idioma); free(pkg.versao_app);
    liberar_chunks(ch, 2);
    liberar_embeddings(e);
    liberar_perguntas(qs, nq);
}

int main(void) {
#ifndef _WIN32
    /* Mesmo motivo de src/main.c: teste com sockets nao pode morrer de SIGPIPE. */
    signal(SIGPIPE, SIG_IGN);
#endif
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
    test_serve_75();
    test_serve_llm();
    test_fase10();
    test_fase12_retrieval();
    test_fase123_rerank();
    test_fase124();
    test_fase124b();
    test_serve_multi();
    test_mcp();
    test_llm_pool();
    test_typesafe_deepseek();
    printf("\nresultado: %d ok, %d falhas\n", passes, fails);
    return fails ? 1 : 0;
}
