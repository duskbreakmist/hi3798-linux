"""Try compositing on the actual ray session bus, preserving original values."""
from pathlib import Path
import json
import subprocess
import sys

state=Path('/root/mv100-compositor-trial')
prefix=['runuser','-u','ray','--','env','DISPLAY=:0',
    'XAUTHORITY=/home/ray/.Xauthority','XDG_RUNTIME_DIR=/run/user/1000',
    'DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus','xfconf-query','-c','xfwm4']
def get(name):
    return subprocess.check_output(prefix+['-p','/general/'+name],text=True).strip()
def set_value(name,value):
    subprocess.run(prefix+['-p','/general/'+name,'-s',value],check=True)

action=sys.argv[1] if len(sys.argv)>1 else 'try'
if action=='rollback':
    if (state/'pending').exists():
        original=json.loads((state/'original.json').read_text())
        set_value('use_compositing','false')
        set_value('vblank_mode',original['vblank_mode'])
        set_value('use_compositing',original['use_compositing'])
        (state/'pending').unlink(missing_ok=True)
        print('Restored compositor settings',flush=True)
    raise SystemExit(0)
if action=='accept':
    if get('use_compositing')!='true':
        raise SystemExit('Trial already rolled back; reapply settings before accepting')
    subprocess.run(['systemctl','stop','mv100-compositor-timeout.timer'],check=False)
    (state/'pending').unlink(missing_ok=True)
    print('Kept compositor settings',flush=True)
    raise SystemExit(0)
if state.exists():
    raise SystemExit('Existing compositor trial; inspect it before another trial')
state.mkdir()
original={name:get(name) for name in ['use_compositing','vblank_mode']}
(state/'original.json').write_text(json.dumps(original,indent=2))
(state/'pending').write_text('Awaiting physical display feedback\n')
script=state/'trial.py'
script.write_bytes(Path(__file__).read_bytes())
subprocess.run(['systemd-run','--unit=mv100-compositor-timeout','--on-active=180s',
    '/usr/bin/python3',str(script),'rollback'],check=True)
set_value('vblank_mode','xpresent')
set_value('use_compositing','true')
print(json.dumps({'before':original,'after':{name:get(name) for name in original}}),flush=True)
