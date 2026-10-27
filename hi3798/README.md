# Hi3798MV100 independent kernel development

Status: stage 0, upstream build baseline only. No MV100 board DTS or private
media/display drivers have been integrated. Do not flash this baseline.

The `upstream` remote is kernel.org stable Linux. `stable-mirror` is the official
GitHub stable mirror. `community` is loonpn/ec6108v9c-linux-kernel, used as a
pinned reference rather than merged wholesale. The initial checkout is shallow;
deepen the upstream history when needed for bisect or rebasing.

Build in WSL as ray with the Ubuntu arm-linux-gnueabihf GCC toolchain:

```sh
bash hi3798/build.sh configure
bash hi3798/build.sh build
```

Outputs default to `../hi3798-linux-build/6.12-baseline`. Override OUT_DIR,
CROSS_COMPILE or JOBS as needed. The scripts do not deploy, mount a rootfs,
write boot partitions or modify the SDK.

`baseline.config` deliberately exercises Btrfs/Zstd, container prerequisites and
Lima compilation. A successful build does not establish that those features or
the board work at runtime. The saved configuration check is not a Docker
compatibility certification.

Next: audit and port MV100 machine/SMP/cache setup, clock/reset, board DTS,
USB/eMMC and Ethernet; then validate the board before enabling experimental GPU
or mounting existing data volumes. Keep the SDK 4.4 kernel available for recovery.

The source provenance and community-file triage are in manifest.json and
community-audit.json. Preserve author/license headers when importing patches.
