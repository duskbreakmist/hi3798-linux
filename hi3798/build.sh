#!/usr/bin/env bash
set -euo pipefail
src=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
out=$(realpath -m "${OUT_DIR:-$src/../hi3798-linux-build/6.12-baseline}")
case "$out/" in "$src/"*) echo 'Build output must be outside source tree' >&2; exit 2;; esac
cross=${CROSS_COMPILE:-arm-linux-gnueabihf-}
jobs=${JOBS:-8}
action=${1:-build}
export ARCH=arm CROSS_COMPILE="$cross"
export KBUILD_BUILD_USER=ray KBUILD_BUILD_HOST=hi3798-linux
export KBUILD_BUILD_TIMESTAMP='2026-10-27 00:00:00 +0000'
export SOURCE_DATE_EPOCH=1793059200
mkdir -p "$out"
case "$action" in
  configure)
    KCONFIG_ALLCONFIG="$src/hi3798/baseline.config" make -C "$src" O="$out" allnoconfig
    python3 "$src/hi3798/verify-config.py" "$out/.config"
    ;;
  check)
    python3 "$src/hi3798/verify-config.py" "$out/.config"
    ;;
  build)
    test -f "$out/.config" || { echo 'Run build.sh configure first' >&2; exit 2; }
    python3 "$src/hi3798/verify-config.py" "$out/.config"
    make -C "$src" O="$out" -j"$jobs" zImage modules
    ;;
  *) echo 'Usage: build.sh {configure|check|build}' >&2; exit 2;;
esac
