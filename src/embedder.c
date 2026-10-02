#include "embedder.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

/* Fase 12.2: stopwords PT (forma ja normalizada, sem acento, minuscula).
   Aplicadas em tokenizar(): valem para embeddings e retrieval juntos. */
static const char *PT_STOPWORDS[] = {
    "a", "ao", "aos", "aquela", "aquelas", "aquele", "aqueles", "aquilo",
    "as", "ate", "com", "como", "da", "das", "de", "dela", "delas", "dele",
    "deles", "depois", "do", "dos", "e", "ela", "elas", "ele", "eles",
    "em", "entre", "era", "eram", "essa", "essas", "esse", "esses",
    "esta", "estamos", "estas", "estava", "estavam", "este", "estes",
    "estou", "eu", "foi", "foram", "ha", "isso", "isto", "ja", "lhe",
    "lhes", "mais", "mas", "me", "mesmo", "meu", "meus", "minha",
    "minhas", "muito", "na", "nao", "nas", "nem", "no", "nos", "nossa",
    "nossas", "nosso", "nossos", "num", "numa", "o", "os", "ou", "para",
    "pela", "pelas", "pelo", "pelos", "por", "qual", "quando", "que",
    "quem", "se", "seja", "sejam", "sem", "somos", "sou", "sua", "suas",
    "tambem", "te", "tem", "tendo", "tenha", "ter", "teu", "teus", "ti",
    "tinha", "tive", "tivemos", "tiver", "tiverao", "tu", "tua", "tuas",
    "um", "uma", "umas", "uns", "voce", "voces", "vos", "vosso", "vossos",
    "d", "s", "n", "sim", "sera", "serao", "foi", "ser", "estar", "estao",
    "dos", "das", "nas", "nos", "aos", "pelo", "pela", "isto", "isso",
    "nao", "sim", "mas", "porque", "pois", "entao", "assim", "onde",
    "cada", "outra", "outras", "outro", "outros", "toda", "todas", "todo",
    "todos", "muita", "muitas", "muitos", "pouca", "poucas", "pouco",
    "poucos", "tanto", "tanta", "seu", "seus", "min", "etc"
};
static const int PT_NSTOP = (int)(sizeof(PT_STOPWORDS) / sizeof(PT_STOPWORDS[0]));

static int eh_stopword(const char *tok) {
    for (int i = 0; i < PT_NSTOP; i++)
        if (strcmp(tok, PT_STOPWORDS[i]) == 0) return 1;
    return 0;
}

/* dobra UTF-8/Latin1 acentuado para ASCII base; retorna nbytes consumidos
   (1 ou 2) e escreve 1-2 chars em out (sem NUL). Retorna 0 se separador. */
static int dobra_acento(const unsigned char *p, char out[2]) {
    unsigned char c = p[0];
    if (c < 128) {
        if (isalnum(c)) { out[0] = (char)tolower(c); return 1; }
        return 0;
    }
    if (c == 0xC3) {
        unsigned char d = p[1];
        if (d == 0) return 0;
        switch (d) {
            case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85:
            case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: case 0xA5:
                out[0] = 'a'; return 2;
            case 0x86: out[0] = 'a'; out[1] = 'e'; return -2;
            case 0xA6: out[0] = 'a'; out[1] = 'e'; return -2;
            case 0x87: case 0xA7: out[0] = 'c'; return 2;
            case 0x88: case 0x89: case 0x8A: case 0x8B:
            case 0xA8: case 0xA9: case 0xAA: case 0xAB:
                out[0] = 'e'; return 2;
            case 0x8C: case 0x8D: case 0x8E: case 0x8F:
            case 0xAC: case 0xAD: case 0xAE: case 0xAF:
                out[0] = 'i'; return 2;
            case 0x90: out[0] = 'd'; return 2;
            case 0x91: case 0xB1: out[0] = 'n'; return 2;
            case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x98:
            case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB8:
                out[0] = 'o'; return 2;
            case 0x99: case 0x9A: case 0x9B: case 0x9C:
            case 0xB9: case 0xBA: case 0xBB: case 0xBC:
                out[0] = 'u'; return 2;
            case 0x9D: case 0xBD: case 0xBF: out[0] = 'y'; return 2;
            case 0x9E: out[0] = 't'; return 2;
            case 0x9F: out[0] = 's'; out[1] = 's'; return -2;
            default: return 0;
        }
    }
    if (c >= 0xC0) {
        /* Latin1 avulso (tolerancia): faixa acentuada vira base */
        if ((c >= 0xC0 && c <= 0xC5) || (c >= 0xE0 && c <= 0xE5)) { out[0] = 'a'; return 1; }
        if (c == 0xC7 || c == 0xE7) { out[0] = 'c'; return 1; }
        if ((c >= 0xC8 && c <= 0xCB) || (c >= 0xE8 && c <= 0xEB)) { out[0] = 'e'; return 1; }
        if ((c >= 0xCC && c <= 0xCF) || (c >= 0xEC && c <= 0xEF)) { out[0] = 'i'; return 1; }
        if (c == 0xD1 || c == 0xF1) { out[0] = 'n'; return 1; }
        if ((c >= 0xD2 && c <= 0xD6) || (c >= 0xF2 && c <= 0xF6)) { out[0] = 'o'; return 1; }
        if ((c >= 0xD9 && c <= 0xDC) || (c >= 0xF9 && c <= 0xFC)) { out[0] = 'u'; return 1; }
        if (c == 0xDD || c == 0xFD || c == 0xFF) { out[0] = 'y'; return 1; }
        return 0;
    }
    return 0;
}

