# Guia JEV — testar, ver o funcionamento, analisar e criar exemplos

Passo a passo hands-on do backend `typesafe-http` (alias `jev`):
do primeiro julgamento ao seu próprio exemplo real, com os números
que vimos na prática. Conceitos em `docs/jev.md`, referência de
flags em `docs/typesafe.md`.

## 1. Pré-requisitos (5 min)

```bat
ollama --version            :: >= 0.35.0 (SystemOne em /v1/systemone)
ollama pull nimble          :: ~10GB, capability "decision"
copy examples\amandac.conf amandac.conf   :: e preencha TYPESAFE_API_KEY
```

`amandac.conf` é gitignored — a chave nunca sobe. Sem chave, só o
nimble local funciona (etapas de nuvem dão SKIP honesto).

```bat
build.bat                                   :: gera amandac.exe
amandac.exe compile --input examples\exemplo.txt --output build\exemplo.amanda --title "Exemplo"
```

Com os livros (pipeline completa dos 4): compile cada PDF de `pdf/`
para `build/*_t74.amanda` (nomes que `check_gold`/`check_typesafe`
esperam). Sem eles, as etapas de livro dão SKIP.

## 2. Ver o funcionamento (3 comandos)

```bat
:: a) local: baseline do motor (rápido, sem rede)
amandac.exe ask --package build\exemplo.amanda "O que e entropia?" --json

:: b) nimble local: mesmo grounding, confiança julgada pelo JEV (lento em CPU!)
amandac.exe ask --package build\exemplo.amanda "O que e entropia?" --json ^
  --backend typesafe-http --typesafe-url http://127.0.0.1:11434 ^
  --typesafe-model nimble --typesafe-timeout-ms 300000

:: c) nuvem: mesmo, com jev-latest (rápido, ~segundos)
amandac.exe ask --package build\exemplo.amanda "O que e entropia?" --json ^
  --backend typesafe-http --typesafe-model jev-latest
```

O que observamos de verdade no exemplo da entropia:

| caminho | `backend` | `confianca` | `pagina` | tempo |
|---|---|---|---|---|
| local | `local` | 0.9689 | 1 | ms |
| nimble | `typesafe-http` | 0.9978 | 1 | ~min (CPU) |
| nuvem | `typesafe-http` | 0.9200 | 1 | s |

Mesma página e citação nos três (grounding local); só a confiança
muda — é o julgamento real do JEV sobre o contexto.

## 3. Ler o resultado

```json
{"resposta":"...","probabilidade":0.92,"confianca":0.92,
 "pagina":1,"recusada":false,"backend":"typesafe-http"}
```

- `"backend"`: quem decidiu. `local` com `--backend typesafe-http`
  = fallback (JEV falhou; causa em `AMANDA_DEBUG=1`, abaixo).
- `probabilidade`/`confianca`: o **noul do JEV** (0–1). `recusada`
  = `noul < limiar_recusa` (default 0.30, pacote v3 ou flag).
- `pagina` + texto/`citacao`: extrativos do `.amanda` — o JEV nunca
  inventa fonte.

Diagnóstico de fallback (nunca exibe chaves):

```bat
set AMANDA_DEBUG=1
amandac.exe ask --package ... --backend typesafe-http ...
:: amanda: typesafe-judge falhou: typesafe: HTTP 401 (confira URL, modelo e TYPESAFE_API_KEY)
```

| mensagem | causa | ação |
|---|---|---|
| `HTTP 401` | chave inválida/ausente | conferir `amandac.conf`/env |
| `timeout`/`transporte` | engine fora, rede, nimble carregando | subir Ollama, aumentar `--typesafe-timeout-ms` |
| `sem answers.suporte.noul` | endpoint não-SystemOne | conferir `/v1/systemone` e modelo `nimble`/`jev-latest` |

## 4. Analisar: local × nimble × nuvem

Roteiro de comparação honesta (mesma pergunta, 3 caminhos):

