#!/bin/sh
# Teste de integracao Linux/Mac
set -e
cd "$(dirname "$0")/.."
BIN=./amandac
if [ ! -x "$BIN" ] && [ -x ./build/amandac ]; then BIN=./build/amandac; fi
echo "[1/4] compile..."
$BIN compile --input examples/exemplo.txt --output /tmp/exemplo.amanda --title "Exemplo Amanda"
echo "[1b/4] compile via --config..."
$BIN compile --config examples/config.yaml --output /tmp/exemplo_cfg.amanda
$BIN inspect --package /tmp/exemplo_cfg.amanda --stats
rm -f /tmp/exemplo_cfg.amanda
echo "[2/4] inspect..."
$BIN inspect --package /tmp/exemplo.amanda --stats
echo "[3/4] ask..."
$BIN ask --package /tmp/exemplo.amanda "O que e entropia?"
echo "[4/4] serve (smoke)..."
$BIN serve --package /tmp/exemplo.amanda --port 18080 --conf-center 0.12 --conf-slope 12 --limiar-recusa 0.3 &
SRV=$!
sleep 2
curl -fsS http://127.0.0.1:18080/v1/models
curl -fsS --max-time 10 -X POST http://127.0.0.1:18080/v1/chat/completions \
  -H 'Content-Type: application/json' \
  -d '{"model":"amanda","messages":[{"role":"user","content":"O que e entropia?"}]}'
curl -fsS --max-time 10 -X POST http://127.0.0.1:18080/v1/embeddings \
  -H 'Content-Type: application/json' \
  -d @examples/smoke_embeddings.json | grep -o '"total_tokens":[0-9]*'
curl -fsS --max-time 10 -N -X POST http://127.0.0.1:18080/v1/chat/completions \
  -H 'Content-Type: application/json' \
  -d @examples/smoke_stream.json | grep -q 'data: \[DONE\]'
kill $SRV
echo "[5/7] eval (Fase 5 - calibracao)..."
$BIN eval --package /tmp/exemplo.amanda --sample 1.0
echo "[6/8] calibrate (Fase 6)..."
$BIN calibrate --package /tmp/exemplo.amanda --sample 1.0
echo "[7/8] laya (engine externo, opcional)..."
sh "$(dirname "$0")/check_laya.sh"
echo "[7/8] laya-llm (Fase 3, backend opcional)..."
sh "$(dirname "$0")/check_laya_llm.sh"
echo "[OK] pipeline de integracao passou."
