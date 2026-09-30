o Projeto Amanda: um compilador de conhecimento em C que transforma um PDF, JSON, .TXT, .CSV em um artefato de decisão especializado (.amanda), pronto para ser servido localmente e consumido por CLIs, OpenCode ou qualquer cliente compatível com a API OpenAI. A arquitetura segue a lógica que discutimos: o PDF é compilado em estados e perguntas tipadas, e um motor de decisão (como o Laya, que já tem implementação em GGUF) devolve probabilidades calibradas.

📁 Estrutura de Pastas do Projeto
text
amanda/
├── CMakeLists.txt
├── README.md
├── LICENSE
├── .gitignore
├── docs/
│   ├── formato_amanda.md
│   ├── pipeline.md
│   └── api.md
├── include/
│   ├── amanda.h
│   ├── pdf_extractor.h
│   ├── chunker.h
│   ├── embedder.h
│   ├── question_gen.h
│   ├── decision_engine.h
│   ├── packager.h
│   ├── server.h
│   └── cli.h
├── src/
│   ├── main.c
│   ├── pdf_extractor.c
│   ├── chunker.c
│   ├── embedder.c
│   ├── question_gen.c
│   ├── decision_engine.c
│   ├── packager.c
│   ├── server.c
│   ├── cli.c
│   └── utils.c
├── third_party/
│   ├── mupdf/          # ou poppler
│   ├── onnxruntime/    # para embeddings
│   └── laya/           # runtime GGUF para Laya
├── models/
│   ├── laya-421m.gguf
│   └── embedding-model.gguf
├── templates/
│   ├── question_choice.tpl
│   ├── question_score.tpl
│   └── question_noul.tpl
├── examples/
│   ├── exemplo.pdf
│   ├── config.yaml
│   └── perguntas_teste.json
├── tests/
│   ├── test_pdf_extractor.c
│   ├── test_question_gen.c
│   ├── test_decision_engine.c
│   └── run_tests.sh
└── scripts/
    ├── build.sh
    ├── compile_pdf.sh
    └── serve.sh

    🧱 Componentes Principais (com Trechos de Código)
1. pdf_extractor.c — Extração de Texto e Estrutura
Usaremos MuPDF (leve, licença AGPL) ou Poppler (GPL). MuPDF tem API C direta para leitura de texto.

c
// include/pdf_extractor.h
#ifndef PDF_EXTRACTOR_H
#define PDF_EXTRACTOR_H

#include <mupdf/fitz.h>

typedef struct {
    char *texto;
    int pagina;
    float x, y, largura, altura;
} BlocoTexto;

typedef struct {
    BlocoTexto *blocos;
    int num_blocos;
    int num_paginas;
} DocumentoExtraido;

DocumentoExtraido* extrair_pdf(const char *caminho);
void liberar_documento(DocumentoExtraido *doc);

#endif
c
// src/pdf_extractor.c (trecho)
#include "pdf_extractor.h"
#include <stdio.h>

DocumentoExtraido* extrair_pdf(const char *caminho) {
    fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    fz_register_document_handlers(ctx);

    fz_document *doc = fz_open_document(ctx, caminho);
    int num_paginas = fz_count_pages(ctx, doc);

    DocumentoExtraido *resultado = calloc(1, sizeof(DocumentoExtraido));
    resultado->num_paginas = num_paginas;

    // ... iterar páginas, extrair texto com fz_new_stext_page ...
    // Para cada página, percorrer blocos e armazenar em resultado->blocos.

    fz_drop_document(ctx, doc);
    fz_drop_context(ctx);
    return resultado;
}
2. chunker.c — Segmentação Semântica
Divide o texto em chunks com sobreposição, preservando citações (página, parágrafo).

c
// include/chunker.h
typedef struct {
    char *texto;
    int pagina_inicio;
    int pagina_fim;
    char *hash;
} Chunk;

Chunk* dividir_em_chunks(DocumentoExtraido *doc, int tamanho_max, int sobreposicao, int *num_chunks);
3. embedder.c — Geração de Embeddings
Usa ONNX Runtime com um modelo de embedding (ex.: all-MiniLM-L6-v2) ou llama.cpp com modelo GGUF.

c
// include/embedder.h
typedef struct {
    float *vetores;      // matriz num_chunks x dim
    int num_vetores;
    int dimensao;
} Embeddings;

