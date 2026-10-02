# Pipeline Amanda (Fases 1–12.2)

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
5. **Empacotamento** — `.amanda` binário com CRC32 (formato v2 desde a
   Fase 7.4: persiste `num_paginas`, blocos e stats de extração;
   leitor aceita v1 com cobertura zerada).
6. **Serviço** — `amandac serve` expõe API compatível OpenAI
   (`/v1/chat/completions`, `/v1/decisions`, `/v1/models`,
   `/v1/amanda/info`, `/v1/embeddings` + SSE), com recuperação híbrida
   (Fase 12.2: 0.6 cosseno + 0.4 BM25 com IDF por pacote, k1=1.2/b=0.75,
   max-norm; antes: overlap simples), recusa calibrada
   (`--conf-center/--conf-slope/--limiar-recusa`, Fase 7.2),
   citação multi top-2 (`[p.X]` + `[p.Y]`) e backend
   sempre local.
7. **Validação** — testes unitários em C (`tests/test_all.c`, 125 checks) +
   pipeline de integração (`scripts/test_pipeline.bat/.sh`, 8 etapas
   incluindo `eval`, `calibrate`, compile via `--config` e smoke do
   `POST /v1/eval`). O `eval`
   reporta cobertura da extração (páginas, blocos, chunks, chars,
   streams, falhas) em texto e `--json` (objeto `cobertura`).
   Regressão de respostas: `scripts/check_gold.bat` (40 perguntas
   curadas, semântica recall@2 — pagina OU 2ª citação — gold v2
   Fase 12.2 em `examples/gold_*.json`).
8. **Laya (etapas 7–8/8 da pipeline, opcional)** — `scripts/check_laya.bat`
   (engine no ar?) e `scripts/check_laya_llm.bat` (backend `laya-http`
   LIVE ou SKIP honesto). Detalhes em `docs/laya.md`.
