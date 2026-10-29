"""One controlled Xorg/Lima trial with timeout and persistent boot rollback."""
from pathlib import Path
import shutil
import subprocess
import sys

root = Path('/root/mv100-glamor-trial')
files = [(Path('/etc/X11/xorg.conf.d/10-fbdev.conf'), 'xorg'),
         (Path('/etc/lightdm/lightdm.conf.d/90-mv100-lima.conf'), 'seat'),
         (Path('/usr/local/sbin/Xorg-mv100-lima'), 'wrapper')]
if len(sys.argv) > 1 and sys.argv[1] in ('rollback', 'boot-rollback', 'accept'):
    action = sys.argv[1]
    if action == 'accept':
        (root/'pending').unlink(missing_ok=True)
        subprocess.run(['systemctl','stop','mv100-glamor-timeout.timer'],check=True)
        subprocess.run(['systemctl','disable','mv100-glamor-rollback.service'],check=True)
        print('Trial accepted; integrate boot selection before rebooting')
        raise SystemExit(0)
    if not (root/'pending').exists():
        raise SystemExit(0)
    Path('/usr/local/share/mv100-desktop/glamor-enabled').unlink(missing_ok=True)
    for target, name in files:
        if (root/name).exists():
            shutil.copyfile(root/name,target)
        else:
            target.unlink(missing_ok=True)
    (root/'pending').unlink(missing_ok=True)
    if action == 'rollback':
        subprocess.run(['systemctl','restart','lightdm'],check=True)
    print('Restored software desktop configuration')
    raise SystemExit(0)
retry = len(sys.argv) > 1 and sys.argv[1] == 'retry'
if root.exists() and not retry:
    raise SystemExit('Existing trial directory: inspect before starting another trial')
if retry:
    if (root/'pending').exists() or not (root/'xorg').is_file():
        raise SystemExit('Retry requires a rolled-back prior trial and original backup')
else:
    root.mkdir()
    for target, name in files:
        if target.exists():
            shutil.copyfile(target,root/name)
shutil.copyfile(__file__,root/'trial.py')
(root/'pending').write_text('Unconfirmed GPU desktop trial\n')
Path('/etc/systemd/system/mv100-glamor-rollback.service').write_text('''[Unit]
Description=Restore unconfirmed Lima desktop trial after reboot
After=local-fs.target
Before=mv100-desktop-profile.service lightdm.service display-manager.service
[Service]
Type=oneshot
ExecStart=/usr/bin/python3 /root/mv100-glamor-trial/trial.py boot-rollback
[Install]
WantedBy=graphical.target
''')
subprocess.run(['systemctl','daemon-reload'],check=True)
subprocess.run(['systemctl','enable','mv100-glamor-rollback.service'],check=True)
files[0][0].write_text('''Section "Device"
    Identifier "MV100 Lima KMS"
    Driver "modesetting"
    Option "kmsdev" "/dev/dri/card0"
    Option "AccelMethod" "glamor"
    Option "SWcursor" "true"
EndSection
''')
files[1][0].parent.mkdir(exist_ok=True)
files[1][0].write_text('[Seat:*]\nxserver-command=/usr/local/sbin/Xorg-mv100-lima\n')
files[2][0].write_text('''#!/bin/sh
# Process-local KMSRO experiment; not a global Mesa replacement.
exec env MESA_LOADER_DRIVER_OVERRIDE=pl111 /usr/lib/xorg/Xorg "$@"
''')
files[2][0].chmod(0o755)
subprocess.run(['systemd-run','--unit=mv100-glamor-timeout','--on-active=180s',
    '/usr/bin/python3',str(root/'trial.py'),'rollback'],check=True)
subprocess.run(['systemctl','restart','lightdm'],check=True)
print('GPU desktop trial started, timed and boot rollback armed')
