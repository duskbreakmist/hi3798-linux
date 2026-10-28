#!/usr/bin/env python3
"""Audit built FDTs against the actual candidate's drivers and boot policy."""
from pathlib import Path
import json
import re
import struct
import subprocess
import sys

src = Path(__file__).resolve().parent.parent
out = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else src.parent / 'hi3798-linux-build/6.12-mv100'

def fdt(path):
    data = path.read_bytes()
    header = struct.unpack_from('>10I', data)
    magic, total, structure, strings = header[:4]
    assert magic == 0xd00dfeed and total == len(data)
    tree, stack = {}, []
    pos = structure
    while True:
        token, = struct.unpack_from('>I', data, pos)
        pos += 4
        if token == 1:
            end = data.index(0, pos)
            name = data[pos:end].decode()
            stack.append(name)
            pos = (end + 4) & ~3
            tree['/'.join(stack) or '/'] = {}
        elif token == 2:
            stack.pop()
        elif token == 3:
            size, offset = struct.unpack_from('>II', data, pos)
            pos += 8
            end = data.index(0, strings + offset)
            name = data[strings + offset:end].decode()
            tree['/'.join(stack) or '/'][name] = data[pos:pos+size]
            pos = (pos + size + 3) & ~3
        elif token == 4:
            pass
        elif token == 9:
            break
        else:
            raise AssertionError((path, token, pos))
    return tree

def cells(data):
    assert len(data) % 4 == 0
    return list(struct.unpack('>' + 'I' * (len(data)//4), data))

def strings(data):
    return data.rstrip(b'\0').decode().split('\0')

clock_source = (src / 'drivers/clk/hisilicon/crg-hi3798mv100.c').read_text()
tables = re.findall(r'static (?:const )?struct hisi_(?:fixed_rate|mux|phase|gate)_clock (\w+)\[\] = \{(.*?)\n\};', clock_source, re.S)
ids = {'crg': set(), 'sysctrl': set()}
for name, body in tables:
    symbols = re.findall(r'\{\s*([A-Z][A-Z0-9_]+),', body)
    if not symbols:
        continue
    definitions = '\n'.join(re.findall(r'^#define HI3798MV100_.*$', clock_source, re.M))
    source = '#include <dt-bindings/clock/hi3798mv100-crg.h>\n' + definitions + '\n' + '\n'.join(f'TOKEN_{i} {s}' for i, s in enumerate(symbols))
    expanded = subprocess.check_output(['gcc', '-E', '-P', '-x', 'c', '-I', str(src / 'include'), '-'], input=source, text=True)
    values = {int(v, 0) for v in re.findall(r'TOKEN_\d+\s+(\S+)', expanded)}
    ids['sysctrl' if 'sysctrl' in name else 'crg'].update(values)

records = []
for variant in ('codex', 'codex-io'):
    tree = fdt(out / f'arch/arm/boot/dts/hisilicon/hi3798mv100-{variant}.dtb')
    assert strings(tree['/']['compatible']) == ['hisilicon,hi3798mv100']
    assert cells(tree['/memory@0']['reg']) == [0, 0x40000000]
    assert len([p for p in tree if p.startswith('/cpus/cpu@')]) == 4
    assert strings(tree['/chosen']['stdout-path']) == ['serial0:115200n8']
    assert cells(tree['/soc/timer@f8a2a000']['interrupts']) == [0, 26, 4]
    assert cells(tree['/clock-xtal']['clock-frequency']) == [24000000]
    assert not any('display@' in p for p in tree)
    gpu = tree['/soc/gpu@f9200000']
    assert strings(gpu['compatible']) == ['hisilicon,hi3798mv100-mali', 'arm,mali-450']
    assert cells(gpu['reg']) == [0xf9200000, 0x30000]
    assert cells(gpu['interrupts']) == [0,94,4]*8
    assert strings(gpu['interrupt-names']) == ['gp','gpmmu','pp0','ppmmu0','pp1','ppmmu1','pmu','pp']
    assert strings(gpu['status']) == ['disabled']
    phandles = {cells(node['phandle'])[0]: node for node in tree.values() if 'phandle' in node}
    clock_references, reset_references = 0, 0
    for name, node in tree.items():
        refs = cells(node.get('clocks', b''))
        while refs:
            provider = phandles[refs.pop(0)]
            count = cells(provider['#clock-cells'])[0]
            args, refs = refs[:count], refs[count:]
            assert len(args) == count
            if count:
                namespace = 'sysctrl' if 'hisilicon,hi3798mv100-sysctrl' in strings(provider['compatible']) else 'crg'
                assert args[0] in ids[namespace], (name, namespace, args)
            clock_references += 1
        refs = cells(node.get('resets', b''))
        while refs:
            provider = phandles[refs.pop(0)]
            assert cells(provider['#reset-cells']) == [2]
            offset, bit, *refs = refs
            assert offset % 4 == 0 and offset + 4 <= cells(provider['reg'])[1] and bit < 32
            reset_references += 1
    for name in ('usb@f9890000', 'usb@f9930000', 'mmc@f9830000', 'mdio@f9841100', 'ethernet@f9840000'):
        assert strings(tree['/soc/' + name]['status']) == ['okay' if variant == 'codex-io' else 'disabled']
    assert cells(tree['/soc/mmc@f9830000']['bus-width']) == [8]
    assert cells(tree['/soc/mmc@f9830000']['max-frequency']) == [50000000]
    assert cells(tree['/soc/mdio@f9841100/ethernet-phy@1']['reg']) == [1]
    for name in ('usb-phy@124', 'usb-phy@158'):
        assert strings(tree['/soc/peripheral-controller@f8a20000/' + name]['compatible']) == ['hisilicon,hi3798mv100-usb2-phy']
    records.append({'variant': variant, 'nodes': len(tree), 'clock_references_checked': clock_references,
                    'reset_references_checked': reset_references, 'binary_dtb_checks': 'passed'})

assert subprocess.check_output(['git', '-C', str(src), 'diff', 'upstream/linux-6.12.y', '--', 'drivers/clk/hisilicon/crg-hi3798cv200.c']) == b''
(out / 'dtb-verification.json').write_text(json.dumps(records, indent=2) + '\n')
print(json.dumps(records, indent=2))
