#include "cli.h"
#include "amanda.h"
#include "pdf_extractor.h"
#include "chunker.h"
#include "embedder.h"
#include "question_gen.h"
#include "decision_engine.h"
#include "packager.h"
#include "server.h"
#include "eval.h"
#include "calibra.h"
#include "config.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void print_uso(void) {
    printf("amandac %s - Compilador de Conhecimento Amanda\n", amanda_version());
    printf("Uso:\n");
    printf("  amandac compile --input <arq> --output <arq.amanda> [--title T] [--author A] [--lang pt-BR] [--chunk-words N] [--overlap N]\n");
    printf("            [--config <arq.yaml>] [--templates-dir DIR] [--max-choice N] [--max-score N] [--max-noul N]\n");
    printf("  amandac serve   --package <arq.amanda> [--port 8080] [--host 127.0.0.1] [--conf-center F] [--conf-slope F] [--limiar-recusa F]\n");
    printf("  amandac ask     --package <arq.amanda> \"pergunta\" [--top-k 3] [--json] [--backend local|laya-http] [--laya-url URL]\n");
    printf("  amandac inspect --package <arq.amanda> [--stats] [--questions N] [--chunks N]\n");
    printf("  amandac eval    --package <arq.amanda> [--sample 0.1] [--seed 42] [--top-k 3] [--json] [--backend local|laya-http] [--laya-url URL]\n");
    printf("  amandac calibrate --package <arq.amanda> [--sample 0.5] [--seed 42] [--json]\n");
    printf("  amandac version\n");
    printf("Entradas aceitas: .pdf .txt .csv .json\n");
    printf("Calibracao (Fase 6): --conf-center F --conf-slope F --limiar-recusa F (ask, eval)\n");
}

static const char *flag_val(int argc, char **argv, const char *flag, const char *def) {
    for (int i = 0; i < argc - 1; i++)
        if (strcmp(argv[i], flag) == 0) return argv[i + 1];
    return def;
}

static int flag_bool(int argc, char **argv, const char *flag) {
    for (int i = 0; i < argc; i++)
        if (strcmp(argv[i], flag) == 0) return 1;
    return 0;
}

static int parse_backend(const char *s) {
    if (!s) return DECISION_BACKEND_LOCAL;
    if (strcmp(s, "laya-http") == 0 || strcmp(s, "laya") == 0) return DECISION_BACKEND_LAYA_HTTP;
    return DECISION_BACKEND_LOCAL;
}

static void fill_backend(DecisionConfig *cfg, int argc, char **argv) {
    cfg->backend = parse_backend(flag_val(argc, argv, "--backend", "local"));
    const char *url = flag_val(argc, argv, "--laya-url", NULL);
    if (url) {
        snprintf(cfg->laya_url, sizeof cfg->laya_url, "%s", url);
    }
}

/* Fase 6: aplica parametros de calibracao (zeros = padrao do motor). */
static void fill_calib(DecisionConfig *cfg, int argc, char **argv) {
    const char *cc = flag_val(argc, argv, "--conf-center", NULL);
    const char *cs = flag_val(argc, argv, "--conf-slope", NULL);
    const char *lr = flag_val(argc, argv, "--limiar-recusa", NULL);
    if (cc) cfg->conf_center = (float)atof(cc);
    if (cs) cfg->conf_slope = (float)atof(cs);
    if (lr) cfg->limiar_recusa = (float)atof(lr);
}

static void data_hoje(char *buf, size_t n) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    strftime(buf, n, "%Y-%m-%d", tm);
}

