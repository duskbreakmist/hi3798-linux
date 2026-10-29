"""Inspect or explicitly adjust the actual user's compositor session."""
from pathlib import Path
import argparse, subprocess, json
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--compositing', choices=['keep','on','off'], default='keep')
p.add_argument('--vblank-mode', choices=['keep','auto','xpresent','glx','off'], default='keep')
args = p.parse_args()
for directory in Path('/proc').iterdir():
    if not directory.name.isdigit():
        continue
    try:
        if (directory/'comm').read_text().strip() != 'xfwm4':
            continue
        values = dict(item.split(b'=',1) for item in (directory/'environ').read_bytes().split(b'\0') if b'=' in item)
    except (FileNotFoundError, PermissionError, ProcessLookupError):
        continue
    keys = [b'DBUS_SESSION_BUS_ADDRESS',b'DISPLAY',b'XAUTHORITY',b'XDG_RUNTIME_DIR']
    selected = {k.decode():values[k].decode() for k in keys if k in values}
    selected.setdefault('DISPLAY',':0')
    selected.setdefault('XAUTHORITY','/home/ray/.Xauthority')
    selected.setdefault('XDG_RUNTIME_DIR','/run/user/1000')
    print(json.dumps(selected))
    prefix = ['runuser','-u','ray','--','env']+[k+'='+v for k,v in selected.items()]
    def query(name, value=None):
        command = prefix+['xfconf-query','-c','xfwm4','-p','/general/'+name]
        if value is not None:
            subprocess.run(command+['-s',value],check=True)
        return subprocess.check_output(command,text=True).strip()
    names=['use_compositing','vblank_mode']
    print('Before:',json.dumps({name:query(name) for name in names}))
    if args.vblank_mode != 'keep':
        query('vblank_mode', args.vblank_mode)
    if args.compositing != 'keep':
        query('use_compositing', 'true' if args.compositing=='on' else 'false')
    print('After:',json.dumps({name:query(name) for name in names}))
    break
else:
    raise SystemExit('No running xfwm4 session')
