#!/usr/bin/env python3
"""Check the candidate's actual configuration, including forced RAM boot."""
import pathlib
import subprocess
import sys
config = pathlib.Path(sys.argv[1])
subprocess.run([sys.executable, str(pathlib.Path(__file__).with_name('verify-config.py')), str(config)], check=True)
values = dict(line.split('=', 1) for line in config.read_text().splitlines() if line.startswith('CONFIG_') and '=' in line)
required = ['CONFIG_MACH_HI3798MV100', 'CONFIG_COMMON_CLK_HI3798MV100',
            'CONFIG_ARM_APPENDED_DTB', 'CONFIG_ARM_THUMB',
            'CONFIG_HISI_FEMAC', 'CONFIG_MDIO_HISI_FEMAC', 'CONFIG_MMC_BLOCK',
            'CONFIG_MMC_DW_HI3798CV200', 'CONFIG_PHY_HISI_INNO_USB2',
            'CONFIG_ARM_TIMER_SP804', 'CONFIG_GENERIC_CLOCKEVENTS_BROADCAST']
for key in required:
    assert values.get(key) == 'y', (key, values.get(key))
for key in ['CONFIG_HOTPLUG_CPU', 'CONFIG_CPU_ICACHE_DISABLE', 'CONFIG_CPU_DCACHE_DISABLE', 'CONFIG_ARM_LPAE']:
    assert values.get(key, 'n') == 'n', (key, values.get(key))
for key in ['CONFIG_ARM_THUMB','CONFIG_POSIX_TIMERS','CONFIG_COMPAT_32BIT_TIME',
            'CONFIG_PROC_SYSCTL','CONFIG_BINFMT_SCRIPT','CONFIG_SHMEM']:
    if values.get(key) != 'y': raise SystemExit('Required userspace feature missing: '+key)
assert values.get('CONFIG_CMDLINE_FORCE')=='y' or values.get('CONFIG_CMDLINE_EXTEND')=='y'
cmd=values['CONFIG_CMDLINE']
assert 'mem=1G' in cmd and 'panic=0' in cmd
assert 'maxcpus=' not in cmd
if 'root=/dev/ram0' in cmd:
    assert 'rdinit=/init' in cmd
    assert pathlib.Path(values['CONFIG_INITRAMFS_SOURCE'].strip('"')).is_file()
elif 'root=/dev/sda4' in cmd or 'root=/dev/mmcblk0p4' in cmd or values.get('CONFIG_CMDLINE_EXTEND') == 'y':
    assert 'rootwait' in cmd and 'rdinit=' not in cmd
    assert values.get('CONFIG_INITRAMFS_SOURCE') == '""'
    assert values.get('CONFIG_CMDLINE_PARTITION') == 'y'
else:
    raise SystemExit('Unrecognized rootfs profile')
print('MV100 configuration passed; hardware results require separate tests.')
