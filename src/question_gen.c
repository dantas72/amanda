#include "question_gen.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

const char *tipo_pergunta_str(TipoPergunta t) {
    switch (t) {
    case TIPO_CHOICE: return "choice";
    case TIPO_SCORE: return "score";
    case TIPO_NOUL: return "noul";
    default: return "unknown";
    }
}

#define FMT_CHOICE "Qual e o elemento central citado no trecho: \"{{trecho}}\"?"
#define FMT_SCORE "Em uma escala de {{min}} a {{max}}, qual a relevancia do trecho \"{{trecho}}\" para o tema do documento?"
#define FMT_NOUL "Segundo o documento, a afirmacao \"{{afirmacao}}\" e verdadeira?"

void templates_padrao(QuestionTemplates *t) {
    memset(t, 0, sizeof *t);
    snprintf(t->choice, sizeof t->choice, "%s", FMT_CHOICE);
    snprintf(t->score, sizeof t->score, "%s", FMT_SCORE);
    snprintf(t->noul, sizeof t->noul, "%s", FMT_NOUL);
    t->ok = 0;
}

/* substitui {{chave}} por valor; chaves desconhecidas ficam como estao */
static char *render_fmt(const char *fmt, const char *keys[], const char *vals[], int n) {
    ByteBuf b; buf_init(&b);
    for (const char *p = fmt; *p; ) {
        if (p[0] == '{' && p[1] == '{') {
            const char *fim = strstr(p + 2, "}}");
            if (!fim) { buf_append(&b, p, strlen(p)); break; }
            size_t kl = (size_t)(fim - (p + 2));
            char key[32];
            if (kl >= sizeof key) kl = sizeof key - 1;
            memcpy(key, p + 2, kl);
            key[kl] = '\0';
            const char *rep = NULL;
            for (int i = 0; i < n; i++)
                if (strcmp(keys[i], key) == 0) { rep = vals[i]; break; }
            if (rep) buf_append(&b, rep, strlen(rep));
            else buf_append(&b, p, (size_t)(fim + 2 - p));
            p = fim + 2;
        } else {
            buf_append(&b, p, 1);
            p++;
        }
    }
    buf_reserve(&b, 1);
    b.data[b.len] = '\0';
    return (char *)b.data;
}

/* extrai "enunciado" de um .tpl JSON-ish (com unescape); se nao houver,
   usa o arquivo inteiro aparado como formato */
static char *tpl_enunciado(const char *texto) {
    const char *p = strstr(texto, "\"enunciado\"");
    if (p) {
        p = strchr(p + 11, ':');
        if (p) {
            p++;
            while (*p && isspace((unsigned char)*p)) p++;
            if (*p == '"') {
                p++;
                ByteBuf b; buf_init(&b);
                while (*p && *p != '"') {
                    if (*p == '\\' && p[1]) {
                        char e = p[1];
                        if (e == 'n') buf_append(&b, "\n", 1);
                        else if (e == 't') buf_append(&b, "\t", 1);
                        else if (e == 'r') buf_append(&b, "\r", 1);
                        else buf_append(&b, &e, 1);
                        p += 2;
                    } else {
                        buf_append(&b, p, 1);
                        p++;
                    }
                }
                buf_reserve(&b, 1);
                b.data[b.len] = '\0';
                return (char *)b.data;
            }
        }
    }
    char *t = xstrdup(texto);
    str_trim(t);
    return t;
}

static int tpl_ler_arquivo(const char *dir, const char *nome, char *dst, size_t ndst) {
    char path[1152];
    snprintf(path, sizeof path, "%s/%s", dir, nome);
    char *txt = read_file_text(path);
    if (!txt) {
        snprintf(path, sizeof path, "%s\\%s", dir, nome);
        txt = read_file_text(path);
    }
    if (!txt) return 0;
    char *en = tpl_enunciado(txt);
    free(txt);
    if (!en || !en[0]) { free(en); return 0; }
    snprintf(dst, ndst, "%s", en);
    free(en);
    return 1;
}

int carregar_templates(const char *dir, QuestionTemplates *out) {
    templates_padrao(out);
    if (!dir || !dir[0]) return 0;
    int n = 0;
    char buf[2048];
    if (tpl_ler_arquivo(dir, "question_choice.tpl", buf, sizeof buf)) {
        snprintf(out->choice, sizeof out->choice, "%s", buf); n++;
    }
    if (tpl_ler_arquivo(dir, "question_score.tpl", buf, sizeof buf)) {
        snprintf(out->score, sizeof out->score, "%s", buf); n++;
    }
    if (tpl_ler_arquivo(dir, "question_noul.tpl", buf, sizeof buf)) {
        snprintf(out->noul, sizeof out->noul, "%s", buf); n++;
    }
    out->ok = n > 0 ? 1 : 0;
    return out->ok;
}

