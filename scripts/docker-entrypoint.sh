#!/bin/sh
# Amanda — entrypoint do container.
# Uso:
#   serve (padrao): serve todos os /data/*.amanda (multi por model=basename).
#   qualquer outro argv: executa `amandac <argv>` (ex.: mcp, ask, inspect).
# Env:
#   PORT (8080), HOST (0.0.0.0), WORKERS (8), MAX_CONNS, EVAL_MAX,
#   AMANDA_API_KEY, CORS, CONF_CENTER, CONF_SLOPE, LIMIAR_RECUSA,
#   BACKEND (local|laya-http), LAYA_URL, LAYA_TIMEOUT_MS, LAYA_MAX,
#   DATA_DIR (/data), IGNORE_CALIB (1 = --ignore-calib).
set -eu

if [ "${1:-serve}" != "serve" ]; then
  exec amandac "$@"
fi
shift || true

DATA_DIR="${DATA_DIR:-/data}"
PORT="${PORT:-8080}"
HOST="${HOST:-0.0.0.0}"

pkgs=""
n=0
for f in "$DATA_DIR"/*.amanda; do
  [ -e "$f" ] || continue
  base="$(basename "$f" .amanda)"
  if [ -z "$pkgs" ]; then
    pkgs="--package $base=$f"
  else
    pkgs="$pkgs --package $base=$f"
  fi
  n=$((n + 1))
done

if [ "$n" -eq 0 ]; then
  echo "entrypoint: nenhum .amanda em $DATA_DIR" >&2
  echo "entrypoint: monte um volume com pacotes: -v /seus:/data" >&2
  echo "entrypoint: ou compile fora e copie o .amanda para /data" >&2
  exit 1
fi

# shellcheck disable=SC2086
exec amandac serve $pkgs \
  --host "$HOST" --port "$PORT" \
  ${WORKERS:+--workers "$WORKERS"} \
  ${MAX_CONNS:+--max-conns "$MAX_CONNS"} \
  ${EVAL_MAX:+--eval-max "$EVAL_MAX"} \
  ${AMANDA_API_KEY:+--api-key "$AMANDA_API_KEY"} \
  ${CORS:+--cors "$CORS"} \
  ${CONF_CENTER:+--conf-center "$CONF_CENTER"} \
  ${CONF_SLOPE:+--conf-slope "$CONF_SLOPE"} \
  ${LIMIAR_RECUSA:+--limiar-recusa "$LIMIAR_RECUSA"} \
  ${BACKEND:+--backend "$BACKEND"} \
  ${LAYA_URL:+--laya-url "$LAYA_URL"} \
  ${LAYA_TIMEOUT_MS:+--laya-timeout-ms "$LAYA_TIMEOUT_MS"} \
  ${LAYA_MAX:+--laya-max "$LAYA_MAX"} \
  ${IGNORE_CALIB:+--ignore-calib} \
  "$@"