static int cmd_compile(int argc, char **argv) {
    const char *cfg_path = flag_val(argc, argv, "--config", NULL);
    AmandaConfig acfg;
    config_defaults(&acfg);
    if (cfg_path) {
        char *cerr = NULL;
        if (config_ler(cfg_path, &acfg, &cerr) != 0) {
            fprintf(stderr, "compile: %s\n", cerr ? cerr : "?");
            free(cerr);
            return 1;
        }
    }
    /* precedencia: flag CLI > config > padrao */
    const char *f_input = flag_val(argc, argv, "--input", NULL);
    const char *f_output = flag_val(argc, argv, "--output", NULL);
    const char *f_title = flag_val(argc, argv, "--title", NULL);
    const char *f_author = flag_val(argc, argv, "--author", NULL);
    const char *f_lang = flag_val(argc, argv, "--lang", NULL);
    const char *f_tpl = flag_val(argc, argv, "--templates-dir", NULL);
    const char *input = f_input ? f_input : (acfg.tem_input ? acfg.input : NULL);
    const char *output = f_output ? f_output : NULL;
    const char *title = f_title ? f_title : (acfg.title[0] ? acfg.title : NULL);
    const char *author = f_author ? f_author : acfg.author;
    const char *lang = f_lang ? f_lang : acfg.lang;
    const char *tpl_dir = f_tpl ? f_tpl : acfg.templates_dir;
    int chunk_words = acfg.chunk_words;
    {
        const char *v = flag_val(argc, argv, "--chunk-words", NULL);
        if (v) chunk_words = atoi(v);
    }
    int overlap = acfg.overlap;
    {
        const char *v = flag_val(argc, argv, "--overlap", NULL);
        if (v) overlap = atoi(v);
    }
    QuestionGenConfig qcfg;
    memset(&qcfg, 0, sizeof qcfg);
    qcfg.max_choice = acfg.max_choice; qcfg.max_score = acfg.max_score; qcfg.max_noul = acfg.max_noul;
    {
        const char *v = flag_val(argc, argv, "--max-choice", NULL);
        if (v) qcfg.max_choice = atoi(v);
        v = flag_val(argc, argv, "--max-score", NULL);
        if (v) qcfg.max_score = atoi(v);
        v = flag_val(argc, argv, "--max-noul", NULL);
        if (v) qcfg.max_noul = atoi(v);
    }
    if (!input || !output) {
        fprintf(stderr, "compile: --input e --output sao obrigatorios (ou --config com pdf.caminho)\n");
        return 2;
    }
    long long t0 = now_ms();
    char *erro = NULL;
    DocumentoExtraido *doc = extrair_documento(input, &erro);
    if (!doc) {
        fprintf(stderr, "compile: falha na extracao: %s\n", erro ? erro : "?");
        free(erro);
        return 1;
    }
    int nchunks = 0;
    Chunk *chunks = dividir_em_chunks(doc, chunk_words, overlap, &nchunks);
    if (!chunks || nchunks == 0) {
        fprintf(stderr, "compile: nenhum chunk gerado\n");
        liberar_documento(doc);
        return 1;
    }
    Embeddings *emb = gerar_embeddings(chunks, nchunks);
    QuestionTemplates tpl;
    templates_padrao(&tpl);
    if (tpl_dir && tpl_dir[0]) {
        QuestionTemplates carregados;
        if (!carregar_templates(tpl_dir, &carregados)) {
            if (f_tpl || cfg_path)
                fprintf(stderr, "compile: aviso: templates nao carregados de '%s' (usando embutidos)\n", tpl_dir);
        } else {
            tpl = carregados;
            printf("compile: templates de '%s'\n", tpl_dir);
        }
    }
    int nq = 0;
    PerguntaTipada *qs = gerar_perguntas_tpl(chunks, nchunks, &qcfg, &tpl, &nq);

    AmandaPackage pkg;
    memset(&pkg, 0, sizeof pkg);
    pkg.titulo = xstrdup(title ? title : input);
    pkg.autor = xstrdup(author);
    char dh[32]; data_hoje(dh, sizeof dh);
    pkg.data = xstrdup(dh);
    pkg.idioma = xstrdup(lang);
    pkg.versao_app = xstrdup(amanda_version());
    pkg.chunks = chunks;
    pkg.num_chunks = nchunks;
    pkg.embeddings = emb;
    pkg.perguntas = qs;
    pkg.num_perguntas = nq;

    int rc = empacotar_amanda(&pkg, output, &erro);
    long long t1 = now_ms();
    if (rc != 0) {
        fprintf(stderr, "compile: falha ao empacotar: %s\n", erro ? erro : "?");
        free(erro);
    } else {
        printf("compile ok: %s\n", output);
        printf("  blocos extraidos: %d (%d paginas)\n", doc->num_blocos, doc->num_paginas);
        printf("  chunks: %d | perguntas: %d | dim: %d | tempo: %lld ms\n",
               nchunks, nq, emb->dimensao, t1 - t0);
    }
    free(pkg.titulo); free(pkg.autor); free(pkg.data);
    free(pkg.idioma); free(pkg.versao_app);
    liberar_chunks(chunks, nchunks);
    liberar_embeddings(emb);
    liberar_perguntas(qs, nq);
    liberar_documento(doc);
    /* evita double-free: pkg aponta para memoria ja liberada */
    return rc == 0 ? 0 : 1;
}