/* divide texto em sentencas */
static char **split_sentences(const char *texto, int *n_out) {
    int cap = 16, n = 0;
    char **s = (char **)xmalloc(sizeof(char *) * (size_t)cap);
    ByteBuf cur; buf_init(&cur);
    for (const char *p = texto; ; p++) {
        char c = *p;
        int fim = (c == '\0');
        if (!fim) buf_append(&cur, &c, 1);
        int boundary = fim || ((c == '.' || c == '!' || c == '?' || c == ';') &&
                               (p[1] == '\0' || isspace((unsigned char)p[1]) || p[1] == '"'));
        if (boundary) {
            buf_reserve(&cur, 1);
            cur.data[cur.len] = '\0';
            char *t = xstrdup(str_trim((char *)cur.data));
            if (strlen(t) >= 20) {
                if (n >= cap) { cap *= 2; s = (char **)xrealloc(s, sizeof(char *) * (size_t)cap); }
                s[n++] = t;
            } else free(t);
            cur.len = 0;
        }
        if (fim) break;
    }
    buf_free(&cur);
    *n_out = n;
    return s;
}

/* entidade candidata: sequencia de palavras capitalizadas ou termos entre aspas/numeros */
static int is_cap_word(const char *w) {
    if (!w || !w[0]) return 0;
    unsigned char c = (unsigned char)w[0];
    if (c >= 'A' && c <= 'Z') return 1;
    if (c >= 128) return 1; /* acentuada maiuscula aproximada */
    if (c >= '0' && c <= '9') return 1;
    return 0;
}

static char *shorten(const char *s, size_t maxn) {
    if (strlen(s) <= maxn) return xstrdup(s);
    char *p = xstrndup(s, maxn);
    char *sp = strrchr(p, ' ');
    if (sp && (size_t)(sp - p) > maxn / 2) *sp = '\0';
    return p;
}

static void push_pergunta(PerguntaTipada **arr, int *n, int *cap, PerguntaTipada *q) {
    if (*n >= *cap) {
        *cap = *cap ? *cap * 2 : 16;
        *arr = (PerguntaTipada *)xrealloc(*arr, sizeof(PerguntaTipada) * (size_t)*cap);
    }
    (*arr)[(*n)++] = *q;
}

