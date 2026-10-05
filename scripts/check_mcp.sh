#!/bin/sh
# Smoke do MCP server via stdio (POSIX): initialize -> tools/list ->
# ask/decisions/version/ping + erro -32602. Sem dependencias alem de sh+grep.
set -eu
cd "$(dirname "$0")/.."
BIN=./amandac
if [ ! -x "$BIN" ] && [ -x ./build/amandac ]; then BIN=./build/amandac; fi
if [ ! -x "$BIN" ]; then echo "[ERRO] binario amandac nao encontrado (cmake --build build)"; exit 1; fi
if [ ! -f /tmp/exemplo.amanda ]; then
  echo "[mcp] compilando pacote de teste..."
  "$BIN" compile --input examples/exemplo.txt --output /tmp/exemplo.amanda --title "Exemplo Amanda"
fi
REQ=/tmp/amanda_mcp_req.txt
OUT=/tmp/amanda_mcp_out.txt
ERR=/tmp/amanda_mcp_err.txt
printf '%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' \
  '{"jsonrpc":"2.0","method":"notifications/initialized"}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}' \
  '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"ask","arguments":{"pergunta":"O que e entropia?"}}}' \
  '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"decisions","arguments":{"pergunta":"O que e entropia?"}}}' \
  '{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"version","arguments":{}}}' \
  '{"jsonrpc":"2.0","id":"a-b","method":"ping"}' \
  '{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"inexistente","arguments":{}}}' \
  > "$REQ"
"$BIN" mcp --package /tmp/exemplo.amanda < "$REQ" > "$OUT" 2> "$ERR"
grep -q 'protocolVersion' "$OUT" && echo "[mcp] initialize: OK"
grep -q '"ask"' "$OUT" && grep -q '"decisions"' "$OUT" && echo "[mcp] tools/list: OK"
grep -q 'confianca' "$OUT" && echo "[mcp] ask: OK"
grep -q 'probabilidade' "$OUT" && echo "[mcp] decisions: OK"
grep -q 'amandac' "$OUT" && echo "[mcp] version: OK"
grep -q 'a-b' "$OUT" && echo "[mcp] ping: OK"
grep -q '32602' "$OUT" && echo "[mcp] erro -32602: OK"
echo "[OK] mcp smoke passou."
