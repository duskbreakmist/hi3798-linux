"""Run the packaged GLES2 triangle once, capture its window, then stop it."""
from pathlib import Path
import os
import signal
import subprocess
import time

root = Path('/home/ray/hi3798-lima')
log_path = root/'es2tri-capture.txt'
with log_path.open('wb') as log:
    process = subprocess.Popen(['runuser','-u','ray','--','env','DISPLAY=:0',
        'XAUTHORITY=/home/ray/.Xauthority','MESA_LOADER_DRIVER_OVERRIDE=pl111',
        '/usr/bin/stdbuf','-oL','-eL','/usr/bin/es2tri.arm-linux-gnueabihf'],
        stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
    try:
        deadline = time.monotonic()+75
        while 'Attrib color' not in log_path.read_text(errors='replace'):
            if process.poll() is not None:
                raise SystemExit('es2tri exited before rendering: '+log_path.read_text())
            if time.monotonic()>deadline:
                raise SystemExit('Timed out waiting for es2tri initialization')
            time.sleep(0.25)
        time.sleep(1)
        environment = dict(os.environ,DISPLAY=':0',XAUTHORITY='/var/run/lightdm/root/:0')
        tree = subprocess.check_output(['xwininfo','-root','-tree'],env=environment,text=True)
        (root/'es2tri-windows.txt').write_text(tree)
        subprocess.run(['xwd','-root','-screen','-silent','-out',str(root/'es2tri-desktop.xwd')],
                       env=environment,check=True)
        print('Captured the initialized GLES2 triangle window',flush=True)
    finally:
        if process.poll() is None:
            os.killpg(process.pid,signal.SIGTERM)
            process.wait(timeout=5)