PerguntaTipada *gerar_perguntas_tpl(Chunk *chunks, int num_chunks,
                                    const QuestionGenConfig *cfg,
                                    const QuestionTemplates *tpl_in,
                                    int *num_perguntas) {
    QuestionGenConfig c = {3, 2, 5};
    if (cfg) c = *cfg;
    QuestionTemplates tdef;
    templates_padrao(&tdef);
    const QuestionTemplates *tpl = tpl_in ? tpl_in : &tdef;
    PerguntaTipada *out = NULL;
    int n = 0, cap = 0;

    /* coleta entidades globais para distratores */
    int gcap = 64, gn = 0;
    char **glob = (char **)xmalloc(sizeof(char *) * (size_t)gcap);
    for (int i = 0; i < num_chunks; i++) {
        int ns = 0;
        char **sents = split_sentences(chunks[i].texto, &ns);
        for (int k = 0; k < ns; k++) {
            char *copy = xstrdup(sents[k]);
            for (char *p = copy; *p; p++) if (*p == ',' || *p == ':' || *p == '(' || *p == ')') *p = ' ';
            /* extrai sequencias capitalizadas */
            char *tok = strtok(copy, " \t\r\n\"'");
            ByteBuf ent; buf_init(&ent);
            int ent_w = 0;
            while (tok) {
                if (is_cap_word(tok) && strlen(tok) >= 3) {
                    if (ent_w) buf_append(&ent, " ", 1);
                    buf_append(&ent, tok, strlen(tok));
                    ent_w++;
                } else {
                    if (ent_w >= 1 && ent.len >= 3) {
                        buf_reserve(&ent, 1); ent.data[ent.len] = '\0';
                        if (gn >= gcap) { gcap *= 2; glob = (char **)xrealloc(glob, sizeof(char *) * (size_t)gcap); }
                        glob[gn++] = xstrdup((char *)ent.data);
                    }
                    ent.len = 0; ent_w = 0;
                }
                tok = strtok(NULL, " \t\r\n\"'");
            }
            if (ent_w >= 1 && ent.len >= 3) {
                buf_reserve(&ent, 1); ent.data[ent.len] = '\0';
                if (gn >= gcap) { gcap *= 2; glob = (char **)xrealloc(glob, sizeof(char *) * (size_t)gcap); }
                glob[gn++] = xstrdup((char *)ent.data);
            }
            buf_free(&ent);
            free(copy);
        }
        for (int k = 0; k < ns; k++) free(sents[k]);
        free(sents);
    }

    for (int i = 0; i < num_chunks; i++) {
        int ns = 0;
        char **sents = split_sentences(chunks[i].texto, &ns);
        int made_choice = 0, made_score = 0, made_noul = 0;

        /* NOUL: afirmacoes = primeiras sentencas */
        for (int k = 0; k < ns && made_noul < c.max_noul; k++) {
            PerguntaTipada q;
            memset(&q, 0, sizeof q);
            q.tipo = TIPO_NOUL;
            char *curto = shorten(sents[k], 220);
            const char *keys[] = {"afirmacao", "pagina"};
            char pgnum[32];
            snprintf(pgnum, sizeof pgnum, "%d", chunks[i].pagina_inicio);
            const char *vals[] = {curto, pgnum};
            q.enunciado = render_fmt(tpl->noul[0] ? tpl->noul : FMT_NOUL, keys, vals, 2);
            q.afirmacao = xstrdup(sents[k]);
            q.pagina_fonte = chunks[i].pagina_inicio;
            q.chunk_hash = xstrdup(chunks[i].hash);
            push_pergunta(&out, &n, &cap, &q);
            free(curto);
            made_noul++;
        }

        /* CHOICE: usa sentencas com entidades locais */
        for (int k = 0; k < ns && made_choice < c.max_choice; k++) {
            /* encontra entidade local: primeira palavra capitalizada longa */
            char *copy = xstrdup(sents[k]);
            char *best = NULL;
            char *tok = strtok(copy, " \t\r\n\"'(),.:;");
            while (tok) {
                if (is_cap_word(tok) && strlen(tok) >= 4) { best = tok; break; }
                tok = strtok(NULL, " \t\r\n\"'(),.:;");
            }
            if (!best) { free(copy); continue; }
            char *ent = xstrdup(best);
            free(copy);

            PerguntaTipada q;
            memset(&q, 0, sizeof q);
            q.tipo = TIPO_CHOICE;
            char *ctx = shorten(sents[k], 160);
            {
                const char *keys[] = {"trecho", "pagina"};
                char pgnum[32];
                snprintf(pgnum, sizeof pgnum, "%d", chunks[i].pagina_inicio);
                const char *vals[] = {ctx, pgnum};
                q.enunciado = render_fmt(tpl->choice[0] ? tpl->choice : FMT_CHOICE, keys, vals, 2);
            }
            free(ctx);
            q.num_opcoes = 4;
            q.opcoes = (char **)xcalloc(4, sizeof(char *));
            q.opcoes[0] = ent;
            int doi = 1;
            for (int g = 0; g < gn && doi < 4; g++) {
                if (strcmp(glob[g], ent) == 0) continue;
                int dup = 0;
                for (int z = 0; z < doi; z++) if (strcmp(q.opcoes[z], glob[g]) == 0) { dup = 1; break; }
                if (!dup) q.opcoes[doi++] = xstrdup(glob[g]);
            }
            while (doi < 4) {
                char tmp[64];
                snprintf(tmp, sizeof tmp, "Alternativa %d (nao consta no trecho)", doi + 1);
                q.opcoes[doi++] = xstrdup(tmp);
            }
            /* embaralha deterministico por hash */
            uint32_t h = fnv1a_32((unsigned char *)chunks[i].hash, strlen(chunks[i].hash));
            h ^= (uint32_t)k * 2654435761u;
            for (int z = 3; z > 0; z--) {
                int j = (int)(h % (uint32_t)(z + 1));
                char *tt = q.opcoes[z]; q.opcoes[z] = q.opcoes[j]; q.opcoes[j] = tt;
                h = h * 1664525u + 1013904223u;
            }
            q.pagina_fonte = chunks[i].pagina_inicio;
            q.chunk_hash = xstrdup(chunks[i].hash);
            push_pergunta(&out, &n, &cap, &q);
            made_choice++;
        }

        /* SCORE: rubrica sobre o chunk */
        for (int k = 0; k < 1 && made_score < c.max_score; k++) {
            PerguntaTipada q;
            memset(&q, 0, sizeof q);
            q.tipo = TIPO_SCORE;
            char *ctx = shorten(chunks[i].texto, 160);
            {
                const char *keys[] = {"trecho", "min", "max", "pagina"};
                char pgnum[32];
                snprintf(pgnum, sizeof pgnum, "%d", chunks[i].pagina_inicio);
                const char *vals[] = {ctx, "0", "10", pgnum};
                q.enunciado = render_fmt(tpl->score[0] ? tpl->score : FMT_SCORE, keys, vals, 4);
            }
            free(ctx);
            q.min = 0; q.max = 10;
            q.pagina_fonte = chunks[i].pagina_inicio;
            q.chunk_hash = xstrdup(chunks[i].hash);
            push_pergunta(&out, &n, &cap, &q);
            made_score++;
            (void)k;
        }

        for (int k = 0; k < ns; k++) free(sents[k]);
        free(sents);
    }

    for (int i = 0; i < gn; i++) free(glob[i]);
    free(glob);
    *num_perguntas = n;
    return out;
}

PerguntaTipada *gerar_perguntas(Chunk *chunks, int num_chunks,
                                 const QuestionGenConfig *cfg, int *num_perguntas) {
    return gerar_perguntas_tpl(chunks, num_chunks, cfg, NULL, num_perguntas);
}

void liberar_perguntas(PerguntaTipada *p, int n) {
    if (!p) return;
    for (int i = 0; i < n; i++) {
        free(p[i].enunciado);
        for (int k = 0; k < p[i].num_opcoes; k++) free(p[i].opcoes[k]);
        free(p[i].opcoes);
        free(p[i].afirmacao);
        free(p[i].chunk_hash);
    }
    free(p);
}
