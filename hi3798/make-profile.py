import os
from pathlib import Path
import re, sys
root=Path(__file__).resolve().parent
profile=sys.argv[1]
if profile not in ('direct','diagnostic','ssd','bootloader'): raise SystemExit('Unknown profile')
values={}
paths=[root/'mv100.config']
if profile in ('direct','ssd'): paths.append(root/'direct-root.config')
if profile=='ssd': paths.append(root/'ssd-root.config')
if profile=='bootloader': paths.append(root/'bootloader-root.config')
for path in paths:
    for line in path.read_text().splitlines():
        match=re.match(r'(CONFIG_\w+)=|# (CONFIG_\w+) is not set',line)
        if match: values[match[1] or match[2]]=line
if profile == 'diagnostic':
    values['CONFIG_CPU_FREQ_DEFAULT_GOV_PERFORMANCE'] = '# CONFIG_CPU_FREQ_DEFAULT_GOV_PERFORMANCE is not set'
    values['CONFIG_CPU_FREQ_DEFAULT_GOV_USERSPACE'] = 'CONFIG_CPU_FREQ_DEFAULT_GOV_USERSPACE=y'
    if os.environ.get('MV100_CPU_EXPERIMENT') == '1':
        values['CONFIG_CMDLINE'] = values['CONFIG_CMDLINE'][:-1] + ' hi3798mv100_cpufreq.allow_overclock=1' + chr(34)
Path(sys.argv[2]).write_text('\n'.join(values.values())+'\n')