static int cmd_serve(int argc, char **argv) {
    const char *pack = flag_val(argc, argv, "--package", NULL);
    int port = atoi(flag_val(argc, argv, "--port", "8080"));
    const char *host = flag_val(argc, argv, "--host", "127.0.0.1");
    if (!pack) { fprintf(stderr, "serve: --package obrigatorio\n"); return 2; }
    char *erro = NULL;
    AmandaPackage *pkg = carregar_amanda(pack, &erro);
    if (!pkg) {
        fprintf(stderr, "serve: %s\n", erro ? erro : "?");
        free(erro);
        return 1;
    }
    ServerConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.host = host; cfg.port = port; cfg.pkg = pkg; cfg.stop_flag = NULL;
    {
        const char *cc = flag_val(argc, argv, "--conf-center", NULL);
        const char *cs = flag_val(argc, argv, "--conf-slope", NULL);
        const char *lr = flag_val(argc, argv, "--limiar-recusa", NULL);
        if (cc) cfg.conf_center = (float)atof(cc);
        if (cs) cfg.conf_slope = (float)atof(cs);
        if (lr) { cfg.limiar_recusa = (float)atof(lr); cfg.tem_limiar = 1; }
    }
    int rc = server_run(&cfg);
    liberar_package(pkg);
    return rc;
}

static int cmd_ask(int argc, char **argv) {
    const char *pack = flag_val(argc, argv, "--package", NULL);
    int topk = atoi(flag_val(argc, argv, "--top-k", "3"));
    int asjson = flag_bool(argc, argv, "--json");
    /* pergunta: --question/--pergunta ou primeiro posicional (fora de flags com valor) */
    const char *q = flag_val(argc, argv, "--question", NULL);
    if (!q) q = flag_val(argc, argv, "--pergunta", NULL);
    char *qjoin = NULL;
    if (!q) {
        ByteBuf b; buf_init(&b);
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--package") == 0 || strcmp(argv[i], "--top-k") == 0 ||
                strcmp(argv[i], "--question") == 0 || strcmp(argv[i], "--pergunta") == 0 ||
                strcmp(argv[i], "--backend") == 0 || strcmp(argv[i], "--laya-url") == 0 ||
                strcmp(argv[i], "--conf-center") == 0 || strcmp(argv[i], "--conf-slope") == 0 ||
                strcmp(argv[i], "--limiar-recusa") == 0) {
                i++; /* pula valor da flag */
                continue;
            }
            if (argv[i][0] == '-') continue;
            if (b.len) buf_append(&b, " ", 1);
            buf_append(&b, argv[i], strlen(argv[i]));
        }
        if (b.len) {
            buf_reserve(&b, 1);
            b.data[b.len] = '\0';
            qjoin = (char *)b.data;
            q = qjoin;
        } else {
            buf_free(&b);
        }
    }
    if (!pack || !q) {
        fprintf(stderr, "ask: uso: amandac ask --package <arq.amanda> \"pergunta\"\n");
        free(qjoin);
        return 2;
    }
    char *erro = NULL;
    AmandaPackage *pkg = carregar_amanda(pack, &erro);
    if (!pkg) {
        fprintf(stderr, "ask: %s\n", erro ? erro : "?");
        free(erro);
        free(qjoin);
        return 1;
    }
    DecisionConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.limiar_confianca = 0.7f; cfg.limiar_recusa = 0.3f;
    cfg.top_k = topk > 0 ? topk : 3;
    fill_backend(&cfg, argc, argv);
    fill_calib(&cfg, argc, argv);
    int via_laya = 0;
    Decisao *d = executar_decisao_hibrida(q, pkg->chunks, pkg->num_chunks, pkg->embeddings, &cfg, &via_laya);
    if (asjson) {
        char *esc = json_escape(d->resposta);
        printf("{\"resposta\":\"%s\",\"probabilidade\":%.4f,\"confianca\":%.4f,\"pagina\":%d,\"recusada\":%s,\"backend\":\"%s\"}\n",
               esc, d->probabilidade, d->confianca, d->pagina, d->recusada ? "true" : "false",
               via_laya ? "laya-http" : "local");
        free(esc);
    } else {
        printf("%s\n", d->resposta);
        printf("\n[confianca=%.2f pagina=%d backend=%s%s]\n", d->confianca, d->pagina,
               via_laya ? "laya-http" : "local",
               d->recusada ? " recusada" : "");
    }
    liberar_decisao(d);
    liberar_package(pkg);
    free(qjoin);
    return 0;
}

