# Amanda — Compilador de Conhecimento para Empresas

[![CI](https://github.com/dantas72/amanda/actions/workflows/ci.yml/badge.svg)](https://github.com/dantas72/amanda/actions)

![Amanda](amandaLogo.png)

Repositório: `https://github.com/dantas72/amanda`

Leia em: [English](README.en.md) · [Русский](README.ru.md) · [中文](README.zh.md)

© 2026 LabsObjects — criado e implementado por Fernando Dantas, Brasil.
Licença MIT: `LICENSE` (PT-BR), `LICENSE.en` (US English),
`LICENSE.ru` (RU), `LICENSE.zh` (CN).

Transforma os documentos da sua operação (`PDF / TXT / CSV / JSON`)
em artefatos de decisão `.amanda`: cada resposta vem com a fonte e a
página citada, nível de confiança calibrado e recusa automática fora
de escopo. Roda 100% local, sem dependência de nuvem, e se integra
aos seus sistemas por CLI ou API compatível com OpenAI — sozinho ou
redigindo via LLM (ver `docs/laya.md`). Ideal para compliance,
jurídico, financeiro e atendimento.

## Build (Windows)
```bat
build.bat
```
Gera `amandac.exe` e incrementa `version.bin` (versão atual em `## Versão` abaixo).

## Build (Linux/Mac)
```sh
cmake -S . -B build && cmake --build build
cmake --build build --target version_bump   # paridade com build.bat (incrementa version.bin)
```

## CI
GitHub Actions (`.github/workflows/ci.yml`): build + testes unitários
(`ctest`) + integração (`compile`/`inspect`/`ask`/serve smoke) em
Windows e Linux (macOS temporariamente fora; ver Fase 7.5), com
artefatos `amandac` + `amanda_tests` por release de CI (Fase 7.6).
A etapa Laya é opcional (SKIP sem o engine).

## Uso
```sh
amandac compile --input examples/exemplo.txt --output exemplo.amanda
amandac compile --config examples/config.yaml --output exemplo.amanda
amandac inspect --package exemplo.amanda --stats
amandac inspect --package exemplo.amanda --json   # relatório máquina (Fase 7.6)
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

## Serve calibrado (Fase 7.2)

```sh
amandac serve --package exemplo.amanda --port 8080 --conf-center 0.100 --conf-slope 22.0 --limiar-recusa 0.90
```

Aplica a calibração do `calibrate`/`eval` ao servidor (banner mostra os
parâmetros ativos). Backend segue sempre local no `serve`: com servidor
single-thread, uma inferência LLM por request travaria o serviço
(reavaliar na Fase 7.5, com threads). Ver `docs/api.md`.

## Status das fases

Fases 1–6 + 7.1 prontas (`FASES.md`): núcleo, CMake+CI, Laya-HTTP,
SSE+embeddings, `eval`, `calibrate`, docs/higiene. Em andamento:
Fase 7 (7.2 serve calibrado, 7.3 config+templates, 7.4 PDF+,
7.5 servidor robusto, 7.6 release).

## Testes
```bat
gcc -O2 -Iinclude tests\test_all.c src\amanda.c src\utils.c src\pdf_extractor.c src\chunker.c src\embedder.c src\question_gen.c src\decision_engine.c src\laya_backend.c src\packager.c src\eval.c src\calibra.c src\config.c -o build\amanda_tests.exe -lws2_32 && build\amanda_tests.exe
scripts\test_pipeline.bat
```

## Estrutura
- `include/` headers públicos
- `src/` implementação C11 sem dependências externas (só `ws2_32` no Windows)
- `docs/` especificações de formato, pipeline e API
- `tests/` testes unitários em C
- `scripts/` pipelines de integração
- `version.bin` versão lida pelo binário e incrementada a cada `build.bat`

## Versão
`version.bin` é a fonte da verdade, incrementada a cada `build.bat`.
Último build local:
Build: `1.0.26`
