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
