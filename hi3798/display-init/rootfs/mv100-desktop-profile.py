#!/usr/bin/python3
"""Select the verified MV100 Xorg profile, preserving the 4.4 recovery path."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

ROOT = Path('/usr/local/share/mv100-desktop')
CONFIG = Path('/etc/X11/xorg.conf.d/10-fbdev.conf')
SERVICE = Path('/etc/systemd/system/mv100-desktop-profile.service')
KMS = '''Section "Device"
    Identifier "MV100 DRM software"
    Driver "modesetting"
    Option "kmsdev" "/dev/dri/card0"
    Option "AccelMethod" "none"
    Option "ShadowFB" "false"
    Option "SWcursor" "true"
EndSection
'''
KMS_GPU = KMS.replace('"MV100 DRM software"', '"MV100 Lima KMS"').replace(
    '    Option "AccelMethod" "none"\n    Option "ShadowFB" "false"',
    '    Option "AccelMethod" "glamor"')
XORG_WRAPPER = '''#!/bin/sh
# MV100 process-local KMSRO; the old 4.4 recovery path uses plain Xorg.
if [ -c /dev/dri/renderD128 ] &&
   grep -q 'Option "AccelMethod" "glamor"' /etc/X11/xorg.conf.d/10-fbdev.conf; then
    exec env MESA_LOADER_DRIVER_OVERRIDE=pl111 /usr/lib/xorg/Xorg "$@"
fi
exec /usr/lib/xorg/Xorg "$@"
'''
UNIT = '''[Unit]
Description=Select MV100 Xorg backend for the running kernel
After=local-fs.target systemd-udev-trigger.service
Before=lightdm.service display-manager.service
ConditionPathExists=/proc/device-tree/compatible

[Service]
Type=oneshot
ExecStart=/usr/bin/python3 /usr/local/share/mv100-desktop/profile.py select
RemainAfterExit=yes

[Install]
WantedBy=graphical.target
'''


def atomic_write(path, data):
    temporary = path.with_name(path.name + '.mv100-tmp')
    temporary.write_bytes(data)
    temporary.chmod(0o644)
    temporary.replace(path)


def select(force_legacy=False):
    legacy = ROOT / 'legacy-fbdev.conf'
    if not legacy.is_file():
        raise SystemExit('Missing legacy profile; leaving Xorg configuration unchanged')
    modern = int(os.uname().release.split('.')[0]) >= 5 and not force_legacy
    driver = Path('/sys/class/drm/card0/device/driver')
    if modern:
        # Probe is normally complete well before LightDM; never wait indefinitely.
        deadline = time.monotonic() + 3
        while not driver.exists() and time.monotonic() < deadline:
            time.sleep(0.1)
    direct = modern and driver.exists() and driver.resolve().name == 'simple-framebuffer'
    gpu_driver = Path('/sys/class/drm/renderD128/device/driver')
    gpu = (direct and (ROOT/'glamor-enabled').is_file() and gpu_driver.exists()
           and gpu_driver.resolve().name == 'lima')
    data = (KMS_GPU if gpu else KMS).encode() if direct else legacy.read_bytes()
    if not CONFIG.exists() or CONFIG.read_bytes() != data:
        atomic_write(CONFIG, data)
    print('MV100 Xorg profile: ' + ('modesetting, Lima glamor' if gpu else
          'modesetting, CPU rendering, no shadow' if direct else 'legacy fbdev'))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('action', choices=['install', 'select', 'legacy', 'cpu'])
    p.add_argument('--legacy-config', type=Path)
    p.add_argument('--enable-glamor', action='store_true', help='Opt in after successful GPU desktop trial')
    args = p.parse_args()
    if os.geteuid() != 0:
        raise SystemExit('Run as root')
    compatible = Path('/proc/device-tree/compatible').read_bytes().split(b'\0')
    if b'hisilicon,hi3798mv100' not in compatible:
        raise SystemExit('This profile is specific to Hi3798MV100')
    if args.action == 'install':
        ROOT.mkdir(parents=True, exist_ok=True)
        saved = ROOT / 'legacy-fbdev.conf'
        if args.legacy_config:
            data = args.legacy_config.read_bytes()
            if not re.search(rb'Driver\s+"fbdev"', data) or b'"modesetting"' in data:
                raise SystemExit('The supplied legacy configuration must use fbdev')
            if saved.exists() and saved.read_bytes() != data:
                raise SystemExit('A different legacy backup already exists; inspect it first')
            atomic_write(saved, data)
        if not saved.is_file():
            raise SystemExit('First install requires --legacy-config pointing to the original fbdev file')
        if args.enable_glamor:
            driver = Path('/sys/class/drm/renderD128/device/driver')
            if not driver.exists() or driver.resolve().name != 'lima':
                raise SystemExit('Enabling glamor requires the Lima-enabled kernel')
            wrapper = Path('/usr/local/sbin/Xorg-mv100-lima')
            atomic_write(wrapper, XORG_WRAPPER.encode())
            wrapper.chmod(0o755)
            seat = Path('/etc/lightdm/lightdm.conf.d/90-mv100-lima.conf')
            seat.parent.mkdir(parents=True, exist_ok=True)
            atomic_write(seat, b'[Seat:*]\nxserver-command=/usr/local/sbin/Xorg-mv100-lima\n')
            atomic_write(ROOT/'glamor-enabled', b'Accepted MV100 Lima desktop profile\n')
        target = ROOT / 'profile.py'
        if Path(__file__).resolve() != target:
            shutil.copyfile(__file__, target)
            target.chmod(0o644)
        atomic_write(SERVICE, UNIT.encode())
        subprocess.run(['systemctl', 'daemon-reload'], check=True)
        subprocess.run(['systemctl', 'enable', SERVICE.name], check=True)
    if args.action == 'cpu':
        (ROOT/'glamor-enabled').unlink(missing_ok=True)
    select(force_legacy=args.action == 'legacy')


if __name__ == '__main__':
    main()
