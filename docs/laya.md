# Integração Amanda × Laya (verificado em 2026-09-30)

Fonte: `https://github.com/aayushch/laya` — release `v1.10.1`
(`3bd37aa`), README do tag `v1.10.1`.

## Correção importante ao `Projeto.md`

O `Projeto.md` assumia que o Laya "já tem implementação em GGUF"
(`models/laya-421m.gguf`, 421M parâmetros, via llama.cpp).
**Isso está incorreto.** O Laya real é um *AI command center*
local-first, não um arquivo de pesos:

| Camada     | Tecnologia (Laya v1.10.1)                              |
|------------|--------------------------------------------------------|
| Shell      | Tauri v2 (Rust) + Svelte 5                             |
| Engine     | Python FastAPI em `http://127.0.0.1:8420` (27 routers) |
| LLM        | LiteLLM: Ollama, LM Studio, Claude, GPT, Gemini, qualquer endpoint OpenAI-compatible |
| Vetores    | ChromaDB embarcado (`~/.laya/data/chroma/`)            |
| Léxico     | SQLite FTS5 (`cards_fts`/`events_fts`) + BM25          |
| Fusão      | Reciprocal Rank Fusion (busca híbrida)                 |
| Embeddings | ONNX (built-in) ou sentence-transformers (opcional)    |

Não existe `laya-421m.gguf` no repositório do Laya. Por isso a
Fase 1 do Amanda **não depende do Laya** e usa recuperação híbrida
própria (0.6 cosseno + 0.4 sobreposição léxica) — o mesmo desenho
do Laya (vetorial + BM25 com fusão), o que torna a integração
futura natural.

## Contrato de integração (Fase 3, sem mocks)

1. **Amanda como backend de contexto do Laya.** O `amandac serve`
   já expõe `POST /v1/chat/completions` em formato OpenAI. Como o
   Laya fala com "qualquer endpoint OpenAI-compatible via LiteLLM",
   basta apontar o Laya (Settings → modelo customizado/OpenAI-compatible)
   para `http://127.0.0.1:<porta>` do Amanda servindo um `.amanda`.
   Nenhum código novo é necessário do lado Amanda para isso.

2. **Laya como inferência LLM sobre estados Amanda (Fase 3).**
   Hoje o `decision_engine` responde por recuperação calibrada local.
   Na Fase 3, um backend opcional `laya-http` enviará
   (estado do chunk + pergunta tipada) ao engine do Laya
   (`:8420`, chat/completions via LiteLLM com Ollama/LM Studio local)
   e converterá o retorno em `Decisao` com probabilidade calibrada.
   Requer: Laya instalado e rodando; fallback permanece o motor local.

3. **Prompts customizados.** O Laya permite sobrescrever prompts de
   pipeline via `~/.laya/prompts/*.md` + `POST /prompts/reload`.
   Os templates em `templates/` do Amanda seguem o mesmo espírito
   (perguntas tipadas versionadas em arquivo).

## O que NÃO fazer

- Não baixar "laya-421m.gguf" de terceiros: não é artefato oficial.
- Não acoplar o build Fase 1 ao Laya: `build.bat` continua sem
  dependências externas. A integração é via HTTP em runtime.

## Fase 3 — implementada (`amandac 1.0.5`)

Backend opcional `laya-http` com fallback automático para o motor local:

- `src/laya_backend.c`: cliente HTTP mínimo (sockets, sem deps) com
  timeout. `laya_chat(url, message, timeout, &content)` faz
  `POST /chat {"message": ...}` e extrai `message.content`. Retorna
  `LAYA_UNAVAILABLE` (e o chamador cai para o local) em: engine fora
  do ar, timeout, HTTP não-2xx, conteúdo vazio ou a string de erro
  padrão do Laya (engine sem modelo ativo).
- `executar_decisao_hibrida()` (`src/decision_engine.c`): roda primeiro
  a recuperação local (grounding: página/citação/confiança) e, se
  `backend == laya-http`, envia (citação + pergunta) ao Laya. Se o Laya
  responder `NAO CONSTA` ou falhar, mantém a resposta local.
  `usou_laya_out` informa o caminho usado.
- Flags: `amandac ask --backend local|laya-http --laya-url URL`
  (padrão `http://127.0.0.1:8420`, timeout 120s) e as mesmas no
  `amandac eval` (relatório traz `via_laya`/`via_local`).
- Pipeline: `scripts/check_laya_llm.bat/.sh` — verifica engine,
  providers com modelos e `/chat` real; se live, roda
  `ask --backend laya-http` de verdade; senão SKIP honesto com motivo.
  Nunca falha a pipeline.

## Ollama + nimble (verificado em 2026-10-01)

- Ollama em `http://127.0.0.1:11434` com `nimble:latest` (9B Q8,
  Qwen, 262k ctx). Laya lista `localollama`/`ollamaserver` como
  custom providers (`base_url http://localhost:11434`).
- Atenção: o slot **chat** do Laya pode apontar para um modelo de
  nuvem sem chave (ex. `claude-sonnet-4-6`) — nesse caso o `/chat`
  retorna a string de erro e o backend cai para local (SKIP honesto).
  Para o caminho vivo, aponte o slot chat para o nimble no Settings
  do Laya. `setup_complete:false` também foi observado sem impedir
  o uso.
