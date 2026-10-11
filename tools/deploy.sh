#!/bin/bash
# Envia dist/Visor para o PS5 por FTP (ftpsrv na porta 2121).
#   tools/deploy.sh <ip-do-ps5>
set -e
cd "$(dirname "$0")/../dist/Visor"
find . -type f | while read -r f; do
  curl -sS --ftp-create-dirs -T "$f" "ftp://$1:2121/data/homebrew/Visor/${f#./}"
done
echo "enviado para /data/homebrew/Visor"
