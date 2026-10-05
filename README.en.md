# Amanda — Knowledge Compiler for Business

[![CI](https://github.com/dantas72/amanda/actions/workflows/ci.yml/badge.svg)](https://github.com/dantas72/amanda/actions)

![Amanda](amandaLogo.png)

Repository: `https://github.com/dantas72/amanda`

Read this in: [Português](README.md) · [Русский](README.ru.md) · [中文](README.zh.md)

© 2026 LabsObjects — created and implemented by Fernando Dantas, Brazil.
MIT License: `LICENSE` (PT-BR), `LICENSE.en` (US English),
`LICENSE.ru` (RU), `LICENSE.zh` (CN).

Turns your operation's documents (`PDF / TXT / CSV / JSON`) into
`.amanda` decision artifacts: every answer comes with its cited
source and page, calibrated confidence level, and automatic
out-of-scope refusal. Runs 100% locally with no cloud dependency,
and integrates with your systems via CLI or an OpenAI-compatible
API — standalone or drafting via LLM (see `docs/laya.md`). Ideal
for compliance, legal, finance, and customer support.

## Build (Windows)
```bat
build.bat
```
Generates `amandac.exe` and increments `version.bin` (current version under `## Version` below).

## Build (Linux/Mac)
```sh
cmake -S . -B build && cmake --build build
cmake --build build --target version_bump   # parity with build.bat (increments version.bin)
```

## CI
GitHub Actions (`.github/workflows/ci.yml`): build + unit tests
(`ctest`) + integration (`compile`/`inspect`/`ask`/serve smoke) on
Windows and Linux (macOS paused — instant crash in unit tests on M1
and Intel, see `FASES.md`; M2/M3/M4 have no free hosted labels),
with `amandac` + `amanda_tests` artifacts per CI release (Phase 7.6).
The Laya step is optional (SKIP without the engine).

## Usage
```sh
amandac compile --input examples/exemplo.txt --output exemplo.amanda
amandac compile --config examples/config.yaml --output exemplo.amanda
amandac inspect --package exemplo.amanda --stats
amandac inspect --package exemplo.amanda --json   # machine report (Phase 7.6, with v3 calibration)
amandac ask --package exemplo.amanda "O que é entropia?"
amandac ask --package exemplo.amanda "O que é entropia?" --json
amandac eval --package exemplo.amanda --sample 0.1
amandac calibrate --package exemplo.amanda --sample 0.5 --apply
amandac serve --package exemplo.amanda --port 8080
amandac serve --package cvm=livro1.amanda --package ibri=livro2.amanda --port 8080  # multi (Phase 13)
amandac mcp --package exemplo.amanda   # MCP server over stdio (OpenCode/agents, see docs/mcp.md)
amandac version
```

## Metrics (Phase 5)

```sh
amandac eval --package exemplo.amanda --sample 0.1
amandac eval --package exemplo.amanda --sample 1.0 --json
```

Validates a sample of the typed questions and reports fidelity (% correct
page), calibration (confidence × accuracy, gap + ECE), latency (goal
≤500ms), and refusal (in-scope + out-of-scope probes). Details in
`docs/eval.md`. Natural-answer regression:
`scripts/check_gold.bat` (80 curated questions, recall@2).

## Laya backend (Phase 3, optional)

```sh
amandac ask --package exemplo.amanda "O que é entropia?" --backend laya-http --laya-url http://127.0.0.1:8420
```

Sends (context + question) to the Laya engine's `POST /chat` (LLM via
Ollama, e.g. `nimble`) and combines it with local grounding
(page/citation/confidence). Any failure automatically falls back to
the local engine. Details in `docs/laya.md`.

## Real backends: JEV/TypeSafe + DeepSeek

```sh
amandac ask --package exemplo.amanda "O que é entropia?" --json --backend typesafe-http --typesafe-url http://127.0.0.1:11434 --typesafe-model nimble
TYPESAFE_API_KEY=... amandac ask --package exemplo.amanda "Pergunta" --backend typesafe-http --typesafe-model jev-latest
DEEPSEEK_API_KEY=... amandac ask --package exemplo.amanda "Pergunta" --backend deepseek-http
```

`typesafe-http` (alias `jev`): real noul judgment — TypeSafe cloud
`api.typesafe.ai` or local Ollama nimble — calibrates confidence
over local grounding; `deepseek-http`: cloud redraft via DeepSeek.
Keys via flag/env/file/`amandac.conf` (never logged, never in
`amanda.json` — copy `examples/amandac.conf`);
endpoints and models in `--config-json` (`examples/amanda.json`).
Honest local fallback (the `"backend"` field). Real pipeline:
`scripts/check_typesafe.bat` + `scripts/check_deepseek.bat`
(SKIP without Ollama/keys). Details in `docs/typesafe.md`,
`docs/deepseek.md` and `docs/jev.md`.

## Recalibration (Phase 6, stored in v3 since 12.4)

```sh
amandac calibrate --package exemplo.amanda --sample 0.5
amandac calibrate --package exemplo.amanda --sample 0.5 --validacao examples/gold_cvm.json --apply
amandac ask --package exemplo.amanda "Pergunta" --conf-center 0.200 --conf-slope 16.0 --limiar-recusa 0.85
amandac eval --package exemplo.amanda --conf-center 0.200 --conf-slope 16.0 --limiar-recusa 0.85
```

`calibrate` grid-searches (center × slope × threshold, 0.01 threshold
step) for the point that maximizes balanced accuracy (accept
in-scope, refuse probes); with `--validacao`, the objective becomes
`(bal + recall@2)/2` over labeled natural questions. `--apply`
writes into the package (`.amanda` v3 format); `ask`/`eval`/`serve`
pick it up automatically (CLI flag wins, `--ignore-calib` forces the
default). Zeros = historic default. Details and per-book table in
`docs/eval.md`.

## Serve (Phases 7.2/7.5/11/13 + LLM pool)

```sh
amandac serve --package exemplo.amanda --port 8080 --conf-center 0.200 --conf-slope 16.0 --limiar-recusa 0.85
amandac serve --config examples/config.yaml
AMANDA_API_KEY=segredo amandac serve --package exemplo.amanda --port 8080 --workers 8
```

Fixed worker pool with queue (`--workers`, `--max-conns` = total cap
with `503`), key via flag/env/file (never logged), `serve --config`
(`servidor:` section), access log on `stderr`, no built-in TLS
(production behind a reverse-proxy). Multi-package by `"model"`
(see `docs/api.md`). With `--backend laya-http`, chat/decisions try
the Laya engine with automatic local fallback (the `"backend"` field):
LLM pool with priority (`decisions` HIGH > `chat` NORMAL,
`--laya-queue`/`--laya-queue-ms`; full queue or expired wait =
local, see `docs/laya.md`).

## MCP server (ask/decisions for OpenCode and agents)

```sh
amandac mcp --package exemplo.amanda
```

MCP server over stdio (JSON-RPC, one message per line, log on
`stderr`): tools `ask`, `decisions`, `inspect`, `version`;
multi-package by `model` as in `serve`. Template in
`examples/mcp_config.json`, details in `docs/mcp.md`.

## Docker

```sh
docker build -t amandac .
docker run --rm -p 8080:8080 -v /seus/amanda:/data amandac
```

Multi-stage image (unit tests run during the build); entrypoint
serves every `/data/*.amanda`, or runs any command (`mcp`,
`inspect`, ...). Variables (`PORT`, `AMANDA_API_KEY`, `WORKERS`,
calibration...) in `docs/docker.md`.

## Phase status

Phases 1–13 done (`FASES.md`, in Portuguese) + **MCP server**
(`amandac mcp`: ask/decisions/inspect/version over stdio,
`docs/mcp.md`) + **Docker** (multi-stage image with tests in the
build, `docs/docker.md`) + **LLM pool** with priority (`decisions` >
`chat`, `docs/laya.md`): pure-C Windows core, CMake+CI, Laya over
HTTP, SSE+embeddings, `eval`, `calibrate` (+`--apply` v3 and
`--validacao`), PDF+ extraction, robust enterprise serve (pool,
multi-package by `model`), release artifacts, BM25 retrieval +
PT stopwords + multi-citation, rerank (stemming/junk/saturating
norm/phrase), inverted index + query cache. Reference (`amandac
1.0.39`, v3 packages): CVM 86.64%, IBRI 87.41%, INVEST 88.27%,
Direito 84.00%; latency 0.3–5ms; probes 3/3; gold 75/80 audited
(see `docs/eval.md`).

## Tests
```bat
gcc -O2 -Wall -Wextra -std=c11 -Iinclude tests\test_all.c src\amanda.c src\utils.c src\pdf_extractor.c src\chunker.c src\embedder.c src\question_gen.c src\decision_engine.c src\laya_backend.c src\typesafe_backend.c src\deepseek_backend.c src\packager.c src\server.c src\eval.c src\calibra.c src\config.c src\mcp.c -o build\amanda_tests.exe -lws2_32 && build\amanda_tests.exe
scripts\test_pipeline.bat
scripts\check_gold.bat
```
Reference: **267 checks** + pipeline (12 steps, incl. MCP smoke
and real backends with honest SKIP)
+ gold (80 questions, recall@2). CI: Windows + Linux (`ctest` + MCP
smoke + Docker build); macOS paused (see `FASES.md`).

## Structure
- `include/` public headers
- `src/` C11 implementation with no external dependencies (only `ws2_32` on Windows)
- `docs/` format (`formato_amanda.md`), pipeline, API, eval, Laya, usage guide, business tutorial, MCP (`mcp.md`), Docker (`docker.md`)
- `templates/` typed-question wordings (+ `empresas/` packs: compliance, finance, legal, support)
- `examples/` sample, `config.yaml`, smoke fixtures and regression golds
- `tests/` unit tests in C
- `scripts/` integration pipelines
- `version.bin` version read by the binary and incremented on every `build.bat`

## Future roadmap (documented; not implemented)
- **GPU tests**: repeat live Laya (nimble + llama3.2:3B) on GTX 1660 Ti and 10GB+ GPUs (see `docs/laya.md`).
- **macOS CI + self-hosted M2-M4**: reactivate with the crash log
  (run 37075569182, Unit tests step, 0s on M1 and Intel) or a local
  test on a real Mac; kit ready in `docs/macos.md`.

## Version
`version.bin` is the source of truth, incremented on every `build.bat`.
Last local build:
Build: `1.0.50`
