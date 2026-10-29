#!/bin/sh
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
out=${1:?Usage: build-kmsro-select.sh OUTPUT_DIRECTORY}
mkdir -p "$out"
${CC:-arm-linux-gnueabihf-gcc} -shared -fPIC -O2 -Wall -Wextra "$src/mv100-drm-select.c" -ldl -pthread -o "$out/libkmsro-lima-select.so"
