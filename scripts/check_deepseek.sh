#!/bin/sh
# check_deepseek.sh - redacao real via DeepSeek (POSIX). Espelha check_deepseek.bat.
# Sem DEEPSEEK_API_KEY: SKIP honesto. NUNCA exibe a chave.
set -eu
cd "$(dirname "$0")/.."
BIN=./amandac
if [ ! -x "$BIN" ] && [ -x ./build/amandac ]; then BIN=./build/amandac; fi
if [ ! -x "$BIN" ]; then echo "[ERRO] binario amandac nao encontrado (cmake --build build)"; exit 1; fi
HAS_DS_KEY=0
if [ -n "${DEEPSEEK_API_KEY:-}" ]; then HAS_DS_KEY=1; fi
if [ -f amandac.conf ] && grep -q '^DEEPSEEK_API_KEY=' amandac.conf 2>/dev/null; then HAS_DS_KEY=1; fi
if [ "$HAS_DS_KEY" != "1" ]; then
  echo "[deepseek] SKIP: sem DEEPSEEK_API_KEY e sem amandac.conf com a chave"
  exit 0
fi
if [ ! -f /tmp/exemplo.amanda ]; then
  echo "[deepseek] compilando pacote de teste..."
  "$BIN" compile --input examples/exemplo.txt --output /tmp/exemplo.amanda --title "Exemplo Amanda"
fi
DS_OUT=/tmp/amanda_ds_out.json
echo "[deepseek] nuvem api.deepseek.com com deepseek-chat..."
"$BIN" ask --package /tmp/exemplo.amanda "O que e entropia?" --json \
  --backend deepseek-http --deepseek-model deepseek-chat \
  --deepseek-timeout-ms 120000 > "$DS_OUT"
grep -q '"backend":"deepseek-http"' "$DS_OUT" && echo "[deepseek] nuvem: OK"
echo "[OK] deepseek smoke passou."
