# Amanda — Continuar (atualizado 2026-10-01, Fases 1–5 prontas)

Último commit após este lote: Fase 3 (`laya-http` + fallback) em
`https://github.com/dantas72/amanda`, branch `main`.

## Estado atual (tudo verde)
- Fases `[x]`: 1 (núcleo `amandac.exe`), 2 (CMake + CI),
  3 (`laya-http` com fallback + etapa `check_laya_llm`),
  4 (SSE + `/v1/embeddings`), 5 (`eval` + métricas).
- Testes: **36/36 unit** (`build/amanda_tests.exe`: +11 Fase 3),
  pipeline `scripts\test_pipeline.bat` OK (7 etapas; laya-llm = SKIP
  honesto com engine fora do ar).
- `version.bin`: **1.0.6** (`amandac 1.0.6`).
- Livros em `pdf/` compilados para `build/*.amanda` (gitignore, não commitar).
- Ollama: `nimble:latest` (9B Q8) em `:11434`, cadastrado no Laya
  (`localollama`/`ollamaserver`). Caminho vivo pendente de apontar o
  slot **chat** do Laya p/ nimble (hoje: `claude-sonnet-4-6` sem chave
  → `/chat` retorna erro → fallback local). Ver `docs/laya.md`.

## Falta (backlog, fora das fases)
- Recalibrar confiança do `decision_engine` (overconfiança ~0.99) e
  limiar de recusa (probes 0/3) — ver `docs/eval.md`.
- Ampliar extração de PDFs complexos + cobertura no `eval`.
- Caminho vivo laya-http end-to-end (com nimble respondendo no `/chat`).

## Como retomar
```bat
cd C:\Projetos\Projeto_IA\Amanda
git pull
build.bat                              :: incrementa version.bin e gera amandac.exe
gcc -O2 -std=c11 -Iinclude tests/test_all.c src/amanda.c src/utils.c src/pdf_extractor.c src/chunker.c src/embedder.c src/question_gen.c src/decision_engine.c src/laya_backend.c src/packager.c src/eval.c -o build/amanda_tests.exe -lws2_32 && build/amanda_tests.exe
scripts\test_pipeline.bat              :: pipeline completa (laya = SKIP sem engine)
amandac.exe ask --package build\cvm_valores_mobiliarios.amanda "O que e ...?" --backend laya-http
```

## Decisões registradas (não reabrir sem motivo)
1. **Laya v1.10.1 NÃO é GGUF** — é app Tauri + engine FastAPI (`:8420`), LLM via
   LiteLLM. `Projeto.md` estava errado; correção em `docs/laya.md`. Nunca baixar
   "laya-421m.gguf" de terceiros.
2. **Regra Skill.md nº 5**: sempre confirmar escopo antes de alterar código.
3. **Armadilhas de shell já mapeadas**:
   - Servidor de teste: usar `start` via `.bat` (cmd); `Start-Job` morre entre sessões.
   - JSON inline em PowerShell/cmd quebra aspas — usar fixtures em
     `examples/smoke_*.json` + `curl -d @arquivo`.
   - `ConvertFrom-Json` do PS 5.1 não tem `-Depth` (padrão 2) — validar JSON
     grande por regex/contagem, não por parse profundo.
   - `buf_append` com tamanho hardcoded: conferir sem o NUL (bug real da Fase 4);
     preferir `buf_append_cstr`.
   - **`.bat` novo: escrever com CRLF e sem PowerShell inline com regex/aspas**
     (bug real da Fase 3: cmd silencia o script; usar `curl` + `findstr`).
   - **Flags com valor no `ask` posicional**: pular o valor no join da pergunta
     (bug real da Fase 3: `--backend laya-http` contaminava a pergunta e mudava
     a confiança 0.99 → 0.90).
4. Commits como `Spoiledpay` (config local). Push usa Credential Manager do Windows.
5. `build/`, `*.exe`, `*.amanda`, `models/*.gguf` estão no `.gitignore` (não commitar).
6. **Fase 5 não recalibrou o motor**: `eval` mede e reporta; mudar
   centro/inclinação/limiar do `decision_engine` ficou de backlog.
7. **Fase 3 nunca depende do Laya/Ollama**: testes e pipeline passam sem eles
   (fallback local + SKIP honesto). Ollama CLI pode não estar no PATH;
   usar a API `:11434` para verificar.
