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

## Fase 11 — `serve --backend laya-http` (`amandac` 1.0.19+)

Com threads (Fase 7.5), o `serve` pode inferir via Laya sem travar:

```bat
amandac.exe serve --package livro.amanda --port 8080 --backend laya-http
```

- Grounding continua local (página/citação/confiança do índice);
  o Laya redige sobre a citação; resposta ganha `" (via Laya)"`.
- Qualquer falha (engine fora do ar, timeout `--laya-timeout-ms`,
  sem modelo ativo) cai para o motor local na hora.
- `--laya-max N` (default 2) limita inferências simultâneas para
  proteger o engine; sem slot, a resposta sai local. O campo
  `"backend"` (`local`/`laya-http`) diz o caminho usado.
- `POST /v1/embeddings` e `POST /v1/eval` seguem sempre locais.
- Requer o caminho vivo da Fase 3 (slot chat do Laya apontado p/
  modelo com provider, ex. nimble no Ollama); senão tudo cai em
  `local` com honestidade no campo `backend`.

## Teste vivo com engine real + nimble (2026-10-01, sessão documentada)

Ambiente: Ollama `:11434` com `nimble:latest` (9B Q8, 10GB) no ar;
engine Laya `:8420` saudável (sqlite/chromadb/n8n ok); GPU GeForce
MX250 2GB (1813 MiB livres).

Achados:

1. Slot `chat` estava em `claude-sonnet-4-6` sem chave Anthropic →
   `/chat` devolvia a string de erro padrão. Corrigido via
   `PUT /settings {"models":{"chat":"localollama/nimble:latest"}}`
   (deep merge; verificado no `GET /settings` seguinte).
2. `/chat` vivo **não respondeu em 280s nem em 480s** (curl 28).
   Causa: nimble com só 173MB em VRAM (~98% em CPU) + pipeline RAG
   do Laya + `llm_retries:3` sobre provider `default_timeout:120`.
   Ollama direto (`/api/generate`, prompt mínimo) também não
   respondeu em 240s. Gargalo é hardware, não integração.
3. **Fallback com engine real provado**: `serve --backend laya-http
   --laya-timeout-ms 20000` respondeu rápido com resposta local
   correta (`"backend":"local"`, conf 0.993, p. 1).
4. Caminho vivo segue coberto pelos testes com stub (Fase 11,
   117/117): mesma `laya_chat`, mesmo `/chat`, mesma extração de
   `content`.

### Testes futuros (caminho vivo real)

- [x] Modelo pequeno em CPU (2026-10-01): `ollama pull llama3.2:3B`
  (3.2B Q4, 2GB) + `scripts\check_laya_llm.bat` → `ask` e `serve`
  via Laya OK (`"backend":"laya-http"`, `(via Laya)`). Ver § teste
  vivo llama3.2:3B.
- [ ] GPU com 10GB+ VRAM (offload total do nimble) e repetir o
  teste vivo ponta a ponta; medir latência chat/decisions.
- [ ] Se o slot `chat` voltar a apontar p/ nuvem sem chave, o
  sintoma é a string de erro no `/chat` → fallback local honesto
  (ver § Ollama + nimble). Não é regressão do Amanda.
- [ ] Reativar `macos-latest` no CI com diagnóstico em Mac real
  (falha ARM pré-existente desde 7.2; ver guia § 9).

## Teste vivo com llama3.2:3B (2026-10-01, primeiro LLM vivo ponta a ponta)

- Ollama direto: `POST /api/generate` (`llama3.2:3B`, prompt mínimo,
  `stream:false`) respondeu em ~23s (17s load) — CPU OK.
- Engine do Laya estava fora do ar (sem listener em `:8420`); subido
  via `laya-app.exe`. No boot, os slots foram para
  `ollama/llama3.2:3B` (antes `localollama/nimble:latest`).
- `ask --backend laya-http` → `"backend":"laya-http"`, `(via Laya)`,
  conf 0.9935, p. 1. `serve --backend laya-http` → idem, conf 0.993.
  O nimble nunca respondeu nesta máquina (MX250 2GB).
- Bug achado no gate do `scripts\check_laya_llm.bat`: `findstr /C:`
  trata o padrão como literal, então o regex nunca casava e o script
  sempre dava SKIP com o engine no ar. Trocado por dois literais
  (`"providers":[]` vazio = SKIP; `{"id":` ausente = SKIP),
  espelhando o `grep` do `.sh` (que estava correto). Revalidado:
  `LIVE + OK via Laya` no `ask`; o `serve` caiu em fallback local
  honesto uma vez com o engine sob contenção e passou via Laya na
  repetição com o engine ocioso.

## Ollama + nimble (base, verificado em 2026-10-01)

- Ollama em `http://127.0.0.1:11434` com `nimble:latest` (9B Q8,
  Qwen, 262k ctx). Laya lista `localollama`/`ollamaserver` como
  custom providers (`base_url http://localhost:11434`).
- `setup_complete:false` foi observado sem impedir o uso.