Embeddings* gerar_embeddings(Chunk *chunks, int num_chunks, const char *modelo_onnx);
c
// src/embedder.c (trecho)
#include <onnxruntime_c_api.h>

Embeddings* gerar_embeddings(Chunk *chunks, int num_chunks, const char *modelo) {
    const OrtApi *ort = OrtGetApiBase()->GetApi(ORT_API_VERSION);
    OrtEnv *env;
    ort->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "amanda", &env);

    OrtSessionOptions *opts;
    ort->CreateSessionOptions(&opts);
    OrtSession *session;
    ort->CreateSession(env, modelo, opts, &session);

    // ... tokenizar, executar inferência, normalizar vetores ...
}
4. question_gen.c — Geração de Perguntas Tipadas
Este é o coração do compilador. A partir de cada chunk, geramos três tipos de perguntas:

Choice: “Qual é a classificação de X?” (com opções extraídas do texto)

Score: “Qual a pontuação de X numa escala de 0 a 10?”

Noul: “A afirmação ‘X’ é verdadeira segundo o documento?”

c
// include/question_gen.h
typedef enum { TIPO_CHOICE, TIPO_SCORE, TIPO_NOUL } TipoPergunta;

typedef struct {
    TipoPergunta tipo;
    char *enunciado;
    char **opcoes;       // para CHOICE
    int num_opcoes;
    float min, max;      // para SCORE
    char *afirmacao;     // para NOUL
    int pagina_fonte;
} PerguntaTipada;

PerguntaTipada* gerar_perguntas(Chunk *chunk, int *num_perguntas);
c
// templates/question_choice.tpl
{
  "tipo": "choice",
  "enunciado": "Qual é a classificação de {{entidade}} no contexto de {{contexto}}?",
  "opcoes": {{opcoes}},
  "fonte": "pagina {{pagina}}"
}
5. decision_engine.c — Integração com Laya/JEV
O motor de decisão recebe um estado (texto do chunk) e uma pergunta tipada, e devolve resposta com probabilidade. Usaremos o Laya (open source, 421M parâmetros, GGUF).

c
// include/decision_engine.h
typedef struct {
    char *resposta;       // para CHOICE: opção escolhida
    float probabilidade;
    float confianca;
} Decisao;

Decisao* executar_decisao(const char *estado, PerguntaTipada *pergunta, const char *modelo_gguf);
c
// src/decision_engine.c (trecho)
#include "llama.h"  // llama.cpp para carregar GGUF

Decisao* executar_decisao(const char *estado, PerguntaTipada *pergunta, const char *modelo) {
    struct llama_model *modelo_laya = llama_load_model_from_file(modelo, ...);
    // Montar prompt: estado + pergunta tipada
    // Executar forward pass não autoregressivo
    // Extrair logits para as opções / score / noul
    // Calcular softmax e retornar Decisao
}
6. packager.c — Empacotamento no Formato .amanda
O artefato final é um container binário com:

Header: versão, hash, idioma.

Índice de chunks: offsets, páginas.

Embeddings: vetores quantizados.

Grafo de perguntas: perguntas tipadas + fontes.

Cache de decisões: perguntas frequentes com respostas validadas.

Metadados: título, autor, data.

c
// include/packager.h
typedef struct {
    char *titulo;
    char *autor;
    Chunk *chunks;
    int num_chunks;
    Embeddings *embeddings;
    PerguntaTipada *perguntas;
    int num_perguntas;
} AmandaPackage;

void empacotar_amanda(AmandaPackage *pkg, const char *saida);
AmandaPackage* carregar_amanda(const char *caminho);
7. server.c — Servidor HTTP Local (API OpenAI-compatible)
Usaremos libmicrohttpd ou ulfius (leve, C). Endpoints:

GET /v1/models

POST /v1/chat/completions (compatível com OpenAI)

POST /v1/decisions (para perguntas tipadas)

GET /v1/amanda/info

c
// src/server.c (trecho)
#include <microhttpd.h>

static enum MHD_Result responder_chat(void *cls, struct MHD_Connection *conn, ...) {
    // Parse JSON (jansson ou cJSON)
    // Carregar pacote .amanda
    // Recuperar chunks relevantes via busca vetorial + léxica
    // Executar decisões com Laya
    // Montar resposta JSON no formato OpenAI
}
8. cli.c — Interface de Linha de Comando
Comandos:

