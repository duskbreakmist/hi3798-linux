"""Install isolated armhf Mesa smoke-test tools without replacing graphics libs."""
from pathlib import Path
import shutil
import subprocess

source=Path(__file__).resolve().parent
targets=[('lima-render-image',Path('/usr/local/libexec/mv100/lima-render-image')),
         ('mv100-gles2-test',Path('/usr/local/bin/mv100-gles2-test')),
         ('mv100-mesa-run',Path('/usr/local/bin/mv100-mesa-run'))]
for name,target in targets:
    if not (source/name).is_file():
        raise SystemExit('Missing deployment input: '+str(source/name))
for name,target in targets:
    target.parent.mkdir(parents=True,exist_ok=True)
    temporary=target.with_name(target.name+'.new')
    shutil.copyfile(source/name,temporary)
    temporary.chmod(0o755)
    temporary.replace(target)
    print('Installed',target)
subprocess.run(['usermod','-a','-G','render,video','ray'],check=True)
print('ray is in render/video; new login sessions inherit these groups')
