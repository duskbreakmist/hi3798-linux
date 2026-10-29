"""Complete one guarded GPU desktop/app validation without operator timing gaps."""
from pathlib import Path
import json
import subprocess
import time

root = Path('/home/ray/hi3798-lima')
def run(args, **kwargs):
    return subprocess.run(args,check=True,**kwargs)

try:
    run(['python3','/run/try-lima-glamor-guarded.py','retry'])
    deadline = time.monotonic()+100
    while True:
        log = Path('/var/log/Xorg.0.log').read_text(errors='replace')
        if ('glamor X acceleration enabled on Mali450' in log and
            'GLX: Initialized DRI2 GL provider' in log and
            subprocess.run(['pgrep','-x','lxqt-session'],stdout=subprocess.DEVNULL).returncode==0):
            break
        if time.monotonic()>deadline:
            raise RuntimeError('GPU Xorg/session initialization did not finish')
        time.sleep(0.5)
    # Renew the rollback window for the remaining single application check.
    run(['systemctl','restart','mv100-glamor-timeout.timer'])
    info=subprocess.check_output(['runuser','-u','ray','--','env','DISPLAY=:0',
        'XAUTHORITY=/home/ray/.Xauthority','MESA_LOADER_DRIVER_OVERRIDE=pl111',
        '/usr/bin/es2_info.arm-linux-gnueabihf'],text=True,timeout=60)
    (root/'es2-info-final.txt').write_text(info)
    if 'GL_RENDERER: Mali450' not in info or 'GL_VENDOR: lima' not in info:
        raise RuntimeError('Native EGL client did not select Mali450')
    run(['python3','/run/capture-es2tri.py'],timeout=85)
    pixel=subprocess.check_output(['python3','/run/verify-direct-kms-window.py'],text=True)
    (root/'glamor-scanout.json').write_text(pixel)
    if not json.loads(pixel)['scanout_updated']:
        raise RuntimeError('Physical scanout did not update')
    dmesg=subprocess.check_output(['dmesg'],text=True)
    (root/'dmesg-after-glamor.txt').write_text(dmesg)
    if any(pattern in dmesg for pattern in ['job timeout','task error','Internal error: Oops','Kernel panic']):
        raise RuntimeError('GPU error found in kernel log')
    run(['python3','/run/mv100-desktop-profile.py','install','--enable-glamor'])
    run(['python3','/root/mv100-glamor-trial/trial.py','accept'])
    (root/'accepted.txt').write_text('Mali450 EGL/GLES + Xorg glamor + physical update accepted\n')
    print('ACCEPTED: native Mesa EGL/GLES renderer Mali450, GPU Xorg, physical scanout',flush=True)
except BaseException:
    subprocess.run(['python3','/root/mv100-glamor-trial/trial.py','rollback'])
    raise
