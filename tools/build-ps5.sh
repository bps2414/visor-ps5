#!/bin/bash
# Compila para PS5 e monta dist/Visor (a pasta que vai em /data/homebrew).
# Rodar no WSL, com o toolchain em /opt/ps5-payload-sdk.
set -e
SRC="$(cd "$(dirname "$0")/.." && pwd)"
BUILD=~/visor-build-ps5

/opt/ps5-payload-sdk/bin/prospero-cmake -S "$SRC" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null
rm -f "$BUILD/visor"
make -C "$BUILD" -j8 visor visor-tile 2>&1 | grep -E "error|Error |undefined" -A6 || true
[ -f "$BUILD/visor" ] || { echo "BUILD FALHOU"; exit 1; }

rm -rf "$SRC/dist"
mkdir -p "$SRC/dist/Visor"
/opt/ps5-payload-sdk/bin/prospero-strip -o "$SRC/dist/Visor/visor.elf" "$BUILD/visor"
cp "$BUILD/visor-tile" "$SRC/dist/Visor/visor-tile.elf"
cp -r "$SRC/assets" "$SRC/fonts" "$SRC/sce_sys" "$SRC/homebrew.js" "$SRC/dist/Visor/"
(cd "$SRC/dist" && zip -qr Visor.zip Visor)
ls -la "$SRC/dist/Visor" "$SRC/dist/Visor.zip"
