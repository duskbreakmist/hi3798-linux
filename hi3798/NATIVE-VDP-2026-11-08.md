# Native VDP DRM/KMS (experimental)

Native RGB DMA scanout, real 60Hz vblank/page-flips and a standard NV12 overlay
are verified on Hi3798MV100. Weston10 + Debian mpv0.35 dmabuf-wayland promotes
an unobscured video window to the NV12 plane; when another window overlaps it,
the compositor uses GPU composition and restores the NV12 plane afterwards.
The existing SDK-based HDMI clock/timing initialization bridge is still required.
This is not an upstream-ready or vendor-independent HDMI implementation.

## Build with the existing shortcuts

```sh
./1start_menuconfig.sh --variant codex-native
./2just_menuconfig.sh --variant codex-native
./3start_make_cp.sh --variant codex-native
```

These explicitly enable DRM_HI3798MV100 and VIDEOBUF2_DMA_CONTIG. The default
variant remains codex-lima-mp2. Native output requires cma=256M when using the
FHD decoder pool, and the separate capture_dma=1 V4L2 module/runtime.
The shortcut copies hi_kernel.bin to its reported release directory; it never
flashes. Source build alone does not install the decoder or Wayland runtime.

## Evidence and current limits

- Standard libdrm: 120 page flips at 60.000Hz.
- Input FIR COPY is essential; DISEN reproducibly stops NV12 after half a frame.
- Corrected final VDP output matches all 2,073,600 luma bytes of a known frame.
- 1920x1080 to 1280x720 at +320+180: all 921,600 luma pixels match nearest sampling.
- Physically contiguous V4L2 capture: 150/150 hashes match software decode.
- A 60s pv4 A/V run completed; 9 sampled VO drops by57.9s, decoder drops0.
  A diagnostic WBC readback ran during that test; it is not a pure benchmark.
- Fixed 1080p60 only; no EDID/hotplug/HDMI mode setting port yet.
- NV12 BT709 limited range; nearest scaling, no high-quality FIR tables yet.
- DRM master/compositor owns planes. Existing Xorg modesetting/glamor desktop
  uses native RGB KMS, but its ordinary mpv X11 output does not select the NV12
  hardware plane. Use a Wayland compositor with mpv dmabuf-wayland for that.
- Browser hardware decoding and standardized DRM writeback are not supplied.
- Decoder tile-to-linear conversion still uses CPU; no end-to-end zero-copy claim.

Detailed methods, runtime helpers, logs and screenshots are in the companion
hi3798-mpv-hardware-acceleration repository, docs/kernel-next-native-kms-2026-11-08.md.
