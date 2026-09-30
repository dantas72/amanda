# Amanda — Continuar amanhã (2026-10-01)

Último commit: `88d975d` (Fase 4) em `https://github.com/dantas72/amanda`, branch `main`.

## Estado atual (tudo verde)
- Fases `[x]`: 1 (núcleo `amandac.exe`), 2 (CMake + CI win/linux/mac), 4 (SSE + `/v1/embeddings`).
- Testes: 17/17 unit (`build/amanda_tests.exe`), pipeline `scripts\test_pipeline.bat` OK.
- CI: `.github/workflows/ci.yml` roda no push (ver aba Actions no GitHub).

## Faltam
- [ ] **Fase 5** — calibração + métricas: validar em 10% das perguntas geradas,
      relatório de fidelidade (% cita página correta), calibração
      (confiança × acurácia), latência (<500ms) e recusa. Ideia: novo
      subcomando `amandac eval --package X --sample 0.1`.
- [ ] **Fase 3 (deferida)** — integração Laya via HTTP. Pré-requisito: Laya
      instalado com engine em `127.0.0.1:8420`; então `scripts\check_laya.bat`
      passa de SKIP → OK. Ver `docs/laya.md`.

## Como retomar
```bat
cd C:\Projetos\Projeto_IA\Amanda
git pull
build.bat                              :: incrementa version.bin e gera amandac.exe
gcc -O2 -std=c11 -Iinclude tests/test_all.c src/amanda.c src/utils.c src/pdf_extractor.c src/chunker.c src/embedder.c src/question_gen.c src/decision_engine.c src/packager.c -o build/amanda_tests.exe && build/amanda_tests.exe
scripts\test_pipeline.bat              :: pipeline completa (etapa Laya = SKIP sem engine)
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
4. Commits como `Spoiledpay` (config local). Push usa Credential Manager do Windows.
5. `build/`, `*.exe`, `*.amanda`, `models/*.gguf` estão no `.gitignore` (não commitar).