bash
amanda compile --input livro.pdf --output livro.amanda --model models/laya-421m.gguf
amanda serve --package livro.amanda --port 8080
amanda ask --package livro.amanda "Qual a definição de entropia?"
amanda inspect --package livro.amanda --stats
c
// src/cli.c (trecho)
#include <argp.h>

int main(int argc, char **argv) {
    // Parse de subcomandos: compile, serve, ask, inspect
    // Chamar funções correspondentes
}
🔄 Pipeline de Compilação (do PDF ao .amanda)
Ingestão: amanda compile --input doc.pdf

Extração: MuPDF extrai texto + metadados de layout.

Chunking: Divide em blocos de ~512 tokens com sobreposição de 64.

Embeddings: ONNX Runtime gera vetores para cada chunk.

Geração de Perguntas: Para cada chunk, o question_gen cria:

2–3 perguntas choice (com opções extraídas por NER + regras)

1–2 perguntas score (rubricas baseadas em adjetivos/quantificadores)

3–5 perguntas noul (afirmações extraídas como fatos)

Validação: Executa o Laya sobre 10% das perguntas com resposta conhecida para calibrar limiares.

Empacotamento: Gera o arquivo .amanda com todos os artefatos.

Teste: amanda ask com perguntas de exemplo.

🧪 Pipeline de Teste
Testes Unitários (C)
bash
cd build && ctest --output-on-failure
test_pdf_extractor: verifica se extrai texto de PDF de exemplo.

test_question_gen: verifica se gera perguntas tipadas válidas.

test_decision_engine: verifica se Laya retorna probabilidades calibradas.

Testes de Integração (Shell + Python)
bash
# scripts/test_pipeline.sh
amanda compile --input examples/exemplo.pdf --output /tmp/exemplo.amanda
amanda serve --package /tmp/exemplo.amanda --port 8081 &
SERVER_PID=$!
sleep 2
curl -X POST http://localhost:8081/v1/chat/completions \
  -H "Content-Type: application/json" \
  -d '{"model":"amanda","messages":[{"role":"user","content":"O que é X?"}]}'
kill $SERVER_PID
Métricas de Validação
Fidelidade: % de respostas que citam a página correta.

Calibração: desvio entre confiança declarada e acurácia real.

Latência: tempo médio de resposta (meta: < 500 ms).

Recusa: % de perguntas fora do escopo recusadas corretamente.

📦 Exemplo de Arquivo de Configuração (config.yaml)
yaml
projeto: "Compilador Amanda"
versao: "0.1.0"
pdf:
  caminho: "examples/exemplo.pdf"
  idioma: "pt-BR"
chunking:
  tamanho_max: 512
  sobreposicao: 64
embeddings:
  modelo: "models/embedding-model.gguf"
  dimensao: 384
question_gen:
  max_choice: 3
  max_score: 2
  max_noul: 5
decision_engine:
  modelo: "models/laya-421m.gguf"
  limiar_confianca: 0.7
  limiar_recusa: 0.3
servidor:
  porta: 8080
  host: "127.0.0.1"
🚀 Como Compilar e Executar
bash
# 1. Clonar e preparar dependências
git clone https://github.com/seu-usuario/amanda.git
cd amanda
mkdir build && cd build

# 2. Configurar com CMake
cmake .. -DMUPDF_DIR=/path/to/mupdf -DONNXRUNTIME_DIR=/path/to/onnxruntime

# 3. Compilar
make -j$(nproc)

# 4. Compilar um PDF
./amanda compile --input ../examples/exemplo.pdf --output ../exemplo.amanda

# 5. Servir localmente
./amanda serve --package ../exemplo.amanda --port 8080

# 6. Consumir via CLI ou OpenCode
./amanda ask --package ../exemplo.amanda "Qual a definição de entropia?"
🔮 Próximos Passos
Especificação do formato .amanda (documento docs/formato_amanda.md).

Implementação do question_gen com regras + LLM auxiliar (opcional).

Integração com Laya via llama.cpp (já existe suporte a GGUF).

Servidor OpenAI-compatible com streaming SSE.

CLI completa com subcomandos e flags.

Pipeline de CI com testes automatizados.

O Compilador Amanda não compete com o JEV: ele alimenta o JEV (ou o Laya) com estados e perguntas tipadas extraídas de documentos. É a camada de compilação de conhecimento que faltava.