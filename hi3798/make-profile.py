from pathlib import Path
import re, sys
root=Path(__file__).resolve().parent
profile=sys.argv[1]
if profile not in ('direct','diagnostic','ssd'): raise SystemExit('Unknown profile')
values={}
paths=[root/'mv100.config']
if profile in ('direct','ssd'): paths.append(root/'direct-root.config')
if profile=='ssd': paths.append(root/'ssd-root.config')
for path in paths:
    for line in path.read_text().splitlines():
        match=re.match(r'(CONFIG_\w+)=|# (CONFIG_\w+) is not set',line)
        if match: values[match[1] or match[2]]=line
Path(sys.argv[2]).write_text('\n'.join(values.values())+'\n')
