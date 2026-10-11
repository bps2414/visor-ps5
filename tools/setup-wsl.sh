#!/bin/bash
# Ambiente de build (Ubuntu 24.04, rodar como root): host + toolchain de PS5.
set -e
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq clang-18 lld-18 cmake pkg-config zip librsvg2-bin \
    libsdl2-dev libsdl2-image-dev libfreetype-dev libssl-dev \
    libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev
if [ ! -d /opt/ps5-payload-sdk ]; then
    curl -fsSL -o /tmp/ps5-payload-dev.tar.gz \
        https://github.com/ps5-payload-dev/pacbrew-repo/releases/latest/download/ps5-payload-dev.tar.gz
    tar xf /tmp/ps5-payload-dev.tar.gz -C /
    rm /tmp/ps5-payload-dev.tar.gz
fi
# RmlUi nao tem pacote no 24.04: compila pro host na mesma versao do toolchain de PS5.
if [ ! -f /usr/local/lib/cmake/RmlUi/RmlUiConfig.cmake ]; then
    rm -rf /tmp/RmlUi && mkdir /tmp/RmlUi
    curl -fsSL https://github.com/mikke89/RmlUi/archive/refs/tags/6.2.tar.gz         | tar xz --strip-components=1 -C /tmp/RmlUi
    cmake -S /tmp/RmlUi -B /tmp/RmlUi/build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF >/dev/null
    make -C /tmp/RmlUi/build -j"$(nproc)" install >/dev/null
    rm -rf /tmp/RmlUi
fi
echo SETUP_OK
ls /opt/ps5-payload-sdk /opt/ps5-payload-sdk/bin | head -60
