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
    printf("            [--tj-espaco F] [--tj-salto F]\n");
    printf("  amandac serve   --package <arq.amanda> [--package <outro.amanda>] [--port 8080] [--host 127.0.0.1] [--conf-center F] [--conf-slope F] [--limiar-recusa F]\n");
    printf("                  [--cors ORIGEM] [--api-key CHAVE] [--api-key-file ARQ] [--max-body BYTES] [--max-conns N] [--workers N] [--eval-max N]\n");
    printf("                  [--config <arq.yaml>] [--ignore-calib]\n");
    printf("                  [--backend local|laya-http] [--laya-url URL] [--laya-timeout-ms MS] [--laya-max N]\n");
    printf("  (serve multi: --package repetivel ou nome=caminho; \"model\" seleciona o pacote; \"amanda\" = 1o)\n");
    printf("  amandac ask     --package <arq.amanda> \"pergunta\" [--top-k 3] [--json] [--backend local|laya-http] [--laya-url URL]\n");
    printf("                  [--conf-center F] [--conf-slope F] [--limiar-recusa F] [--ignore-calib]\n");
    printf("  amandac inspect --package <arq.amanda> [--stats] [--questions N] [--chunks N] [--json]\n");
    printf("  amandac eval    --package <arq.amanda> [--sample 0.1] [--seed 42] [--top-k 3] [--json] [--backend local|laya-http] [--laya-url URL]\n");
    printf("                  [--max-amostras N] [--conf-center F] [--conf-slope F] [--limiar-recusa F]\n");
    printf("  amandac calibrate --package <arq.amanda> [--sample 0.5] [--seed 42] [--json] [--apply] [--output <arq.amanda>]\n");
    printf("                  [--validacao <gold.json>] (repetivel; naturais como positivos + recall@1)\n");
    printf("  amandac version\n");
    printf("Entradas aceitas: .pdf .txt .csv .json\n");
    printf("Calibracao (Fase 6): --conf-center F --conf-slope F --limiar-recusa F (ask, eval)\n");
    printf("Fase 12.4: ask/eval/serve usam a calibracao gravada no pacote (v3) quando as flags\n");
    printf("  nao sao passadas; --ignore-calib forca o padrao historico (0.12/12.0/0.30).\n");
    printf("  calibrate --apply grava o sugerido no pacote (requer recompilar quem usa v1/v2).\n");
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
    /* Fase 10: limiares TJ (flag > config > padrao -100/+500) */
    {
        const char *e = flag_val(argc, argv, "--tj-espaco", NULL);
        const char *s = flag_val(argc, argv, "--tj-salto", NULL);
        float esp = e ? (float)atof(e) : (acfg.tem_extracao ? acfg.tj_espaco : -100.0f);
        float sal = s ? (float)atof(s) : (acfg.tem_extracao ? acfg.tj_salto : 500.0f);
        pdf_tj_config(esp, sal);
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
    pkg.num_paginas = doc->num_paginas;
    pkg.extra_blocos = doc->num_blocos;
    pkg.extra_total_streams = (int)doc->stats.total_streams;
    pkg.extra_text_streams = (int)doc->stats.text_streams;
    pkg.extra_failed = (int)(doc->stats.failed_inflate + doc->stats.failed_decode);
    pkg.extra_fallback = (int)doc->stats.used_fallback;

    int rc = empacotar_amanda(&pkg, output, &erro);
    long long t1 = now_ms();
    if (rc != 0) {
        fprintf(stderr, "compile: falha ao empacotar: %s\n", erro ? erro : "?");
        free(erro);
    } else {
        printf("compile ok: %s\n", output);
        printf("  blocos extraidos: %d (%d paginas)\n", doc->num_blocos, doc->num_paginas);
        printf("  extracao: streams=%d texto=%d falhas=%d fallback=%s\n",
               doc->stats.total_streams, doc->stats.text_streams,
               doc->stats.failed_inflate + doc->stats.failed_decode,
               doc->stats.used_fallback ? "sim" : "nao");
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

/* Fase 13: nome do pacote p/ roteamento: prefixo `nome=` ou basename sem extensao. */
static void pkg_name_from_path(const char *spec, char *name_out, size_t nn, const char **path_out) {
    const char *eq = strchr(spec, '=');
    /* `=` so vale como separador se houver caminho apos ele */
    if (eq && eq[1]) {
        size_t nl = (size_t)(eq - spec);
        if (nl >= nn) nl = nn - 1;
        memcpy(name_out, spec, nl);
        name_out[nl] = '\0';
        *path_out = eq + 1;
        return;
    }
    *path_out = spec;
    const char *b = strrchr(spec, '/');
    const char *b2 = strrchr(spec, '\\');
    if (b2 && (!b || b2 > b)) b = b2;
    b = b ? b + 1 : spec;
    snprintf(name_out, nn, "%s", b);
    char *dot = strrchr(name_out, '.');
    if (dot) *dot = '\0';
    if (!name_out[0]) snprintf(name_out, nn, "amanda");
}

static int cmd_serve(int argc, char **argv) {
    /* Fase 13: --package repetivel (ate 8), --config com secao servidor. */
    const char *cfg_path = flag_val(argc, argv, "--config", NULL);
    AmandaConfig acfg;
    config_defaults(&acfg);
    if (cfg_path) {
        char *cerr = NULL;
        if (config_ler(cfg_path, &acfg, &cerr) != 0) {
            fprintf(stderr, "serve: %s\n", cerr ? cerr : "?");
            free(cerr);
            return 1;
        }
    }
    /* coleta specs de pacotes: flags > config (pacote unico) */
    const char *specs[SRV_MAX_PKGS];
    int n_specs = 0;
    for (int i = 0; i < argc - 1 && n_specs < SRV_MAX_PKGS; i++) {
        if (strcmp(argv[i], "--package") == 0 && argv[i + 1])
            specs[n_specs++] = argv[i + 1];
    }
    if (n_specs == 0 && acfg.tem_servidor && acfg.srv_pacote[0])
        specs[n_specs++] = acfg.srv_pacote;
    if (n_specs == 0) { fprintf(stderr, "serve: --package obrigatorio\n"); return 2; }

    const char *host = flag_val(argc, argv, "--host", NULL);
    if (!host) host = acfg.tem_servidor && acfg.srv_host[0] ? acfg.srv_host : "127.0.0.1";
    const char *port_s = flag_val(argc, argv, "--port", NULL);
    int port = port_s ? atoi(port_s) : (acfg.tem_servidor && acfg.srv_port > 0 ? acfg.srv_port : 8080);

    AmandaPackage *pkgs[SRV_MAX_PKGS];
    char names[SRV_MAX_PKGS][128];
    const char *name_ptrs[SRV_MAX_PKGS];
    for (int i = 0; i < n_specs; i++) {
        const char *path = NULL;
        pkg_name_from_path(specs[i], names[i], sizeof names[i], &path);
        name_ptrs[i] = names[i];
        for (int j = 0; j < i; j++) {
            if (strcmp(names[j], names[i]) == 0) {
                fprintf(stderr, "serve: nome de pacote duplicado: %s (use nome=caminho)\n", names[i]);
                return 2;
            }
        }
        char *erro = NULL;
        pkgs[i] = carregar_amanda(path, &erro);
        if (!pkgs[i]) {
            fprintf(stderr, "serve: %s (%s)\n", erro ? erro : "?", path);
            free(erro);
            for (int j = 0; j < i; j++) liberar_package(pkgs[j]);
            return 1;
        }
    }
    if (flag_bool(argc, argv, "--ignore-calib")) {
        for (int i = 0; i < n_specs; i++) pkgs[i]->tem_calib = 0;
    }

    ServerConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.host = host; cfg.port = port;
    cfg.pkg = (n_specs == 1) ? pkgs[0] : NULL;
    cfg.pkgs = pkgs;
    cfg.pkg_names = name_ptrs;
    cfg.n_pkgs = n_specs;
    cfg.stop_flag = NULL;
    /* precedencia por campo: flag CLI > config > padrao */
    {
        const char *cc = flag_val(argc, argv, "--conf-center", NULL);
        const char *cs = flag_val(argc, argv, "--conf-slope", NULL);
        const char *lr = flag_val(argc, argv, "--limiar-recusa", NULL);
        if (cc) cfg.conf_center = (float)atof(cc);
        else if (acfg.tem_servidor && acfg.conf_center != 0.0f) cfg.conf_center = acfg.conf_center;
        if (cs) cfg.conf_slope = (float)atof(cs);
        else if (acfg.tem_servidor && acfg.conf_slope != 0.0f) cfg.conf_slope = acfg.conf_slope;
        if (lr) { cfg.limiar_recusa = (float)atof(lr); cfg.tem_limiar = 1; }
        else if (acfg.tem_limiar) { cfg.limiar_recusa = acfg.limiar_recusa; cfg.tem_limiar = 1; }
        /* Fase 7.5/13: robustez */
        const char *co = flag_val(argc, argv, "--cors", NULL);
        const char *ak = flag_val(argc, argv, "--api-key", NULL);
        const char *akf = flag_val(argc, argv, "--api-key-file", NULL);
        const char *mb = flag_val(argc, argv, "--max-body", NULL);
        const char *mc = flag_val(argc, argv, "--max-conns", NULL);
        const char *em = flag_val(argc, argv, "--eval-max", NULL);
        const char *wo = flag_val(argc, argv, "--workers", NULL);
        cfg.cors_origin = co ? co : (acfg.tem_servidor && acfg.srv_cors[0] ? acfg.srv_cors : NULL);
        if (!ak && acfg.tem_servidor && acfg.srv_api_key[0]) ak = acfg.srv_api_key;
        if (!akf && acfg.tem_servidor && acfg.srv_api_key_file[0]) akf = acfg.srv_api_key_file;
        /* Fase 13: nunca expor a chave (nem logar, nem ecoar em erro) */
        char *key = amanda_resolve_api_key(ak, akf);
        cfg.api_key = key;
        if (mb) cfg.max_body = atol(mb);
        else if (acfg.tem_servidor && acfg.srv_max_body > 0) cfg.max_body = acfg.srv_max_body;
        if (mc) cfg.max_conns = atoi(mc);
        else if (acfg.tem_servidor && acfg.srv_max_conns > 0) cfg.max_conns = acfg.srv_max_conns;
        if (em) cfg.eval_max = atoi(em);
        else if (acfg.tem_servidor && acfg.srv_eval_max > 0) cfg.eval_max = acfg.srv_eval_max;
        if (wo) cfg.workers = atoi(wo);
        else if (acfg.tem_servidor && acfg.srv_workers > 0) cfg.workers = acfg.srv_workers;
        /* Fase 11: inferencia LLM no serve */
        {
            const char *be = flag_val(argc, argv, "--backend", NULL);
            if (!be && acfg.tem_servidor && acfg.srv_backend[0]) be = acfg.srv_backend;
            const char *lu = flag_val(argc, argv, "--laya-url", NULL);
            if (!lu && acfg.tem_servidor && acfg.srv_laya_url[0]) lu = acfg.srv_laya_url;
            const char *lt = flag_val(argc, argv, "--laya-timeout-ms", NULL);
            const char *lm = flag_val(argc, argv, "--laya-max", NULL);
            if (be && (strcmp(be, "laya-http") == 0 || strcmp(be, "laya") == 0))
                cfg.backend = DECISION_BACKEND_LAYA_HTTP;
            if (lu) snprintf(cfg.laya_url, sizeof cfg.laya_url, "%s", lu);
            if (lt) cfg.laya_timeout_ms = atoi(lt);
            else if (acfg.tem_servidor && acfg.srv_laya_timeout_ms > 0) cfg.laya_timeout_ms = acfg.srv_laya_timeout_ms;
            if (lm) cfg.laya_max = atoi(lm);
            else if (acfg.tem_servidor && acfg.srv_laya_max > 0) cfg.laya_max = acfg.srv_laya_max;
        }
    }
    int rc = server_run(&cfg);
    free((void *)cfg.api_key);
    for (int i = 0; i < n_specs; i++) liberar_package(pkgs[i]);
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
    cfg.limiar_confianca = 0.7f;
    cfg.top_k = topk > 0 ? topk : 3;
    fill_backend(&cfg, argc, argv);
    fill_calib(&cfg, argc, argv);
    /* Fase 12.4: pacote v3 vence o padrao; flag CLI vence o pacote. */
    if (!flag_bool(argc, argv, "--ignore-calib"))
        decisao_usar_calib_pacote(&cfg, pkg->tem_calib,
                                  pkg->cal_center, pkg->cal_slope, pkg->cal_limiar);
    if (cfg.limiar_recusa == 0.0f) cfg.limiar_recusa = 0.3f;
    int via_laya = 0;
    RetrievalIndex *rix = indice_criar(pkg->chunks, pkg->num_chunks);
    Decisao *d = executar_decisao_hibrida_idx(q, rix, pkg->chunks, pkg->num_chunks, pkg->embeddings, &cfg, &via_laya);
    indice_liberar(rix);
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
    if (flag_bool(argc, argv, "--json")) {
        char *j = package_stats_json(pkg);
        printf("%s\n", j);
        free(j);
        liberar_package(pkg);
        return 0;
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
    if (flag_bool(argc, argv, "--ignore-calib") && pkg) pkg->tem_calib = 0;
    {
        const char *ma = flag_val(argc, argv, "--max-amostras", NULL);
        if (ma) cfg.max_amostras = atoi(ma);
    }
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
    int apply = flag_bool(argc, argv, "--apply");
    const char *outpath = flag_val(argc, argv, "--output", NULL);
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
    /* Fase 12.4b: --validacao repetivel (gold JSON com naturais). */
    for (int i = 0; i < argc - 1 && cfg.n_validacao < 8; i++) {
        if (strcmp(argv[i], "--validacao") == 0 && argv[i + 1]) {
            snprintf(cfg.validacao[cfg.n_validacao],
                     sizeof cfg.validacao[0], "%s", argv[i + 1]);
            cfg.n_validacao++;
        }
    }
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
    /* Fase 12.4: --apply grava o sugerido no pacote (formato v3).
       Sem --output, reescreve o proprio --package. */
    if (apply) {
        const char *dest = (outpath && outpath[0]) ? outpath : pack;
        pkg->tem_calib = 1;
        pkg->cal_center = rep.sug_center;
        pkg->cal_slope = rep.sug_slope;
        pkg->cal_limiar = rep.sug_limiar;
        char *werr = NULL;
        if (empacotar_amanda(pkg, dest, &werr) != 0) {
            fprintf(stderr, "calibrate: falha ao gravar: %s\n", werr ? werr : "?");
            free(werr);
            liberar_package(pkg);
            return 1;
        }
        printf("calibrate: gravado em %s (center=%.3f slope=%.1f limiar=%.2f, formato v%d)\n",
               dest, rep.sug_center, rep.sug_slope, rep.sug_limiar, AMANDA_FORMAT_VERSION);
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
