# API Amanda (OpenAI-compatible)

Base: `http://127.0.0.1:8080`

## GET /v1/models
```json
{"object":"list","data":[{"id":"amanda","object":"model","owned_by":"amanda","permission":[]}]}
```

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
 "amanda":{"confianca":0.93,"pagina":1}}
```

## POST /v1/decisions
```json
{"pergunta":"Qual é a capital do Brasil?"}
```
Resposta:
```json
{"resposta":"...","probabilidade":0.91,"confianca":0.91,"pagina":1,"citacao":"...","recusada":false}
```
