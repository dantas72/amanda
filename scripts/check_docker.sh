#!/bin/sh
# Valida o Dockerfile (build completo + testes unitarios dentro da imagem).
# SKIP honesto sem docker; falha de verdade com docker presente.
set -eu
cd "$(dirname "$0")/.."
if ! command -v docker >/dev/null 2>&1; then
  echo "[docker] SKIP: docker nao instalado"
  exit 0
fi
docker build -t amandac-test .
echo "[docker] build: OK"
docker run --rm amandac-test version
echo "[OK] docker smoke passou."
