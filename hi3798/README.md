# Hi3798MV100 independent kernel development

Status: stage 1 offline MV100 candidate. Machine/SMP/clock sources and two
board DTBs are integrated and compiled. No new kernel boot has been validated.
The original build-only baseline remains available; do not flash either image.

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

## MV100 RAM diagnostic candidate

```sh
bash hi3798/build-mv100.sh configure
bash hi3798/build-mv100.sh build
```

The default output is ../hi3798-linux-build/6.12-mv100. Candidate uImages use
the SDK fastboot's legacy ARM/Linux format: payload load/entry 0x02000000,
uncompressed wrapper around zImage plus appended DTB. A static /init is embedded.
The diagnostic DTB enables UART/timer only; the optional -io DTB also probes
USB, conservative 25 MHz eMMC and Ethernet. Both force a RAM-root command line
with maxcpus=1 and mem=512M, irrespective of the old rootfs boot arguments.

No media, display, sound or GPU node is enabled. SMP reset code is compiled but
not exercised by the default candidate. CPU hotplug is disabled. Cache setup is
the normal mainline Cortex-A7 path; cache/coherency correctness remains untested.
The first kernel prints SCTLR/ACTLR values for later hardware review.

Use the Windows workspace's serial_ram_boot.py only after agreeing on a
service interruption and physical power-cycle recovery. It can transfer via
YMODEM to RAM 0x10000000 and bootm that address; it has no flash/saveenv operation.
A stall may require a physical reset/power cycle. The saved old boot path is
unchanged. Never assume the existing SSD or eMMC rootfs is safe to mount merely
because this offline build succeeded.
