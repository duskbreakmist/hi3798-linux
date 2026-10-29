"""Apply the compositor setting to the actual user's session bus."""
from pathlib import Path
import os, subprocess, json
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
    command = ['xfconf-query','-c','xfwm4','-p','/general/use_compositing']
    print('Before:',subprocess.check_output(prefix+command,text=True).strip())
    subprocess.run(prefix+command+['-s','false'],check=True)
    print('After:',subprocess.check_output(prefix+command,text=True).strip())
    break
else:
    raise SystemExit('No running xfwm4 session')