```bat
amandac.exe eval --package build\exemplo.amanda --sample 1.0 > eval_local.txt
amandac.exe eval --package build\exemplo.amanda --sample 1.0 --backend typesafe-http --typesafe-url http://127.0.0.1:11434 --typesafe-model nimble --typesafe-timeout-ms 300000 > eval_nimble.txt
amandac.exe eval --package build\exemplo.amanda --sample 1.0 --backend typesafe-http --typesafe-model jev-latest > eval_nuvem.txt
```

O relatório conta `via_typesafe` × `via_local` (quantas caíram em
fallback) + fidelidade/gap/ECE/latência. Atenção: `eval` com nimble
em CPU demora (1 chamada por pergunta amostrada) — use
`--max-amostras 20` para uma primeira análise.

Regressão de respostas naturais (80 pins, recall@2):

```bat
scripts\check_gold.bat        :: motor local (baseline 75/80 auditada)
scripts\check_typesafe.bat    :: nimble + nuvem + 1 pin por livro (4/4 na nuvem, ver FASES.md)
```

## 5. Criar novos exemplos reais

### 5.1 Pergunta avulsa SystemOne (sem amandac)

Copie `examples/typesafe_request.json`, troque `state` (contexto) e
`instructions`, envie direto:

```bat
curl.exe -s -X POST http://127.0.0.1:11434/v1/systemone ^
  -H "Content-Type: application/json" -d "@examples\typesafe_request.json"
```

Resposta: `answers.suporte.noul` (+ `choice`/`score` se pedir —
nimble devolve `choice` + `probabilities` + `confidence`, ver
`docs/jev.md` § teste com 3 tipos).

### 5.2 Novo pin gold (vira regressão)

1. Compile o livro: `amandac.exe compile --input pdf\SEU.pdf --output build\seu_t74.amanda`
2. Pergunte nos 3 caminhos e anote a página observada estável:
   `ask --json` (local), + `--backend typesafe-http` (nimble/nuvem)
3. Só vire pin o que responde estável e auditado (citação lida!) —
   precedente: pins são comportamento observado, não chute
4. Adicione a linha em `scripts/check_gold.bat` **e** `check_typesafe.bat`
   (e espelhos `.sh`); rode os dois

### 5.3 Novo livro ponta a ponta

```bat
amandac.exe compile --input pdf\livro.pdf --output build\livro.amanda --title "Livro"
amandac.exe inspect --package build\livro.amanda --stats
amandac.exe calibrate --package build\livro.amanda --sample 0.5 --apply
amandac.exe ask --package build\livro.amanda "Pergunta do domínio" --json --backend typesafe-http --typesafe-model jev-latest
amandac.exe serve --package livro=build\livro.amanda --port 8080 --backend typesafe-http
```

`calibrate --apply` grava a calibração (v3); o serve usa sozinho.
Multi-livro: repita `--package nome=arq` (`"model"` seleciona).

## 6. Servir e plugar (OpenCode/agentes)

```bat
:: API OpenAI-compatible com JEV por trás (decisions = prioridade ALTA no pool)
set TYPESAFE_API_KEY=...
amandac.exe serve --package livro=build\livro.amanda --port 8080 --backend typesafe-http

:: MCP stdio (ask/decisions como ferramentas)
amandac.exe mcp --package livro=build\livro.amanda --backend typesafe-http
```

Configuração por arquivo (sem segredos): `--config-json amanda.json`
(`examples/amanda.json`; seção `typesafe` ou alias `jev` = caminho
do JEV por URL+modelo). Chaves: `amandac.conf`.

## 7. Atalhos

| quero | comando |
|---|---|
| tudo de uma vez (SKIP honesto) | `scripts\test_pipeline.bat` (12 etapas) |
| só JEV (nimble+nuvem+livros) | `scripts\check_typesafe.bat` |
| só DeepSeek | `scripts\check_deepseek.bat` |
| unitários | build + `build\amanda_tests.exe` (267 checks) |
| Linux | mesmos nomes com `.sh` + `sh` |