static int cmd_inspect(int argc, char **argv) {
    const char *pack = flag_val(argc, argv, "--package", NULL);
    if (!pack) { fprintf(stderr, "inspect: --package obrigatorio\n"); return 2; }
    char *erro = NULL;
    AmandaPackage *pkg = carregar_amanda(pack, &erro);
    if (!pkg) {
        fprintf(stderr, "inspect: %s\n", erro ? erro : "?");
        free(erro);
        return 1;
    }
    char st[2048];
    package_stats(pkg, st, sizeof st);
    printf("%s", st);
    const char *nq = flag_val(argc, argv, "--questions", NULL);
    const char *nc = flag_val(argc, argv, "--chunks", NULL);
    if (flag_bool(argc, argv, "--stats") && !nq && !nc) { liberar_package(pkg); return 0; }
    if (nq) {
        int lim = atoi(nq);
        if (lim > pkg->num_perguntas) lim = pkg->num_perguntas;
        printf("\n-- perguntas (primeiras %d) --\n", lim);
        for (int i = 0; i < lim; i++)
            printf("[%s p.%d] %s\n", tipo_pergunta_str(pkg->perguntas[i].tipo),
                   pkg->perguntas[i].pagina_fonte, pkg->perguntas[i].enunciado);
    }
    if (nc) {
        int lim = atoi(nc);
        if (lim > pkg->num_chunks) lim = pkg->num_chunks;
        printf("\n-- chunks (primeiros %d) --\n", lim);
        for (int i = 0; i < lim; i++) {
            printf("[chunk %d p.%d-%d #%s] %.200s%s\n", i,
                   pkg->chunks[i].pagina_inicio, pkg->chunks[i].pagina_fim,
                   pkg->chunks[i].hash, pkg->chunks[i].texto,
                   strlen(pkg->chunks[i].texto) > 200 ? "..." : "");
        }
    }
    if (!nq && !nc) {
        printf("\n(use --stats, --questions N, --chunks N para detalhes)\n");
    }
    liberar_package(pkg);
    return 0;
}

