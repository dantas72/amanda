#!/bin/sh
# Serve um pacote .amanda (Linux/Mac)
set -e
cd "$(dirname "$0")/.."
BIN=./amandac
if [ ! -x "$BIN" ] && [ -x ./build/amandac ]; then BIN=./build/amandac; fi
PKG="${1:-exemplo.amanda}"
PORT="${2:-8080}"
exec $BIN serve --package "$PKG" --port "$PORT"
