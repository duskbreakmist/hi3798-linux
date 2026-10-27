#!/usr/bin/env python3
"""Check the candidate's actual configuration, including forced RAM boot."""
import pathlib
import subprocess
import sys
config = pathlib.Path(sys.argv[1])
subprocess.run([sys.executable, str(pathlib.Path(__file__).with_name('verify-config.py')), str(config)], check=True)
values = dict(line.split('=', 1) for line in config.read_text().splitlines() if line.startswith('CONFIG_') and '=' in line)
required = ['CONFIG_MACH_HI3798MV100', 'CONFIG_COMMON_CLK_HI3798MV100',
            'CONFIG_ARM_APPENDED_DTB', 'CONFIG_CMDLINE_FORCE',
            'CONFIG_HISI_FEMAC', 'CONFIG_MDIO_HISI_FEMAC', 'CONFIG_MMC_BLOCK',
            'CONFIG_MMC_DW_HI3798CV200', 'CONFIG_PHY_HISI_INNO_USB2',
            'CONFIG_ARM_TIMER_SP804', 'CONFIG_GENERIC_CLOCKEVENTS_BROADCAST']
for key in required:
    assert values.get(key) == 'y', (key, values.get(key))
for key in ['CONFIG_HOTPLUG_CPU', 'CONFIG_CPU_ICACHE_DISABLE', 'CONFIG_CPU_DCACHE_DISABLE', 'CONFIG_ARM_LPAE']:
    assert values.get(key, 'n') == 'n', (key, values.get(key))
for argument in ['mem=512M', 'root=/dev/ram0', 'rdinit=/init', 'maxcpus=1', 'panic=0']:
    assert argument in values['CONFIG_CMDLINE'], argument
assert 'root=/dev/sd' not in values['CONFIG_CMDLINE']
assert 'root=/dev/mmc' not in values['CONFIG_CMDLINE']
assert pathlib.Path(values['CONFIG_INITRAMFS_SOURCE'].strip('"')).is_file()
print('MV100 offline configuration checks passed; no hardware result implied.')