static int cmd_eval(int argc, char **argv) {
    const char *pack = flag_val(argc, argv, "--package", NULL);
    double sample = atof(flag_val(argc, argv, "--sample", "0.1"));
    unsigned int seed = (unsigned int)atoi(flag_val(argc, argv, "--seed", "42"));
    int topk = atoi(flag_val(argc, argv, "--top-k", "3"));
    int asjson = flag_bool(argc, argv, "--json");
    if (!pack) { fprintf(stderr, "eval: --package obrigatorio\n"); return 2; }
    char *erro = NULL;
    AmandaPackage *pkg = carregar_amanda(pack, &erro);
    if (!pkg) {
        fprintf(stderr, "eval: %s\n", erro ? erro : "?");
        free(erro);
        return 1;
    }
    EvalConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.sample = sample; cfg.seed = seed; cfg.top_k = topk > 0 ? topk : 3;
    cfg.backend = parse_backend(flag_val(argc, argv, "--backend", "local"));
    {
        const char *url = flag_val(argc, argv, "--laya-url", NULL);
        if (url)
            snprintf(cfg.laya_url, sizeof cfg.laya_url, "%s", url);
        const char *cc = flag_val(argc, argv, "--conf-center", NULL);
        const char *cs = flag_val(argc, argv, "--conf-slope", NULL);
        const char *lr = flag_val(argc, argv, "--limiar-recusa", NULL);
        if (cc) cfg.conf_center = (float)atof(cc);
        if (cs) cfg.conf_slope = (float)atof(cs);
        if (lr) { cfg.limiar_recusa = (float)atof(lr); cfg.tem_limiar = 1; }
    }
    EvalReport rep;
    if (eval_run(pkg, &cfg, &rep, &erro) != 0) {
        fprintf(stderr, "eval: %s\n", erro ? erro : "?");
        free(erro);
        liberar_package(pkg);
        return 1;
    }
    if (asjson) {
        char *j = eval_to_json(&rep, pack);
        printf("%s\n", j);
        free(j);
    } else {
        eval_print_text(&rep, pack);
    }
    liberar_package(pkg);
    return 0;
}

static int cmd_calibrate(int argc, char **argv) {
    const char *pack = flag_val(argc, argv, "--package", NULL);
    double sample = atof(flag_val(argc, argv, "--sample", "0.5"));
    unsigned int seed = (unsigned int)atoi(flag_val(argc, argv, "--seed", "42"));
    int asjson = flag_bool(argc, argv, "--json");
    if (!pack) { fprintf(stderr, "calibrate: --package obrigatorio\n"); return 2; }
    char *erro = NULL;
    AmandaPackage *pkg = carregar_amanda(pack, &erro);
    if (!pkg) {
        fprintf(stderr, "calibrate: %s\n", erro ? erro : "?");
        free(erro);
        return 1;
    }
    CalibraConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.sample = sample; cfg.seed = seed;
    CalibraReport rep;
    if (calibra_run(pkg, &cfg, &rep, &erro) != 0) {
        fprintf(stderr, "calibrate: %s\n", erro ? erro : "?");
        free(erro);
        liberar_package(pkg);
        return 1;
    }
    if (asjson) {
        char *j = calibra_to_json(&rep, pack);
        printf("%s\n", j);
        free(j);
    } else {
        calibra_print_text(&rep, pack);
    }
    liberar_package(pkg);
    return 0;
}

int cli_main(int argc, char **argv) {
    if (argc < 2) { print_uso(); return 2; }
    if (strcmp(argv[1], "compile") == 0) return cmd_compile(argc - 1, argv + 1);
    if (strcmp(argv[1], "serve") == 0) return cmd_serve(argc - 1, argv + 1);
    if (strcmp(argv[1], "ask") == 0) return cmd_ask(argc - 1, argv + 1);
    if (strcmp(argv[1], "inspect") == 0) return cmd_inspect(argc - 1, argv + 1);
    if (strcmp(argv[1], "eval") == 0) return cmd_eval(argc - 1, argv + 1);
    if (strcmp(argv[1], "calibrate") == 0) return cmd_calibrate(argc - 1, argv + 1);
    if (strcmp(argv[1], "version") == 0 || strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0) {
        printf("amandac %s (formato .amanda v%d)\n", amanda_version(), AMANDA_FORMAT_VERSION);
        return 0;
    }
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "help") == 0) {
        print_uso();
        return 0;
    }
    fprintf(stderr, "comando desconhecido: %s\n", argv[1]);
    print_uso();
    return 2;
}
