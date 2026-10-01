# Amanda — Continuar (atualizado 2026-10-01, Fases 1–6 prontas)

Último commit após este lote: Fase 6 (recalibração) em
`https://github.com/dantas72/amanda`, branch `main`.

## Estado atual (tudo verde)
- Fases `[x]`: 1 (núcleo), 2 (CMake + CI), 3 (`laya-http` + fallback),
  4 (SSE + `/v1/embeddings`), 5 (`eval`), **6 (`calibrate` + sigmoide
  parametrizável)**.
- Testes: **46/46 unit** (`build/amanda_tests.exe`: +10 Fase 6),
  pipeline `scripts\test_pipeline.bat` OK (8 etapas).
- `version.bin`: **1.0.7** (`amandac 1.0.7`).
- Recalibração nos 4 livros: bal 0.500 → **1.000** (TPR 1.00, TNR
  0.00 → 1.00); CVM com params aplicados: recusa probe 0/3 → 3/3.
  Ver tabela em `docs/eval.md`.
- Ollama `nimble:latest` cadastrado no Laya; caminho vivo requer slot
  chat apontado p/ nimble (fora do repo).

## Falta (backlog)
- Ampliar extração de PDFs complexos + cobertura no `eval`.
- Caminho vivo laya-http end-to-end (nimble no `/chat`).
- Flags de calibração no `serve` (hoje só `ask`/`eval` aplicam).
- Dívidas docs: `Projeto.md` (MuPDF/ONNX/GGUF), `Jimi.md` (Go),
  `config.yaml` sem loader, `templates/*.tpl` decorativos,
  `version.bin` só incrementa no Windows.
- Servidor: single-thread, sem auth/CORS.

## Como retomar
```bat
cd C:\Projetos\Projeto_IA\Amanda
git pull
build.bat                              :: incrementa version.bin e gera amandac.exe
gcc -O2 -std=c11 -Iinclude tests/test_all.c src/amanda.c src/utils.c src/pdf_extractor.c src/chunker.c src/embedder.c src/question_gen.c src/decision_engine.c src/laya_backend.c src/packager.c src/eval.c src/calibra.c -o build/amanda_tests.exe -lws2_32 && build/amanda_tests.exe
scripts\test_pipeline.bat              :: pipeline completa (8 etapas)
amandac.exe calibrate --package build\cvm_valores_mobiliarios.amanda --sample 0.5
```

## Decisões registradas (não reabrir sem motivo)
1. **Laya v1.10.1 NÃO é GGUF** — Tauri + FastAPI (`:8420`), LLM via LiteLLM.
   Nunca baixar "laya-421m.gguf" de terceiros. Ver `docs/laya.md`.
2. **Regra Skill.md nº 5**: sempre confirmar escopo antes de alterar código.
3. **Armadilhas de shell já mapeadas**:
   - Servidor de teste: usar `start` via `.bat` (cmd).
   - JSON inline em PowerShell/cmd quebra aspas — fixtures + `curl -d @arquivo`.
   - `ConvertFrom-Json` PS 5.1 sem `-Depth` — validar por regex/contagem.
   - `buf_append` com tamanho hardcoded — preferir `buf_append_cstr`.
   - `.bat` novo: CRLF e sem PowerShell inline com regex/aspas (usar `curl`+`findstr`).
   - Flags com valor no `ask` posicional: pular o valor no join (bug Fase 3).
   - `echo ... -> ...` em `.bat` cria arquivos lixo (`>` = redirect); usar `=`.
   - Comparar floats de grade com epsilon (bug Fase 6: borda 0.90 reprovou CHECK).
4. Commits como `Spoiledpay` (config local). Push via Credential Manager.
5. `build/`, `*.exe`, `*.amanda`, `models/*.gguf` no `.gitignore` (não commitar).
6. **Zeros = padrão histórico** no sigmoide (0.12/12.0/0.30); `limiar_confianca`
   segue setado mas não lido (compat); recusa usa `limiar_recusa`.
7. **Fase 3/6 nunca dependem do Laya/Ollama**: testes e pipeline passam sem eles.
