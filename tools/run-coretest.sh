#!/bin/bash
# Uso (WSL): tools/run-coretest.sh <ip-do-quest> [olho]
mkdir -p ~/visor-run/assets
cp "$(dirname "$0")/../assets/scrcpy-server" ~/visor-run/assets/
cd ~/visor-run
timeout 70 ~/visor-build/coretest "$1" shot.bmp "${2:-0}" 3
echo "exit=$?"
