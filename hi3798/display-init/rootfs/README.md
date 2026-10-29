# MV100 desktop profile

Observed results: `DESKTOP-PERFORMANCE-2026-10-29.md` and the newer
`LIMA-APPS-2026-10-29.md` in the parent directory. The default is CPU rendering;
the tested single-PP Lima kernel can explicitly opt into accelerated glamor.

Run deployment on the live board as root, after copying the Python files to
`/run`. Do not assume the offline WSL rootfs is the board's current rootfs.

```sh
python3 /run/mv100-desktop-profile.py install --legacy-config /path/to/original-fbdev.conf
python3 /run/desktop-session-performance.py
systemctl start mv100-desktop-profile.service
```

The installer preserves the supplied original fbdev configuration. The service
selects the verified software modesetting/no-shadow profile before LightDM only
on MV100 with a modern kernel and card0 bound to simple-framebuffer. Otherwise it
restores the original fbdev profile, retaining the old 4.4 recovery path.
The actual ray xfwm4 session bus is used. The session helper defaults to keeping
the user's settings; it no longer silently turns compositing off. The CPU
fallback was initially tested without composition, but the GPU desktop's later
tearing report was resolved with composition and XPresent enabled:

```sh
python3 /run/desktop-session-performance.py --compositing on --vblank-mode xpresent
```

After a successful guarded GPU desktop trial, enable the persistent GPU profile:

```sh
python3 /run/mv100-desktop-profile.py install --enable-glamor
```

With a modern kernel, simpledrm, the Lima render node and the accepted marker,
the service selects glamor. If GPU support is absent, it selects the CPU path.
The process-local KMSRO Xorg wrapper also bypasses the override for old 4.4.
This avoids global EGL/Mesa library replacement. The newer two-PP profile
uses a documented MV100 IRQ/poll completion workaround; a one-PP fallback is
still available. GPU devfreq covers 200..400 MHz; CPU defaults to 1 GHz because
the 1.2 GHz runtime attempt subsequently panicked.
The user's xfconf compositor choices persist independently of this kernel/backend
selector. Do not reset them during a kernel deployment or use a no-compositor
historical snapshot as the current accepted desktop preference.

For the isolated test tools, cross-compile `apps/lima-render-image.c` with
`arm-linux-gnueabihf-gcc -O2 -Wall -Wextra ... -ldl -o lima-render-image`, put
that binary alongside `install-lima-tools.py`, `mv100-gles2-test` and
`mv100-mesa-run` on the board, then run `python3 install-lima-tools.py` as root.
The installer atomically replaces its own executables and adds ray to the
standard render/video groups. New login sessions inherit those permissions.

No LightDM restart is performed by the installer or profile service. Changing
the current Xorg backend needs a separate controlled session restart; the
CPU profile had user confirmation; the GPU profile has recorded EGL and physical
scanout acceptance. No reboot is needed simply to reinstall these files.

Recovery:

```sh
systemctl disable mv100-desktop-profile.service
python3 /usr/local/share/mv100-desktop/profile.py legacy
systemctl restart lightdm
```

To retain automatic selection but choose the faster CPU profile instead of
glamor, run `python3 /usr/local/share/mv100-desktop/profile.py cpu`, then restart
LightDM. The GPU device remains available for isolated GLES applications.

The helper has a bounded 3-second device wait. It never replaces Mesa libraries,
starts a GPU test, writes flash, or changes bootloader variables. It assumes the
existing LightDM/LXQt desktop and Xorg modesetting driver are installed.
