#!/usr/bin/env python3
"""Verify build-profile features, not hardware support or Docker operation."""
import pathlib
import sys
config = {}
for line in pathlib.Path(sys.argv[1]).read_text().splitlines():
    if line.startswith('CONFIG_') and '=' in line:
        key, value = line.split('=', 1)
        config[key] = value
required = {
    'CONFIG_ARCH_MULTI_V7': 'y', 'CONFIG_ARCH_HISI': 'y',
    'CONFIG_SMP': 'y', 'CONFIG_NR_CPUS': '4', 'CONFIG_AEABI': 'y',
    'CONFIG_SERIAL_AMBA_PL011_CONSOLE': 'y',
    'CONFIG_USB_STORAGE': 'y', 'CONFIG_EXT4_FS': 'y',
    'CONFIG_BTRFS_FS': 'y', 'CONFIG_ZSTD_DECOMPRESS': 'y',
    'CONFIG_MEMCG': 'y', 'CONFIG_CGROUP_PIDS': 'y',
    'CONFIG_USER_NS': 'y', 'CONFIG_NET_NS': 'y',
    'CONFIG_SECCOMP_FILTER': 'y', 'CONFIG_CGROUP_BPF': 'y',
    'CONFIG_VETH': 'y', 'CONFIG_BRIDGE': 'y',
    'CONFIG_OVERLAY_FS': 'y', 'CONFIG_DRM_LIMA': 'm',
}
errors = [f'{key}: expected {value}, got {config.get(key)}'
          for key, value in required.items() if config.get(key) != value]
if errors:
    sys.exit('Configuration mismatch:\n' + '\n'.join(errors))
print(f'Configuration checks passed ({len(required)} features); board is NOT validated.')
