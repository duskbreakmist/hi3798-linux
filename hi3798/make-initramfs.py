#!/usr/bin/env python3
"""Build a static ARM diagnostic initramfs without root or host mknod."""
from pathlib import Path
import argparse
import os
import subprocess

p = argparse.ArgumentParser()
p.add_argument('output', type=Path)
args = p.parse_args()
src = Path(__file__).resolve().parent.parent
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
cross = os.environ.get('CROSS_COMPILE', 'arm-linux-gnueabihf-')
subprocess.run([cross + 'gcc', '-static', '-O2', '-Wall', '-Wextra',
                '-o', str(out / 'probe-init'), str(src / 'hi3798/probe-init.c')], check=True)
subprocess.run([os.environ.get('HOSTCC', 'gcc'), '-O2', '-o', str(out / 'gen_init_cpio'),
                str(src / 'usr/gen_init_cpio.c')], check=True)
listing = out / 'initramfs.list'
listing.write_text('dir /dev 0755 0 0\ndir /proc 0555 0 0\ndir /sys 0555 0 0\n'
                   'nod /dev/console 0600 0 0 c 5 1\nnod /dev/null 0666 0 0 c 1 3\n'
                   f'file /init {out / "probe-init"} 0755 0 0\n')
display_module = os.environ.get('MV100_DISPLAY_MODULE')
if display_module:
    module_path = Path(display_module).resolve()
    assert module_path.is_file()
    with listing.open('a') as handle:
        handle.write(f'file /display.ko {module_path} 0600 0 0\n')
with (out / 'probe.cpio').open('wb') as stream:
    subprocess.run([str(out / 'gen_init_cpio'), '-t', '1793059200', str(listing)], stdout=stream, check=True)
print(out / 'probe.cpio')
