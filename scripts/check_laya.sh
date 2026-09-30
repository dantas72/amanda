#!/bin/sh
# check_laya.sh — verifica o engine do Laya (v1.10.1+)
# Engine esperado em http://127.0.0.1:8420
# Saida: OK ou SKIP. Nunca falha a pipeline (sempre retorna 0).
HOST="${LAYA_HOST_OVERRIDE:-127.0.0.1}"
PORT="${LAYA_PORT_OVERRIDE:-8420}"
BASE="http://$HOST:$PORT"
for p in /health /api/health /docs /; do
  if curl -fsS -m 5 "$BASE$p" -o /dev/null 2>/dev/null; then
    echo "[laya] OK: engine responde em $BASE$p"
    exit 0
  fi
done
echo "[laya] SKIP: engine do Laya nao responde em $BASE (instale o Laya e rode o engine para ativar esta etapa)"
exit 0
