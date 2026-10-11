#!/bin/bash
# Manda um ELF para o elfldr do PS5 (porta 9021) e mostra a saida dele.
#   tools/send-elf.sh <ip-do-ps5> <arquivo.elf>
exec 3<>"/dev/tcp/$1/9021"
cat "$2" >&3
timeout 10 cat <&3
