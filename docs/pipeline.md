# Pipeline Amanda (Fases 1–13 + MCP/Docker + Pool LLM + Backends reais)

1. **Ingestão** — `amandac compile --input doc.(pdf|txt|csv|json)`
   ou `--config projeto.yaml` (subconjunto YAML: `pdf.caminho/idioma`,
   `chunking`, `question_gen`, `decision_engine`, `templates.dir`,
   `titulo/autor`; precedência: flag CLI > config > padrão).
   PDF é analisado por parser próprio (Fase 7.4): operadores `Tj/TJ/'/"`
   com quebras `Td/TD/Tm/T*`, filtros `FlateDecode/ASCIIHexDecode/
   ASCII85Decode/RunLengthDecode` em cadeia, dicionário próprio por stream,
   concatenação TJ com kerning, WinAnsi→UTF-8. Streams de imagem (DCT/JPX)
   são pulados sem contar falha; stream quebrada não aborta a extração.
   `compile` imprime `extracao: streams=N texto=M falhas=K fallback=sim|nao`.
   TXT/CSV/JSON têm extratores nativos.
2. **Chunking** — palavras com sobreposição (padrão 180/30), preservando
   página inicial/final e hash FNV-1a por chunk.
3. **Embeddings** — TF com bigramas + hash trick em 384 dimensões,
   ponderação log-TF e normalização L2. Determinístico e local.
   Tokens já normalizados (Fase 12.2: dobra de acentos + stopwords PT).
4. **Perguntas tipadas** — regras sobre sentenças: `choice` (entidade +
   distratores globais embaralhados), `score` (rubrica 0–10), `noul`
   (afirmações extraídas como fatos). Enunciados renderizados de
   `templates/question_{choice,score,noul}.tpl` (`{{trecho}}`,
   `{{afirmacao}}`, `{{min}}`, `{{max}}`, `{{pagina}}`); diretório
   ausente/ilegível = fallback embutido (byte-idêntico). Personalizar
   o wording muda os scores — rode `calibrate` depois.
5. **Empacotamento** — `.amanda` binário com CRC32 (formato **v3**
   desde a Fase 12.4: persiste `num_paginas`, blocos, stats de extração
   e calibração aplicada; leitor aceita v1/v2/v3).
6. **Serviço** — `amandac serve` expõe API compatível OpenAI
   (`/v1/chat/completions`, `/v1/decisions`, `/v1/models`,
   `/v1/amanda/info`, `/v1/embeddings`, `/v1/eval` + SSE), com recuperação híbrida
   (Fase 12.2: 0.6 cosseno + 0.4 BM25 com IDF por pacote, k1=1.2/b=0.75;
   Fase 12.3: norma saturante raw/(raw+8), stemming PT, filtro de chunks
   não-linguísticos, phrase-boost +0.2), recusa calibrada
   (`--conf-center/--conf-slope/--limiar-recusa`, Fase 7.2,
   Fase 12.4: pacote v3 vence o padrão, flag CLI vence o pacote,
   `--ignore-calib` força o histórico),
   citação multi top-2 (`[p.X]` + `[p.Y]`), pool fixo de workers +
   multi-pacote por `model` (Fase 13) e backends de inferência
   plugáveis: local, `laya-http` (com fila de prioridade decisions >
   chat), `typesafe-http`/`jev` (SystemOne nuvem ou nimble local) e
   `deepseek-http` (redação; ver `docs/laya.md`, `docs/typesafe.md`,
   `docs/deepseek.md`). Campo `"backend"`/`"via"` informa o caminho.
   Fase 12.4: índice BM25 pré-tokenizado 1x por pacote (compartilhado,
   somente leitura) + cache de embeddings de query (FIFO 32,
   thread-safe) — Direito 447ms → ~6ms.
7. **Validação** — testes unitários em C (`tests/test_all.c`, 267 checks) +
   pipeline de integração (`scripts/test_pipeline.bat/.sh`, 12 etapas
   incluindo `eval`, `calibrate`, compile via `--config`, smoke do
   `POST /v1/eval`, MCP, Docker e backends reais com SKIP honesto). O `eval`
   reporta cobertura da extração (páginas, blocos, chunks, chars,
   streams, falhas) em texto e `--json` (objeto `cobertura`).
   `calibrate --apply` grava o sugerido no pacote (v3); `--output`
   escreve cópia; `--validacao <gold.json>` ancora em perguntas
   naturais (Fase 12.4b). Regressão de respostas: `scripts/check_gold.bat/.sh`
   (80 perguntas curadas, semântica recall@2 — pagina OU 2ª citação —
   gold v3 em `examples/gold_*.json`; baseline 75/80 com 5 FAIL auditados).
8. **Laya / JEV / DeepSeek (etapas opcionais da pipeline)** —
   `scripts/check_laya.bat` (engine no ar?),
   `scripts/check_laya_llm.bat` (backend `laya-http` LIVE ou SKIP honesto),
   `scripts/check_typesafe.bat/.sh` (nimble local + nuvem `jev-latest`,
   SKIP por etapa), `scripts/check_deepseek.bat/.sh` (SKIP sem chave),
   `scripts/sweep_typesafe.bat/.sh` (curva limiar × nuvem, nunca falha).
   Detalhes em `docs/laya.md`, `docs/typesafe.md`, `docs/guia_jev.md`.
