# Docker — imagem `amandac`

Imagem multi-stage (`Dockerfile`): stage `build` compila com
`gcc`+`cmake` e **roda os testes unitários** (build falha se algum
teste falhar); stage `runtime` (debian-slim, usuário `amanda`
sem root, só o binário + entrypoint).

## Build

```sh
docker build -t amandac .
```

## Serve (padrão)

```sh
docker run --rm -p 8080:8080 -v /seus/amanda:/data amandac
```

O entrypoint (`scripts/docker-entrypoint.sh`) serve **todos** os
`/data/*.amanda` (multi-pacote, `model` = basename sem extensão).
Sem `.amanda` em `/data`, o container explica e sai com erro.

Variáveis de ambiente (todas opcionais):

| var | default | flag equivalente |
|---|---|---|
| `PORT` | `8080` | `--port` |
| `HOST` | `0.0.0.0` | `--host` |
| `WORKERS` | `8` | `--workers` |
| `MAX_CONNS` | — | `--max-conns` |
| `EVAL_MAX` | — | `--eval-max` |
| `AMANDA_API_KEY` | — (aberto) | `--api-key` (nunca logada) |
| `CORS` | — | `--cors` |
| `CONF_CENTER` / `CONF_SLOPE` / `LIMIAR_RECUSA` | — | idem |
| `BACKEND` | `local` | `--backend` (`laya-http` exige `LAYA_URL` alcançável) |
| `LAYA_URL` / `LAYA_TIMEOUT_MS` / `LAYA_MAX` | — | idem |
| `LAYA_QUEUE` / `LAYA_QUEUE_MS` | `16` / `5000` | fila LLM com prioridade (decisions > chat) |
| `TYPESAFE_URL` / `TYPESAFE_MODEL` / `TYPESAFE_TIMEOUT_MS` | nuvem / `jev-latest` / `120000` | backend `typesafe-http` (chave via `TYPESAFE_API_KEY` ou `TYPESAFE_KEY_FILE`) |
| `DEEPSEEK_URL` / `DEEPSEEK_MODEL` / `DEEPSEEK_TIMEOUT_MS` | nuvem / `deepseek-chat` / `120000` | backend `deepseek-http` (chave via `DEEPSEEK_API_KEY` ou `DEEPSEEK_KEY_FILE`) |
| `IGNORE_CALIB` | — | `--ignore-calib` |
| `DATA_DIR` | `/data` | diretório varrido |

Exemplo com chave + calibração:

```sh
docker run --rm -p 8080:8080 -v /seus/amanda:/data \
  -e AMANDA_API_KEY=segredo -e WORKERS=8 \
  -e CONF_CENTER=0.2 -e CONF_SLOPE=16 -e LIMIAR_RECUSA=0.85 amandac
```

## Outros comandos

Qualquer argv diferente de `serve` executa o `amandac` direto:

```sh
docker run --rm -v /seus/amanda:/data amandac inspect --package /data/livro.amanda --stats
docker run --rm -i -v /seus/amanda:/data amandac mcp --package /data/livro.amanda
```

## Produção

Sem TLS próprio: rode atrás de reverse-proxy (nginx/caddy) para
HTTPS, como no `serve` nativo. Saúde: `GET /v1/models`.
