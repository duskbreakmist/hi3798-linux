# Read-only diagnostics

`mv100-gpu-soc-probe.c` takes one snapshot of the always-on CRG/PMC controls.
It does not read the powered-off GPU aperture or write registers. It is an
optional out-of-tree module, not a required driver or persistent installation.
Loading it taints the kernel in the normal way; unload it after reading dmesg.

Build outside the kernel checkout, using the output directory/configuration
matching the board's running kernel:

```sh
mkdir -p ../hi3798-linux-build/gpu-soc-probe
cp hi3798/display-init/diagnostics/mv100-gpu-soc-probe.c ../hi3798-linux-build/gpu-soc-probe/
printf 'obj-m += mv100-gpu-soc-probe.o\n' > ../hi3798-linux-build/gpu-soc-probe/Makefile
make -C ../hi3798-linux-build/6.12-mv100 M="$PWD/../hi3798-linux-build/gpu-soc-probe" ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- modules
```

Copy the module into `/run` on the board, insmod once, save the two dmesg lines,
and rmmod `mv100_gpu_soc_probe`. See the parent research record for actual values.

`verify-direct-kms-window.py` requires the built-in display bridge's read-only
status parameter and a live LightDM Xorg session. It briefly maps/destroys one
96x96 red window and checks the physical scanout corner pixel. It is a functional
update check, not a frame-rate or bandwidth benchmark. It temporarily covers the
top-left corner. The tested profile is already accepted; do not repeat it as a
stress loop.
