#!/usr/bin/env python3
"""Produce a vendor configuration comparison without blocking SDK-free builds."""
from pathlib import Path
import json
import os
import subprocess
import sys

here = Path(__file__).resolve().parent
src = here.parent
config = Path(sys.argv[1]).resolve()
sdk = Path(os.environ.get('MV100_SDK_ROOT', str(src.parent/'HiSTBLinux')))
old = Path(os.environ.get('MV100_OLD_CONFIG', str(sdk/'out/hi3798mv100/hi3798mv100_out/obj/source/kernel/linux-4.4.y/.config')))
if not old.is_file():
    print('Vendor comparison skipped: set MV100_OLD_CONFIG; required framework checks remain enforced.')
    raise SystemExit(0)
report = config.parent/'config-migration-audit'
subprocess.run([sys.executable, str(here/'config-audit.py'), '--old', str(old), '--new', str(config), '--tree', str(src), '--out', str(report)], check=True)
import csv
count = sum(1 for _ in csv.DictReader((report/'gap-still-exists.csv').open()))
baseline = report/'previous-gap.json'
previous = json.loads(baseline.read_text())['candidate_gap'] if baseline.exists() else 984
if count > previous:
    raise SystemExit(f'Migration candidate gap increased: {previous} -> {count}. Review intended removals before updating {baseline}.')
baseline.write_text(json.dumps({'candidate_gap': count}, indent=2)+'\n')
print(f'Migration candidate gap: {previous} -> {count}; unrelated hardware remains intentionally excluded.')
