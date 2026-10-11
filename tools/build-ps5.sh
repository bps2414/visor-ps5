#!/bin/bash
# Compila para PS5 e gera dist/visor.elf (o payload, com tudo embutido).
# Rodar no WSL, com o toolchain em /opt/ps5-payload-sdk.
set -e
SRC="$(cd "$(dirname "$0")/.." && pwd)"
BUILD=~/visor-build-ps5

/opt/ps5-payload-sdk/bin/prospero-cmake -S "$SRC" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null
rm -f "$BUILD/visor"
make -C "$BUILD" -j8 visor 2>&1 | grep -E "error|Error |undefined" -A6 || true
[ -f "$BUILD/visor" ] || { echo "BUILD FALHOU"; exit 1; }

rm -rf "$SRC/dist"
mkdir -p "$SRC/dist"
/opt/ps5-payload-sdk/bin/prospero-strip -o "$SRC/dist/visor.elf" "$BUILD/visor"
ls -la "$SRC/dist"
