# Backend JEV/TypeSafe — `amandac --backend typesafe-http` (alias `jev`)

Julgamento real (noul calibrado) sobre o grounding local: o `amandac`
recupera os chunks e a citação como sempre, e o JEV responde se o
contexto sustenta a pergunta. `probabilidade`/`confianca` passam a ser
o noul do JEV; `pagina`/`citacao` continuam locais. Qualquer falha
(engine fora, timeout, HTTP != 2xx, resposta sem noul) = fallback
local honesto (campo `"backend"` informa).

Dois motores, mesmo protocolo System One (`state` + `questions`):

| motor | URL | chave | quando usar |
|---|---|---|---|
| nimble local (Ollama) | `http://127.0.0.1:11434` | nenhuma | desenvolvimento, sem custo, sem rede |
| nuvem TypeSafe | `https://api.typesafe.ai` (default) | `TYPESAFE_API_KEY` | produção, `jev-latest` |

## Chave (nunca no repo, nunca logada)

Precedência: `--typesafe-key` > env `TYPESAFE_API_KEY` >
`--typesafe-key-file` > `amandac.conf`. No Windows:

```powershell
$env:TYPESAFE_API_KEY = "sua-chave-do-console.typesafe.ai"
```

Ou `amandac.conf` na raiz do projeto (gitignored, formato
`KEY=valor` — copie `examples/amandac.conf` e preencha):

```
TYPESAFE_API_KEY=sua-chave-aqui
```

`AMANDA_CONF` troca o caminho do conf. O `amanda.json` e o
`config.yaml` NÃO aceitam chaves (só URL/modelo).

## Uso

```sh
# nimble local (Ollama 0.35+ com nimble baixado; lento em CPU: timeout alto)
amandac ask --package livro.amanda "Pergunta" --json \
  --backend typesafe-http --typesafe-url http://127.0.0.1:11434 \
  --typesafe-model nimble --typesafe-timeout-ms 300000

# nuvem (rápida; 1 pergunta = 1 chamada SystemOne)
amandac ask --package livro.amanda "Pergunta" --json \
  --backend typesafe-http --typesafe-model jev-latest

# serve multi com JEV (pool compartilhado; decisions = prioridade ALTA)
TYPESAFE_API_KEY=... amandac serve --package cvm=livro1.amanda \
  --package ibri=livro2.amanda --port 8080 --backend typesafe-http

# via amanda.json (endpoints/modelos; chave continua no ambiente)
amandac ask --package livro.amanda "Pergunta" --config-json examples/amanda.json
amandac serve --package livro.amanda --config-json amanda.json --backend typesafe-http
```

`--typesafe-url` aceita base (`https://api.typesafe.ai`) ou URL
completa (`.../v1/systemone`); `--typesafe-model` default
`jev-latest`. `eval`/`mcp` aceitam as mesmas flags
(`eval --backend typesafe-http` conta `via_typesafe` no relatório).

## Pipeline real

```bat
scripts\check_typesafe.bat
```

```sh
sh scripts/check_typesafe.sh
```

Etapas com SKIP honesto: (1) nimble local — 1 `ask`, exige
`"backend":"typesafe-http"`; (2) nuvem — 1 `ask` com `jev-latest`
(só com `TYPESAFE_API_KEY`); (3) livros — 1 pin gold por
`*_t74.amanda` presente (só nuvem: nimble em CPU é lento demais por
pergunta). Segunda passagem (nuvem de verdade): exporte
`TYPESAFE_API_KEY` e rode o mesmo script.

## OpenCode (consumindo o especialista)

O `serve` segue OpenAI-compatible (chat/decisions/embeddings) e o
`amandac mcp` expõe `ask`/`decisions` via MCP — o OpenCode consome
pelos dois caminhos com ou sem JEV por trás (ver `docs/mcp.md` e
`examples/mcp_config.json`).

## Notas

- `AMANDA_DEBUG=1` mostra no `stderr` o motivo do fallback
  (ex.: timeout, HTTP 401) — sem nunca exibir chaves.
- Nimble em CPU é lento (minutos por pergunta em contexto grande);
  pipeline local usa 1 pergunta de propósito. Em GPU, repita com
  mais pins.
- Cada `ask` typesafe = 1 POST SystemOne (`input_tokens`+1 de
  `output_tokens` — decisão, não geração).
- HTTP 401 = chave inválida/ausente (o erro cita o código); o
  fallback local mantém o serviço no ar.
