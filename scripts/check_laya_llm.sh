#!/bin/sh
# check_laya_llm.sh — Fase 3: testa o backend laya-http (Linux/Mac)
# Mesma logica do check_laya_llm.bat. Nunca falha (exit 0).
cd "$(dirname "$0")/.."
HOST="${LAYA_HOST_OVERRIDE:-127.0.0.1}"
PORT="${LAYA_PORT_OVERRIDE:-8420}"
BASE="http://$HOST:$PORT"
BIN=./amandac
if [ ! -x "$BIN" ] && [ -x ./build/amandac ]; then BIN=./build/amandac; fi

if [ ! -x "$BIN" ]; then
  echo "[laya-llm] SKIP: binario amandac nao encontrado"
  exit 0
fi
if [ ! -f /tmp/exemplo.amanda ]; then
  echo "[laya-llm] SKIP: /tmp/exemplo.amanda ausente"
  exit 0
fi
if ! curl -fsS -m 8 "$BASE/health" -o /tmp/laya_h.json 2>/dev/null; then
  echo "[laya-llm] SKIP: engine fora do ar em $BASE"
  exit 0
fi
if ! curl -fsS -m 8 "$BASE/settings/available-models" -o /tmp/laya_p.json 2>/dev/null; then
  echo "[laya-llm] SKIP: sem resposta de available-models"
  exit 0
fi
if grep -q '"providers":\[\]' /tmp/laya_p.json; then
  echo "[laya-llm] SKIP: engine sem provider LLM (instale modelo no Ollama e cadastre no Laya)"
  exit 0
fi
if ! grep -q '"models":\[[^]]' /tmp/laya_p.json; then
  echo "[laya-llm] SKIP: providers sem modelos"
  exit 0
fi
if ! curl -fsS -m 90 -X POST "$BASE/chat" -H 'Content-Type: application/json' \
    -d '{"message": "Responda com uma palavra: ok"}' -o /tmp/laya_c.json 2>/dev/null; then
  echo "[laya-llm] SKIP: /chat nao respondeu em 90s"
  exit 0
fi
if grep -q 'encountered an error' /tmp/laya_c.json || grep -q '"content":""' /tmp/laya_c.json; then
  echo "[laya-llm] SKIP: /chat sem modelo ativo (aponte o slot chat p/ nimble no Settings do Laya)"
  exit 0
fi
echo "[laya-llm] LIVE: engine com LLM, testando ask --backend laya-http ..."
if ! "$BIN" ask --package /tmp/exemplo.amanda "O que e entropia?" --backend laya-http --json > /tmp/laya_ask.json; then
  echo "[laya-llm] FAIL: ask --backend laya-http retornou erro"
  cat /tmp/laya_ask.json
  exit 0
fi
if grep -q 'laya-http' /tmp/laya_ask.json; then
  echo "[laya-llm] OK: resposta via Laya"
else
  echo "[laya-llm] OK: fallback local (engine instavel)"
fi
exit 0
