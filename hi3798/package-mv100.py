#!/usr/bin/env python3
"""Create CRC-checked legacy uImages; no flash or environment operations."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import zlib

out = Path(sys.argv[1]).resolve()
zimage = (out / 'arch/arm/boot/zImage').read_bytes()
assert len(zimage) % 4 == 0
records = []
for variant in ('codex', 'codex-io', 'codex-display', 'codex-lima'):
    dtb = (out / f'arch/arm/boot/dts/hisilicon/hi3798mv100-{variant}.dtb').read_bytes()
    assert struct.unpack('>I', dtb[:4])[0] == 0xd00dfeed
    payload = zimage + dtb
    name = f'MV100 6.12.114 {variant} EXP'.encode()[:32].ljust(32, b'\0')
    fields = [0x27051956, 0, 1793059200, len(payload), 0x02000000,
              0x02000000, zlib.crc32(payload), 5, 2, 2, 0, name]
    fmt = '>7I4B32s'
    fields[1] = zlib.crc32(struct.pack(fmt, *fields))
    image = struct.pack(fmt, *fields) + payload
    decoded = list(struct.unpack(fmt, image[:64]))
    header_crc = decoded[1]
    decoded[1] = 0
    assert zlib.crc32(struct.pack(fmt, *decoded)) == header_crc
    assert zlib.crc32(image[64:]) == fields[6]
    path = out / f'uImage-mv100-{variant}'
    path.write_bytes(image)
    records.append({'path': path.name, 'bytes': len(image),
                    'sha256': hashlib.sha256(image).hexdigest(),
                    'dtb_variant': variant, 'header_crc': header_crc,
                    'data_crc': fields[6], 'load': '0x02000000',
                    'entry': '0x02000000', 'validated_on_board': False})
(out / 'candidate-images.json').write_text(json.dumps(records, indent=2) + '\n')
print(json.dumps(records, indent=2))
