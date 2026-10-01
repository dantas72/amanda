# Amanda — Compilador de Conhecimento em C

[![CI](https://github.com/dantas72/amanda/actions/workflows/ci.yml/badge.svg)](https://github.com/dantas72/amanda/actions)

![Amanda](amandaLogo.png)

Repositório: `https://github.com/dantas72/amanda`

Transforma `PDF / TXT / CSV / JSON` em artefato `.amanda` servido
localmente com API compatível OpenAI — pronto para ser consumido
por CLIs, OpenCode ou pelo Laya (ver `docs/laya.md`).

## Build (Windows)
```bat
build.bat
```
Gera `amandac.exe` e incrementa `version.bin` (começa em `1.0.1`).

## Build (Linux/Mac)
```sh
cmake -S . -B build && cmake --build build
```

## CI
GitHub Actions (`.github/workflows/ci.yml`): build + testes unitários
(`ctest`) + integração (`compile`/`inspect`/`ask`/serve smoke) em
Windows, Linux e Mac. A etapa Laya é opcional (SKIP sem o engine).

## Uso
```sh
amandac compile --input examples/exemplo.txt --output exemplo.amanda
amandac inspect --package exemplo.amanda --stats
amandac ask --package exemplo.amanda "O que é entropia?"
amandac eval --package exemplo.amanda --sample 0.1
amandac serve --package exemplo.amanda --port 8080
amandac version
```

## Métricas (Fase 5)

```sh
amandac eval --package exemplo.amanda --sample 0.1
amandac eval --package exemplo.amanda --sample 1.0 --json
```

Valida uma amostra das perguntas tipadas e relata fidelidade (% página
correta), calibração (confiança × acurácia, gap + ECE), latência (meta
≤500ms) e recusa (in-scope + probes fora-escopo). Detalhes em
`docs/eval.md`.

## Backend Laya (Fase 3, opcional)

```sh
amandac ask --package exemplo.amanda "O que é entropia?" --backend laya-http --laya-url http://127.0.0.1:8420
```

Envia (contexto + pergunta) ao `POST /chat` do engine Laya (LLM via
Ollama, ex. `nimble`) e combina com o grounding local
(página/citação/confiança). Qualquer falha cai para o motor local
automaticamente. Detalhes em `docs/laya.md`.

## Recalibração (Fase 6)

```sh
amandac calibrate --package exemplo.amanda --sample 0.5
amandac ask --package exemplo.amanda "Pergunta" --conf-center 0.100 --conf-slope 22.0 --limiar-recusa 0.90
amandac eval --package exemplo.amanda --conf-center 0.100 --conf-slope 22.0 --limiar-recusa 0.90
```

O `calibrate` sugere centro/inclinação/limiar que maximizam a acurácia
balanceada (aceitar in-scope, recusar probes). Zeros = padrão histórico.
Detalhes e tabela por livro em `docs/eval.md`.

## Testes
```bat
gcc -O2 -Iinclude tests\test_all.c src\amanda.c src\utils.c src\pdf_extractor.c src\chunker.c src\embedder.c src\question_gen.c src\decision_engine.c src\laya_backend.c src\packager.c src\eval.c src\calibra.c -o build\amanda_tests.exe -lws2_32 && build\amanda_tests.exe
scripts\test_pipeline.bat
```

## Estrutura
- `include/` headers públicos
- `src/` implementação C11 sem dependências externas (só `ws2_32` no Windows)
- `docs/` especificações de formato, pipeline e API
- `tests/` testes unitários em C
- `scripts/` pipelines de integração
- `version.bin` versão lida pelo binário e incrementada a cada `build.bat`
