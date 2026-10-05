#!/bin/sh
# sweep_typesafe.sh - curva tradeoff limiar_recusa x JEV (POSIX). Espelha sweep_typesafe.bat.
# 4 pins (1/livro) x 5 limiares = 20 chamadas SystemOne rapidas.
# SKIP sem chave/livros; exit 0 sempre (analise, nao regressao).
set -eu
cd "$(dirname "$0")/.."
BIN=./amandac
if [ ! -x "$BIN" ] && [ -x ./build/amandac ]; then BIN=./build/amandac; fi
if [ ! -x "$BIN" ]; then echo "[ERRO] binario amandac nao encontrado (cmake --build build)"; exit 1; fi
HAS_TS_KEY=0
if [ -n "${TYPESAFE_API_KEY:-}" ]; then HAS_TS_KEY=1; fi
if [ -f amandac.conf ] && grep -q '^TYPESAFE_API_KEY=' amandac.conf 2>/dev/null; then HAS_TS_KEY=1; fi
if [ "$HAS_TS_KEY" != "1" ]; then
  echo "[sweep] SKIP: sem TYPESAFE_API_KEY e sem amandac.conf com a chave"
  exit 0
fi
for b in build/cvm_teste74.amanda build/ibri_t74.amanda build/inv_t74.amanda build/dir_t74.amanda; do
  if [ ! -f "$b" ]; then echo "[sweep] SKIP: livros *_t74 ausentes em build"; exit 0; fi
done
TS_OUT=/tmp/amanda_sweep_out.json
echo "LIMIAR RESP RANK_OK RANK_OK_RESP RECUSA_RANK_OK"
for LIM in 0.30 0.50 0.70 0.85 0.89; do
  RESP=0; ROK=0; ROKR=0; RREC=0
  sw_pin() {
    pkg=$1; pag=$2; shift 2; perg=$*
    out=$("$BIN" ask --package "$pkg" "$perg" --json \
      --backend typesafe-http --typesafe-model jev-latest \
      --typesafe-timeout-ms 120000 --limiar-recusa "$LIM")
    if ! printf '%s' "$out" | grep -q '"backend":"typesafe-http"'; then
      echo "[sweep] AVISO: fallback local em $pkg limiar $LIM, pin ignorado"
      return 0
    fi
    RANK=0
    if printf '%s' "$out" | grep -q "\"pagina\":$pag"; then RANK=1;
    elif printf '%s' "$out" | grep -qF "[p.$pag]"; then RANK=1; fi
    if [ "$RANK" = "1" ]; then ROK=$((ROK + 1)); fi
    if printf '%s' "$out" | grep -q '"recusada":false'; then
      RESP=$((RESP + 1))
      if [ "$RANK" = "1" ]; then ROKR=$((ROKR + 1)); fi
    else
      if [ "$RANK" = "1" ]; then RREC=$((RREC + 1)); fi
    fi
  }
  sw_pin build/cvm_teste74.amanda 139 "O que caracteriza uma companhia aberta?"
  sw_pin build/ibri_t74.amanda 70 "O que faz a area de relacoes com investidores?"
  sw_pin build/inv_t74.amanda 81 "O que e analise tecnica?"
  sw_pin build/dir_t74.amanda 531 "O que e responsabilidade civil?"
  echo "$LIM $RESP $ROK $ROKR $RREC"
done
echo "[OK] sweep concluido (4 pins x 5 limiares, recall@2)."
