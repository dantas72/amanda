#!/bin/sh
# Teste de integracao Linux/Mac
set -e
cd "$(dirname "$0")/.."
BIN=./amandac
if [ ! -x "$BIN" ] && [ -x ./build/amandac ]; then BIN=./build/amandac; fi
echo "[1/4] compile..."
$BIN compile --input examples/exemplo.txt --output /tmp/exemplo.amanda --title "Exemplo Amanda"
echo "[2/4] inspect..."
$BIN inspect --package /tmp/exemplo.amanda --stats
echo "[3/4] ask..."
$BIN ask --package /tmp/exemplo.amanda "O que e entropia?"
echo "[4/4] serve (smoke)..."
$BIN serve --package /tmp/exemplo.amanda --port 18080 &
SRV=$!
sleep 2
curl -fsS http://127.0.0.1:18080/v1/models
curl -fsS -X POST http://127.0.0.1:18080/v1/chat/completions \
  -H 'Content-Type: application/json' \
  -d '{"model":"amanda","messages":[{"role":"user","content":"O que e entropia?"}]}'
kill $SRV
echo "[5/5] laya (engine externo, opcional)..."
sh "$(dirname "$0")/check_laya.sh"
echo "[OK] pipeline de integracao passou."
