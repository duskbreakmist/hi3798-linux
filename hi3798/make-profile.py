import argparse
import os
from pathlib import Path
import re

root = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description="Merge MV100 profile and saved menuconfig overrides")
parser.add_argument('profile', choices=['direct', 'diagnostic', 'ssd', 'bootloader'])
parser.add_argument('output', type=Path)
group = parser.add_mutually_exclusive_group()
group.add_argument('--without-user', action='store_true')
group.add_argument('--user-config', type=Path)
args = parser.parse_args()
profile = args.profile
values = {}
paths = [root/'mv100.config']
if profile in ('direct', 'ssd'): paths.append(root/'direct-root.config')
if profile == 'ssd': paths.append(root/'ssd-root.config')
if profile == 'bootloader': paths.append(root/'bootloader-root.config')
for path in paths:
    for line in path.read_text().splitlines():
        match = re.match(r'(CONFIG_\w+)=|# (CONFIG_\w+) is not set', line)
        if match: values[match[1] or match[2]] = line
if profile == 'diagnostic':
    values['CONFIG_CPU_FREQ_DEFAULT_GOV_PERFORMANCE'] = '# CONFIG_CPU_FREQ_DEFAULT_GOV_PERFORMANCE is not set'
    values['CONFIG_CPU_FREQ_DEFAULT_GOV_USERSPACE'] = 'CONFIG_CPU_FREQ_DEFAULT_GOV_USERSPACE=y'
    if os.environ.get('MV100_CPU_EXPERIMENT') == '1':
        values['CONFIG_CMDLINE'] = values['CONFIG_CMDLINE'][:-1] + ' hi3798mv100_cpufreq.allow_overclock=1' + chr(34)
user = args.user_config or root/'configs'/(profile + '.override.config')
if not args.without_user and (args.user_config or user.exists()):
    for line in user.read_text().splitlines():
        match = re.match(r'(CONFIG_\w+)=|# (CONFIG_\w+) is not set', line)
        if match: values[match[1] or match[2]] = line
args.output.write_text('\n'.join(values.values()) + '\n')
