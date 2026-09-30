# Pipeline Amanda (Fase 1)

1. **Ingestão** — `amandac compile --input doc.(pdf|txt|csv|json)`:
   PDF é analisado por parser próprio (streams, FlateDecode/zlib,
   operadores `Tj/TJ`), sem dependências externas. TXT/CSV/JSON têm
   extratores nativos.
2. **Chunking** — palavras com sobreposição (padrão 180/30), preservando
   página inicial/final e hash FNV-1a por chunk.
3. **Embeddings** — TF com bigramas + hash trick em 384 dimensões,
   ponderação log-TF e normalização L2. Determinístico e local.
4. **Perguntas tipadas** — regras sobre sentenças: `choice` (entidade +
   distratores globais embaralhados), `score` (rubrica 0–10), `noul`
   (afirmações extraídas como fatos).
5. **Empacotamento** — `.amanda` binário com CRC32.
6. **Serviço** — `amandac serve` expõe API compatível OpenAI
   (`/v1/chat/completions`, `/v1/decisions`, `/v1/models`,
   `/v1/amanda/info`), com recuperação híbrida
   (0.6 cosseno + 0.4 sobreposição léxica) e recusa calibrada.
7. **Validação** — testes unitários em C (`tests/test_all.c`) +
   pipeline de integração (`scripts/test_pipeline.bat/.sh`).
8. **Laya (etapa 5/5 da pipeline, opcional)** — `scripts/check_laya.bat`
   verifica o engine do Laya em `http://127.0.0.1:8420`. Se o Laya
   estiver instalado e rodando: `OK`. Caso contrário: `SKIP` (não
   falha a pipeline). Detalhes em `docs/laya.md`.