static void tokenizar_emit(char ***toks, int *n, int *cap, ByteBuf *cur) {
    if (cur->len < 2) { cur->len = 0; return; }
    buf_reserve(cur, 1);
    cur->data[cur->len] = '\0';
    /* Fase 12.3 filtro junk: token gigante ou com char 5x repetido
       (sujeira binaria de PDF: "AAAA...", "$$$") nunca vira termo. */
    size_t L = cur->len;
    if (L > 30) { cur->len = 0; return; }
    {
        int run = 1;
        for (size_t i = 1; i < L; i++) {
            if (cur->data[i] == cur->data[i - 1]) {
                if (++run >= 5) { cur->len = 0; return; }
            } else run = 1;
        }
    }
    if (eh_stopword((char *)cur->data)) { cur->len = 0; return; }
    /* Fase 12.3 stemming PT conservador (ordem fixa, 1 passada,
       travas de tamanho; deterministico: preserva matches exatos). */
    {
        char *w = (char *)cur->data;
        size_t l = strlen(w);
        int feito = 0;
        /* -mente: rapidamente -> rapida */
        if (!feito && l > 4 + 3 && strcmp(w + l - 5, "mente") == 0) { w[l - 5] = '\0'; l -= 5; feito = 1; }
        /* -oes -> -ao: acoes -> acao, funcoes -> funcao */
        if (!feito && l > 3 && strcmp(w + l - 3, "oes") == 0) {
            w[l - 3] = '\0'; strcat(w, "ao"); l = strlen(w); feito = 1;
        }
        /* -ais/-eis/-ois/-uis -> -al/-el/-ol/-ul: profissionais -> profissional
           (trava l>4 protege "pais", que vai a "paal" por essa regra) */
        if (!feito && l > 4) {
            const char *suf = w + l - 3;
            char mapa = 0;
            if (strcmp(suf, "ais") == 0) mapa = 'a';
            else if (strcmp(suf, "eis") == 0) mapa = 'e';
            else if (strcmp(suf, "ois") == 0) mapa = 'o';
            else if (strcmp(suf, "uis") == 0) mapa = 'u';
            if (mapa) { w[l - 3] = mapa; w[l - 2] = 'l'; w[l - 1] = '\0'; l -= 1; feito = 1; }
        }
        /* -ens -> -em: homens -> homem, bens -> bem */
        if (!feito && l > 3 && strcmp(w + l - 3, "ens") == 0) {
            w[l - 1] = 'm'; w[l] = '\0'; l -= 1; feito = 1;
        }
        /* -es apos consoante: investidores -> investidor, meses -> mes */
        if (!feito && l > 4 && strcmp(w + l - 2, "es") == 0) {
            char c = w[l - 3];
            if (!(c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u')) {
                w[l - 2] = '\0'; l -= 2; feito = 1;
            }
        }
        /* plural -s: contratos -> contrato, anos -> ano (nunca apos 's') */
        if (!feito && l > 3 && w[l - 1] == 's' && w[l - 2] != 's') {
            w[l - 1] = '\0'; l -= 1; feito = 1;
        }
        /* infinitivo -ar/-er/-ir: falar -> fala (trava protege "mar"/"ser") */
        if (!feito && l > 4 && w[l - 1] == 'r' &&
            ((w[l - 2] == 'a') || (w[l - 2] == 'e') || (w[l - 2] == 'i'))) {
            w[l - 1] = '\0'; l -= 1; feito = 1;
        }
        /* gerundio -ndo: falando -> fala */
        if (!feito && l > 6 && strcmp(w + l - 3, "ndo") == 0) {
            w[l - 3] = '\0'; l -= 3; feito = 1;
        }
        /* participio -ado/-ido: assinado -> assina (trava protege "dado") */
        if (!feito && l > 5 &&
            ((strcmp(w + l - 3, "ado") == 0) || (strcmp(w + l - 3, "ido") == 0))) {
            w[l - 2] = '\0'; l -= 2; feito = 1;
        }
        (void)feito;
    }
    if (*n >= *cap) { *cap *= 2; *toks = (char **)xrealloc(*toks, sizeof(char *) * (size_t)*cap); }
    (*toks)[(*n)++] = xstrdup((char *)cur->data);
    cur->len = 0;
}

char **tokenizar(const char *texto, int *n_out) {
    int cap = 32, n = 0;
    char **t = (char **)xmalloc(sizeof(char *) * (size_t)cap);
    const unsigned char *p = (const unsigned char *)(texto ? texto : "");
    ByteBuf cur; buf_init(&cur);
    while (1) {
        unsigned char c = *p;
        if (c == '\0') { tokenizar_emit(&t, &n, &cap, &cur); break; }
        char dob[2]; int adv = dobra_acento(p, dob);
        if (adv == 1) {
            buf_append(&cur, &dob[0], 1);
            p += 1;
        } else if (adv == -2) {
            buf_append(&cur, &dob[0], 1);
            buf_append(&cur, &dob[1], 1);
            p += 2;
        } else if (adv == 2) {
            buf_append(&cur, &dob[0], 1);
            p += 2;
        } else {
            /* separador (espaco, pontuacao, UTF-8 desconhecido):
               sequencia multibyte desconhecida consome toda ela */
            tokenizar_emit(&t, &n, &cap, &cur);
            if (c >= 128) {
                if ((c & 0xE0) == 0xC0) p += 2;
                else if ((c & 0xF0) == 0xE0) p += 3;
                else if ((c & 0xF8) == 0xF0) p += 4;
                else p += 1;
            } else {
                p += 1;
            }
        }
    }
    buf_free(&cur);
    *n_out = n;
    return t;
}

void liberar_tokens(char **toks, int n) {
    for (int i = 0; i < n; i++) free(toks[i]);
    free(toks);
}

void normalizar_vetor(float *v, int dim) {
    double s = 0;
    for (int i = 0; i < dim; i++) s += (double)v[i] * v[i];
    if (s <= 0) return;
    float inv = (float)(1.0 / sqrt(s));
    for (int i = 0; i < dim; i++) v[i] *= inv;
}

float cos_sim(const float *a, const float *b, int dim) {
    double s = 0;
    for (int i = 0; i < dim; i++) s += (double)a[i] * b[i];
    return (float)s;
}

static void embed_texto(const char *texto, float *vec, int dim) {
    for (int i = 0; i < dim; i++) vec[i] = 0;
    int n = 0;
    char **toks = tokenizar(texto, &n);
    /* unigramas + bigramas com hash */
    for (int i = 0; i < n; i++) {
        uint32_t h = fnv1a_32((unsigned char *)toks[i], strlen(toks[i]));
        vec[h % (uint32_t)dim] += 1.0f;
        if (i + 1 < n) {
            char bg[256];
            snprintf(bg, sizeof bg, "%s %s", toks[i], toks[i+1]);
            uint32_t h2 = fnv1a_32((unsigned char *)bg, strlen(bg));
            vec[h2 % (uint32_t)dim] += 0.5f;
        }
    }
    /* ponderacao log-tf */
    for (int i = 0; i < dim; i++)
        if (vec[i] > 0) vec[i] = 1.0f + logf(vec[i]);
    normalizar_vetor(vec, dim);
    liberar_tokens(toks, n);
}

Embeddings *gerar_embeddings(Chunk *chunks, int num_chunks) {
    Embeddings *e = (Embeddings *)xcalloc(1, sizeof(*e));
    e->num_vetores = num_chunks;
    e->dimensao = EMBEDDER_DIM;
    if (num_chunks == 0) return e;
    e->vetores = (float *)xcalloc((size_t)num_chunks * EMBEDDER_DIM, sizeof(float));
    for (int i = 0; i < num_chunks; i++)
        embed_texto(chunks[i].texto, e->vetores + (size_t)i * EMBEDDER_DIM, EMBEDDER_DIM);
    return e;
}

Embeddings *embed_query(const char *texto) {
    Embeddings *e = (Embeddings *)xcalloc(1, sizeof(*e));
    e->num_vetores = 1;
    e->dimensao = EMBEDDER_DIM;
    e->vetores = (float *)xcalloc(EMBEDDER_DIM, sizeof(float));
    embed_texto(texto, e->vetores, EMBEDDER_DIM);
    return e;
}

void liberar_embeddings(Embeddings *e) {
    if (!e) return;
    free(e->vetores);
    free(e);
}
