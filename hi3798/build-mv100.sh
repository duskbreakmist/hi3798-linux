#!/usr/bin/env bash
set -euo pipefail
src=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
out=$(realpath -m "${OUT_DIR:-$src/../hi3798-linux-build/6.12-mv100}")
case "$out/" in "$src/"*) echo 'Output must be outside source' >&2; exit 2;; esac
export ARCH=arm CROSS_COMPILE="${CROSS_COMPILE:-arm-linux-gnueabihf-}"
export KBUILD_BUILD_USER=ray KBUILD_BUILD_HOST=hi3798-linux
export KBUILD_BUILD_TIMESTAMP='2026-10-27 00:00:00 +0000'
export SOURCE_DATE_EPOCH=1793059200
export KBUILD_BUILD_VERSION=1
action=${1:-build}
profile=${MV100_PROFILE:-ssd}
mkdir -p "$out"
case "$action" in
  configure)
    python3 "$src/hi3798/make-profile.py" "$profile" "$out/requested.config"
    if [ "$profile" = diagnostic ]; then
      python3 "$src/hi3798/make-initramfs.py" "$out/probe-root"
    fi
    KCONFIG_ALLCONFIG="$out/requested.config" make -C "$src" O="$out" allnoconfig
    if [ "$profile" = diagnostic ]; then
      "$src/scripts/config" --file "$out/.config" --set-str INITRAMFS_SOURCE "$out/probe-root/probe.cpio"
    fi
    make -C "$src" O="$out" olddefconfig
    python3 "$src/hi3798/verify-mv100.py" "$out/.config"
    ;;
  build)
    python3 "$src/hi3798/verify-mv100.py" "$out/.config"
    make -C "$src" O="$out" -j"${JOBS:-14}" zImage modules hisilicon/hi3798mv100-codex.dtb hisilicon/hi3798mv100-codex-io.dtb hisilicon/hi3798mv100-codex-display.dtb hisilicon/hi3798mv100-codex-lima.dtb
    python3 "$src/hi3798/package-mv100.py" "$out"
    python3 "$src/hi3798/verify-dtb.py" "$out"
    ;;
  check) python3 "$src/hi3798/verify-mv100.py" "$out/.config" ;;
  *) echo 'Usage: build-mv100.sh {configure|build|check}' >&2; exit 2;;
esac
