# MV100 desktop profile

Observed result and investigation: `DESKTOP-PERFORMANCE-2026-10-29.md` in the
parent directory. This profile uses CPU rendering, not accelerated glamor.

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
The actual ray xfwm4 session bus is used to turn software compositing off.

No LightDM restart is performed by the installer or profile service. Changing
the current Xorg backend needs a separate controlled session restart; the
current deployed profile has already passed the functional check and user
confirmation. No reboot is needed simply to reinstall these files.

Recovery:

```sh
systemctl disable mv100-desktop-profile.service
python3 /usr/local/share/mv100-desktop/profile.py legacy
systemctl restart lightdm
```

The helper has a bounded 3-second device wait. It never replaces Mesa libraries,
starts a GPU test, writes flash, or changes bootloader variables. It assumes the
existing LightDM/LXQt desktop and Xorg modesetting driver are installed.
