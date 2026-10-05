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
Windows and Linux (macOS temporarily out; see Phase 7.5), with
`amandac` + `amanda_tests` artifacts per CI release (Phase 7.6).
The Laya step is optional (SKIP without the engine).

## Usage
```sh
amandac compile --input examples/exemplo.txt --output exemplo.amanda
amandac compile --config examples/config.yaml --output exemplo.amanda
amandac inspect --package exemplo.amanda --stats
amandac inspect --package exemplo.amanda --json   # machine report (Phase 7.6)
amandac ask --package exemplo.amanda "O que é entropia?"
amandac eval --package exemplo.amanda --sample 0.1
amandac serve --package exemplo.amanda --port 8080
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
`docs/eval.md`.

## Laya backend (Phase 3, optional)

```sh
amandac ask --package exemplo.amanda "O que é entropia?" --backend laya-http --laya-url http://127.0.0.1:8420
```

Sends (context + question) to the Laya engine's `POST /chat` (LLM via
Ollama, e.g. `nimble`) and combines it with local grounding
(page/citation/confidence). Any failure automatically falls back to
the local engine. Details in `docs/laya.md`.

## Recalibration (Phase 6)

```sh
amandac calibrate --package exemplo.amanda --sample 0.5
amandac ask --package exemplo.amanda "Pergunta" --conf-center 0.100 --conf-slope 22.0 --limiar-recusa 0.90
amandac eval --package exemplo.amanda --conf-center 0.100 --conf-slope 22.0 --limiar-recusa 0.90
```

`calibrate` suggests the center/slope/threshold that maximize balanced
accuracy (accept in-scope, refuse probes). Zeros = historic default.
Details and per-book table in `docs/eval.md`.

## Calibrated serve (Phase 7.2)

```sh
amandac serve --package exemplo.amanda --port 8080 --conf-center 0.100 --conf-slope 22.0 --limiar-recusa 0.90
```

Applies the `calibrate`/`eval` calibration to the server (banner shows
the active parameters). Serve backend stays always local: with a
single-thread server, one LLM inference per request would stall the
service (revisit in Phase 7.5, with threads). See `docs/api.md`.

## Phase status

Phases 1–6 + 7.1 done (`FASES.md`, in Portuguese): core, CMake+CI,
Laya-HTTP, SSE+embeddings, `eval`, `calibrate`, docs/hygiene.
In progress: Phase 7 (7.2 calibrated serve, 7.3 config+templates,
7.4 PDF+, 7.5 robust server, 7.6 release).

## Tests
```bat
gcc -O2 -Iinclude tests\test_all.c src\amanda.c src\utils.c src\pdf_extractor.c src\chunker.c src\embedder.c src\question_gen.c src\decision_engine.c src\laya_backend.c src\packager.c src\eval.c src\calibra.c src\config.c -o build\amanda_tests.exe -lws2_32 && build\amanda_tests.exe
scripts\test_pipeline.bat
```

## Structure
- `include/` public headers
- `src/` C11 implementation with no external dependencies (only `ws2_32` on Windows)
- `docs/` format, pipeline, and API specs
- `tests/` unit tests in C
- `scripts/` integration pipelines
- `version.bin` version read by the binary and incremented on every `build.bat`

## Version
`version.bin` is the source of truth, incremented on every `build.bat`.
Last local build:
Build: `1.0.39`
