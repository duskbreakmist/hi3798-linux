# MV100 display initialization and Lima checkpoint

Latest clock/MP2 result: `GPU400-MP2-CPU1200-2026-10-29.md`.
GPU 200..400 MHz devfreq and two-PP operation passed; the MV100-only completion
poll fallback remains necessary. CPU 1.2 GHz failed despite a passing short
arithmetic result; the deployed default remains 1 GHz at the firmware voltage.

Earlier compositor/clock follow-up: `CLOCKS-TEARING-2026-10-29.md`.
The user confirmed normal window dragging with compositing enabled and
vblank_mode=xpresent. Session tooling now preserves preferences by default.

GPU result and method: `LIMA-APPS-2026-10-29.md`.
The `codex-lima` DT is a one-PP fallback; `codex-lima-mp2` enables both PPs
with serialized IRQ/poll completion. Native Mesa EGL/GLES2, a 1024-square frame,
the packaged X11 triangle and physical scanout updates passed at 400 MHz.
The original missing IRQ cause is still unresolved; no sustained stability claim.

Earlier CPU desktop profile: `DESKTOP-PERFORMANCE-2026-10-29.md`.
The CPU fallback uses software modesetting with no Xorg shadow buffer;
the accepted GPU desktop now keeps compositing enabled with XPresent to
address the user's window tearing report. It uses modesetting/glamor with a
process-local generic KMSRO entry.
Re-deployment and 4.4 recovery instructions are in `rootfs/README.md`.

Read `RESULTS-2026-10-29.md` for the earlier display/one-frame GPU results, evidence paths,
and validation boundaries. This is a local experimental bring-up, not a claim
that upstream Linux already contains an MV100 display-controller driver.

The display frontend is upstream simpledrm. Initial VDP/HDMI/G0 programming
reuses a narrowly selected portion of the user's local SDK source. Lima is
the upstream GPU driver; its MV100 PMU-domain quirk is separate from display.
SDK source licensing remains in effect; no SDK binary archive is committed.

## Build

Use WSL Ubuntu-24.04 as ray. The prepared SDK boot tree is required at
`/home/ray/workplace/HiSTBLinux/out/hi3798mv100/hi3798mv100_out/obj/source/boot`.
The SDK source is not modified by this build.

```sh
cd /home/ray/workplace/hi3798-linux
MV100_PROFILE=bootloader bash hi3798/build-mv100.sh configure
JOBS=14 bash hi3798/build-mv100.sh build
```

Outputs are outside the checkout, in `../hi3798-linux-build/6.12-mv100/`:

- `uImage-mv100-codex-display`: reserved 8 MiB framebuffer, automatic built-in
  display initialization, GPU DT node disabled.
- `uImage-mv100-codex-lima`: the display path plus enabled Lima node, with the
  MV100 PMU-domain and shared-IRQ fixes. PP1 IRQs are deliberately omitted until
  its completion issue is resolved; only PP0 is exposed to Mesa.
- `uImage-mv100-codex` and `...-io`: no display reservation; the initialization
  helper detects that condition and skips register writes.

Kbuild invokes `build-display-init-port.py --raw-only` to compile SDK units
into the output directory. The module-building mode of that script belongs
to the earlier isolated experiments: do not insmod an identically named
module into a kernel which already contains the built-in helper.

When changing external SDK headers/sources, clean the helper's output before
rebuilding; the SDK tree is an external build input, not a Git dependency of
this kernel checkout. Preserve the prepared SDK tree to reproduce a build.

## Diagnostic image

`MV100_PROFILE=diagnostic ... configure` creates the RAM-only initramfs.
Its `display` command uses the built-in display helper when present, writes
one framebuffer pattern, and reports status. `reboot` returns to the saved
boot path. Disk filesystems are not mounted by these commands.

The earlier CPU experiment now requires explicit `MV100_CPU_EXPERIMENT=1`
while configuring the diagnostic profile. Normal profiles keep the 1 GHz
CPU cap and do not automatically enable 1200/1500 MHz experiments.

## Limits

- Fixed 1920x1080p60 initialization, not full dynamic modesetting/hotplug.
- GPU one- and two-PP rendering passed, including 400 MHz hardware DFS.
  Two-PP mode needs polling for some completions; no pressure/FPS claim.
- Glamor works with the compatibility profiles. Shared-IRQ completion is
  serialized in two-PP mode; the captured panic and prior missing PP1 IRQs are
  documented, not erased by the successful poll-assisted result.
- GPU rates now come from CRG status and the SDK selector table, with a
  dedicated DFS clock/PMC handshake. No external oscillator/voltage measurement
  or PLL reprogramming was performed.
- VDEC/audio/other private multimedia drivers have not been ported by this
  display change. Do not infer hardware video decode from successful GPU use.
