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
Windows e Linux (macOS temporariamente fora; ver `FASES.md`), com
artefatos `amandac` + `amanda_tests` por release de CI (Fase 7.6).
A etapa Laya é opcional (SKIP sem o engine).

## Uso
```sh
amandac compile --input examples/exemplo.txt --output exemplo.amanda
amandac compile --config examples/config.yaml --output exemplo.amanda
amandac inspect --package exemplo.amanda --stats
amandac inspect --package exemplo.amanda --json   # relatório máquina (Fase 7.6, com calibracao v3)
amandac ask --package exemplo.amanda "O que é entropia?"
amandac ask --package exemplo.amanda "O que é entropia?" --json
amandac eval --package exemplo.amanda --sample 0.1
amandac calibrate --package exemplo.amanda --sample 0.5 --apply
amandac serve --package exemplo.amanda --port 8080
amandac serve --package cvm=livro1.amanda --package ibri=livro2.amanda --port 8080  # multi (Fase 13)
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
`docs/eval.md`. Regressão de respostas naturais:
`scripts/check_gold.bat` (80 perguntas curadas, recall@2).

## Backend Laya (Fase 3, opcional)

```sh
amandac ask --package exemplo.amanda "O que é entropia?" --backend laya-http --laya-url http://127.0.0.1:8420
```

Envia (contexto + pergunta) ao `POST /chat` do engine Laya (LLM via
Ollama, ex. `nimble`) e combina com o grounding local
(página/citação/confiança). Qualquer falha cai para o motor local
automaticamente. Detalhes em `docs/laya.md`.

## Recalibração (Fase 6, gravada em v3 na 12.4)

```sh
amandac calibrate --package exemplo.amanda --sample 0.5
amandac calibrate --package exemplo.amanda --sample 0.5 --validacao examples/gold_cvm.json --apply
amandac ask --package exemplo.amanda "Pergunta" --conf-center 0.200 --conf-slope 16.0 --limiar-recusa 0.85
amandac eval --package exemplo.amanda --conf-center 0.200 --conf-slope 16.0 --limiar-recusa 0.85
```

O `calibrate` busca em grade (centro × inclinação × limiar, passo 0.01
no limiar) o ponto que maximiza a acurácia balanceada (aceitar
in-scope, recusar probes); com `--validacao`, o objetivo vira
`(bal + recall@2)/2` sobre perguntas naturais rotuladas. `--apply`
grava no pacote (formato `.amanda` v3); `ask`/`eval`/`serve` usam
automaticamente (flag CLI prevalece, `--ignore-calib` força o padrão).
Zeros = padrão histórico. Detalhes e tabela por livro em `docs/eval.md`.

## Serve (Fases 7.2/7.5/11/13)

```sh
amandac serve --package exemplo.amanda --port 8080 --conf-center 0.200 --conf-slope 16.0 --limiar-recusa 0.85
amandac serve --config examples/config.yaml
AMANDA_API_KEY=segredo amandac serve --package exemplo.amanda --port 8080 --workers 8
```

Pool fixo de workers com fila (`--workers`, `--max-conns` = teto total
com `503`), chave via flag/env/arquivo (nunca logada), `serve
--config` (seção `servidor:`), log de acesso em `stderr`, sem TLS
próprio (produção atrás de reverse-proxy). Multi-pacote por `"model"`
(ver `docs/api.md`). Com `--backend laya-http`, chat/decisions tentam
o engine Laya com fallback local automático (campo `"backend"`).

## Status das fases

Fases 1–13 prontas (`FASES.md`): núcleo Windows em C puro, CMake+CI,
Laya via HTTP, SSE+embeddings, `eval`, `calibrate` (+`--apply` v3 e
`--validacao`), extração PDF+, serve robusto e enterprise (pool,
multi-pacote por `model`), release com artefatos, retrieval BM25 +
stopwords PT + multi-citação, rerank (stemming/junk/norma
saturante/phrase), índice invertido + cache de query. Referência
(`amandac 1.0.34`, pacotes v3): CVM 86.64%, IBRI 87.41%, INVEST 88.27%,
Direito 84.00%; latência 0.3–5ms; probes 3/3; gold 75/80 auditado
(ver `docs/eval.md`).

## Testes
```bat
gcc -O2 -Wall -Wextra -std=c11 -Iinclude tests\test_all.c src\amanda.c src\utils.c src\pdf_extractor.c src\chunker.c src\embedder.c src\question_gen.c src\decision_engine.c src\laya_backend.c src\packager.c src\server.c src\eval.c src\calibra.c src\config.c -o build\amanda_tests.exe -lws2_32 && build\amanda_tests.exe
scripts\test_pipeline.bat
scripts\check_gold.bat
```
Referência: **181 checks** + pipeline (8 etapas) + gold (80 perguntas,
recall@2). CI: Windows + Linux (`ctest`); macOS fora com diagnóstico
pendente em Mac real.

## Estrutura
- `include/` headers públicos
- `src/` implementação C11 sem dependências externas (só `ws2_32` no Windows)
- `docs/` especificações de formato (`formato_amanda.md`), pipeline, API, eval, Laya, guia de uso, tutorial empresarial
- `templates/` enunciados das perguntas tipadas (+ packs `empresas/`: compliance, financeiro, jurídico, atendimento)
- `examples/` exemplo, `config.yaml`, fixtures de smoke e golds de regressão
- `tests/` testes unitários em C
- `scripts/` pipelines de integração
- `version.bin` versão lida pelo binário e incrementada a cada `build.bat`

## Roteiro futuro (pós-13, documentado; não implementado)
- **Docker**: imagem com `amandac` + `serve` como entrypoint (multi-pacote por volume).
- **MCP server**: expor `ask`/`decisions` como ferramentas MCP para OpenCode e agentes.
- **Pool LLM**: fila própria com prioridade para inferências `laya-http` (hoje: slots com fallback imediato).
- **Testes em GPU**: repetir Laya vivo (nimble + llama3.2:3B) em GTX 1660 Ti e GPU 10GB+ (ver `docs/laya.md`).
- **CI macOS**: reativar com diagnóstico em Mac real (falha ARM desde 7.2).

## Versão
`version.bin` é a fonte da verdade, incrementada a cada `build.bat`.
Último build local:
Build: `1.0.34`
