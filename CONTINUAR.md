# Amanda — Continuar (atualizado 2026-09-30, Fase 5 pronta)

Último commit após este lote: Fase 5 (`amandac eval`) em `https://github.com/dantas72/amanda`, branch `main`.

## Estado atual (tudo verde)
- Fases `[x]`: 1 (núcleo `amandac.exe`), 2 (CMake + CI win/linux/mac),
  4 (SSE + `/v1/embeddings`), **5 (`eval` + métricas)**.
- Testes: **25/25 unit** (`build/amanda_tests.exe`, +8 do `eval`),
  pipeline `scripts\test_pipeline.bat` OK (agora com etapa `eval`).
- CI: `.github/workflows/ci.yml` com etapa `Eval (Fase 5)`; Laya check segue opcional.
  (Nesta máquina o engine Laya respondeu em `:8420` — OK, não SKIP.)
- `version.bin`: **1.0.2** (`amandac 1.0.2`).
- Livros em `pdf/` compilados para `build/*.amanda` (gitignore, não commitar):
  - `CVM-livro_top_valores_mobiliarios_br_5ed.pdf` → `build/cvm_valores_mobiliarios.amanda` (90 perguntas)
  - `Livro-IBRI-CVM.pdf` → `build/ibri_cvm.amanda` (21 perguntas)
  - `livro_top_direito.pdf` → `build/top_direito.amanda` (159 perguntas)
  - `top-analise-de-investimentos-2ed.pdf` → `build/top_analise_investimentos.amanda` (75 perguntas)
- `amandac eval --sample 0.1`: latência PASS em todos (0–5 ms); fidelidade
  50–100% conforme o livro; ver tabela e achados em `docs/eval.md`.

## Falta
- [ ] **Fase 3 (deferida)** — integração Laya via HTTP. Pré-requisito: Laya
      instalado com engine em `127.0.0.1:8420`; então `scripts\check_laya.bat`
      passa de SKIP → OK. Ver `docs/laya.md`.
- [ ] **Backlog pós-Fase 5** (ver `docs/eval.md`): recalibrar confiança do
      `decision_engine` (overconfiança ~0.99), recalibrar limiar de recusa
      (probes 0/3), ampliar extração de PDFs complexos + cobertura no `eval`.

## Como retomar
```bat
cd C:\Projetos\Projeto_IA\Amanda
git pull
build.bat                              :: incrementa version.bin e gera amandac.exe
gcc -O2 -std=c11 -Iinclude tests/test_all.c src/amanda.c src/utils.c src/pdf_extractor.c src/chunker.c src/embedder.c src/question_gen.c src/decision_engine.c src/packager.c src/eval.c -o build/amanda_tests.exe && build/amanda_tests.exe
scripts\test_pipeline.bat              :: pipeline completa (eval + Laya opcional)
amandac.exe eval --package build\cvm_valores_mobiliarios.amanda --sample 0.1
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
6. **Fase 5 não recalibrou o motor**: `eval` mede e reporta (gap/ECE, recusa,
   latência); mudar centro/inclinação/limiar do `decision_engine` ficou de
   backlog deliberado para não alterar comportamento sem curva dedicada.
