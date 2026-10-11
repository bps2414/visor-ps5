#!/bin/bash
# Abre o Visor no PS5 pelo websrv e mostra o log dele pelos primeiros segundos.
#   tools/ps5-run.sh <ip-do-ps5> [segundos]
APP=/data/homebrew/Visor
curl -s -m "${2:-15}" "http://$1:8080/hbldr?pipe=1&path=$APP/visor.elf&cwd=$APP" -w "[http %{http_code}]\n"
