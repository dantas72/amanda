# API Amanda (OpenAI-compatible)

Base: `http://127.0.0.1:8080`

Flags de calibração do `serve` (Fase 7.2):
`--conf-center F --conf-slope F --limiar-recusa F`
(zeros = padrão 0.12/12.0/0.30; banner mostra os valores ativos).
Sugeridos pelo `amandac calibrate`. Backend sempre local no `serve`.

Robustez do `serve` (Fase 7.5, endurecido na 13):
`--cors ORIGEM` (default `*`, ecoado em todas as respostas + preflight),
`--api-key CHAVE` ou `AMANDA_API_KEY` (env) ou `--api-key-file ARQ`
(precedência: flag > env > arquivo; sem chave = aberto; preflight
`OPTIONS` sempre livre; a chave nunca aparece em log),
`--max-body BYTES` (default 1048576; acima = `413`),
`--max-conns N` (default 16; fila+ativas — cheio = `503` + `Retry-After: 2`),
`--workers N` (default 8, teto 64; pool fixo com fila limitada),
`--eval-max N` (default 200; teto de amostradas no `POST /v1/eval`).
Cabeçalho limitado a 64KB (`431`), timeout de leitura 30s por conexão.
Log de acesso em `stderr` (hora, método, rota, código, ms — sem corpo
nem chave). Sem TLS próprio: em produção, atrás de reverse-proxy
(nginx/Caddy) com HTTPS.

Inferência LLM no `serve` (Fase 11):
`--backend local|laya-http` (default `local`),
`--laya-url URL` (default `http://127.0.0.1:8420`),
`--laya-timeout-ms MS` (default 60000),
`--laya-max N` (default 2; teto de inferências LLM simultâneas),
`--laya-queue N` (default 16; teto de esperas na fila),
`--laya-queue-ms MS` (default 5000; espera máxima; `0` = sem espera).
Com `laya-http`, `chat` e `decisions` tentam o engine do Laya sobre o
grounding local e caem para o motor local em qualquer falha, com fila
cheia ou com a espera esgotada. `decisions` tem prioridade ALTA na
fila sobre `chat` (NORMAL). Respostas trazem `"backend":"local"` ou
`"laya-http"`.
`embeddings` e `eval` seguem sempre locais.

## Citação multi top-2 (Fase 12.2)

`chat` e `decisions` citam os 2 melhores chunks (`top_k >= 2`):
`citacao` = `"[p.X] <400 chars>[ [p.Y] <400 chars>]"` e o texto
abre com `"Com base no documento (p. X[, Y]): ..."`. `pagina` segue
sendo o melhor chunk (compatível com gold recall@2 e `eval`).

## GET /v1/models
```json
{"object":"list","data":[{"id":"amanda","object":"model","owned_by":"amanda","permission":[]}]}
```
Com multi-pacote (Fase 13), lista um `id` por pacote.

## Multi-pacote (Fase 13)

`--package` repetível (até 8; `nome=caminho` ou basename):
```bat
amandac.exe serve --package cvm=build\cvm_teste74.amanda --package ibri=build\ibri_t74.amanda --port 8080
```
O campo `"model"` seleciona o pacote (`"amanda"` ou omitido = 1º);
`GET /v1/models` descobre os nomes; `decisions`/`eval` aceitam
`"model"` opcional; modelo desconhecido = `404` com a lista.
Cada pacote usa sua calibração gravada (v3) + índice próprio.
`GET /v1/amanda/info` traz o 1º pacote + `"pacotes":[nomes]`.

## GET /v1/amanda/info
```json
{"titulo":"...","chunks":3,"perguntas":12,"dimensao":384,"idioma":"pt-BR","versao":"1.0.1"}
```

## POST /v1/chat/completions
```json
{"model":"amanda","messages":[{"role":"user","content":"O que é entropia?"}]}
```
Resposta:
```json
{"id":"chatcmpl-amanda","object":"chat.completion","model":"amanda",
 "choices":[{"index":0,"message":{"role":"assistant","content":"... (p. 1)"},"finish_reason":"stop"}],
 "amanda":{"confianca":0.93,"pagina":1,"backend":"local"}}
```

## POST /v1/chat/completions (streaming SSE)
```json
{"model":"amanda","stream":true,"messages":[{"role":"user","content":"O que é entropia?"}]}
```
Resposta `Content-Type: text/event-stream`:
```
data: {"id":"chatcmpl-amanda","object":"chat.completion.chunk","model":"amanda","choices":[{"index":0,"delta":{"role":"assistant","content":"... [p.1]"},"finish_reason":null}]}

data: {"id":"chatcmpl-amanda","object":"chat.completion.chunk","model":"amanda","choices":[{"index":0,"delta":{"content":"... "},"finish_reason":null}]}

data: {"id":"chatcmpl-amanda","object":"chat.completion.chunk","model":"amanda","choices":[{"index":0,"delta":{},"finish_reason":"stop"}],"amanda":{"confianca":0.993,"pagina":1}}

data: [DONE]
```

## POST /v1/embeddings
```json
{"input": "entropia"}
```
(`input` aceita string ou array de strings.) Resposta:
```json
{"object":"list","data":[{"object":"embedding","index":0,"embedding":[0.0, ... 384 floats ...]}],
 "model":"amanda","usage":{"prompt_tokens":1,"total_tokens":1}}
```

## POST /v1/decisions
```json
{"pergunta":"Qual é a capital do Brasil?"}
```
Resposta:
```json
{"resposta":"...","probabilidade":0.91,"confianca":0.91,"pagina":1,"citacao":"...","recusada":false,"backend":"local"}
```

## POST /v1/eval (Fase 7.5)
Roda o `eval` sobre o pacote servido (backend local, calibração do `serve`):
```json
{"sample": 0.1, "seed": 42, "top_k": 3}
```
Resposta: mesmo JSON do `amandac eval --json` (inclui `cobertura`).
Se a amostra exceder `--eval-max`, retorna `400` pedindo `sample` menor.
