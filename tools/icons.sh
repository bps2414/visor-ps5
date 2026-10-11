#!/bin/bash
# Gera os PNGs a partir de icon.svg (rodar no WSL).
cd "$(dirname "$0")/.."
rsvg-convert -w 512 -h 512 icon.svg -o sce_sys/icon0.png
rsvg-convert -w 240 -h 240 icon.svg -o assets/logo.png
