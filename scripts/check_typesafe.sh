#!/bin/sh
# check_typesafe.sh - pipeline real JEV/TypeSafe (POSIX). Espelha check_typesafe.bat.
# 1. nimble no Ollama :11434, sem chave. 2. nuvem com TYPESAFE_API_KEY.
# SKIP honesto por etapa. NUNCA exibe a chave.
set -eu
cd "$(dirname "$0")/.."
BIN=./amandac
if [ ! -x "$BIN" ] && [ -x ./build/amandac ]; then BIN=./build/amandac; fi
if [ ! -x "$BIN" ]; then echo "[ERRO] binario amandac nao encontrado (cmake --build build)"; exit 1; fi
if [ ! -f /tmp/exemplo.amanda ]; then
  echo "[typesafe] compilando pacote de teste..."
  "$BIN" compile --input examples/exemplo.txt --output /tmp/exemplo.amanda --title "Exemplo Amanda"
fi
TS_OUT=/tmp/amanda_ts_out.json
HAS_NIMBLE=0
if curl -s --max-time 5 http://127.0.0.1:11434/api/tags -o /tmp/amanda_ollama.json 2>/dev/null; then
  if grep -q nimble /tmp/amanda_ollama.json 2>/dev/null; then HAS_NIMBLE=1; fi
fi
if [ "$HAS_NIMBLE" = "1" ]; then
  echo "[typesafe] nimble local: ask com julgamento JEV real..."
  "$BIN" ask --package /tmp/exemplo.amanda "O que e entropia?" --json \
    --backend typesafe-http --typesafe-url http://127.0.0.1:11434 \
    --typesafe-model nimble --typesafe-timeout-ms 300000 > "$TS_OUT"
  grep -q '"backend":"typesafe-http"' "$TS_OUT" && echo "[typesafe] nimble local: OK"
else
  echo "[typesafe] SKIP: nimble indisponivel no Ollama :11434"
fi
if [ -z "${TYPESAFE_API_KEY:-}" ]; then
  echo "[typesafe] SKIP: TYPESAFE_API_KEY ausente, sem teste nuvem"
  echo "[typesafe] livros: SKIP sem nuvem, nimble local e lento demais por pergunta"
  echo "[OK] typesafe smoke passou."
  exit 0
fi
echo "[typesafe] nuvem api.typesafe.ai com jev-latest..."
"$BIN" ask --package /tmp/exemplo.amanda "O que e entropia?" --json \
  --backend typesafe-http --typesafe-model jev-latest \
  --typesafe-timeout-ms 120000 > "$TS_OUT"
grep -q '"backend":"typesafe-http"' "$TS_OUT" && echo "[typesafe] nuvem: OK"
TPASS=0
TFAIL=0
ts_gcheck() {
  pkg=$1; pag=$2; shift 2; perg=$*
  if [ ! -f "$pkg" ] && [ -f "/tmp/$(basename "$pkg")" ]; then pkg="/tmp/$(basename "$pkg")"; fi
  if [ ! -f "$pkg" ]; then echo "[typesafe] SKIP livro ausente: $pkg"; return 0; fi
  out=$("$BIN" ask --package "$pkg" "$perg" --json \
    --backend typesafe-http --typesafe-model jev-latest \
    --typesafe-timeout-ms 120000)
  if printf '%s' "$out" | grep -q "\"pagina\":$pag"; then
    TPASS=$((TPASS + 1))
  elif printf '%s' "$out" | grep -qF "[p.$pag]"; then
    TPASS=$((TPASS + 1))
  else
    TFAIL=$((TFAIL + 1))
    echo "[typesafe] FAIL: $pkg p.$pag esperada: $perg"
    printf '%s\n' "$out"
  fi
}
echo "[typesafe] livros com jev-latest, 1 pin por livro..."
ts_gcheck build/cvm_teste74.amanda 139 "O que caracteriza uma companhia aberta?"
ts_gcheck build/ibri_t74.amanda 70 "O que faz a area de relacoes com investidores?"
ts_gcheck build/inv_t74.amanda 81 "O que e analise tecnica?"
ts_gcheck build/dir_t74.amanda 531 "O que e responsabilidade civil?"
echo "[typesafe] livros: $TPASS PASS, $TFAIL FAIL, 4 pins"
if [ "$TFAIL" != "0" ]; then exit 1; fi
echo "[OK] typesafe smoke passou."
