#!/bin/sh
# Compila um documento para .amanda (Linux/Mac)
set -e
cd "$(dirname "$0")/.."
BIN=./amandac
if [ ! -x "$BIN" ] && [ -x ./build/amandac ]; then BIN=./build/amandac; fi
INPUT="${1:-examples/exemplo.txt}"
OUTPUT="${2:-exemplo.amanda}"
exec $BIN compile --input "$INPUT" --output "$OUTPUT"
