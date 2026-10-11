#!/bin/bash
# Build de PC + captura de uma tela, sem monitor (rodar no WSL).
#   tools/uishot.sh <saida.bmp> "<teclas>"      ex.: "down down ok"
#   RESET=1 apaga os ajustes salvos antes.
set -e
SRC="$(cd "$(dirname "$0")/.." && pwd)"
cmake -S "$SRC" -B ~/visor-build -DCMAKE_BUILD_TYPE=Release >/dev/null
make -C ~/visor-build -j8 visor 2>&1 | grep -E "error|Error " -A6 || true
rm -rf ~/visor-run/assets ~/visor-run/fonts
mkdir -p ~/visor-run && cp -r "$SRC/assets" "$SRC/fonts" ~/visor-run/
cd ~/visor-run
[ -n "$RESET" ] && rm -f visor.conf
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy VISOR_SHOT="$1" VISOR_KEYS="$2" \
    timeout 90 ~/visor-build/visor 2>&1 | grep -v "^$" | tail -8
ls -la "$1" | cut -c1-60
