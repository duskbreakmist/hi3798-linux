#!/usr/bin/env python3
"""Capture the delta between a menuconfig session and the clean baseline.

`mv100.config` is consumed as KCONFIG_ALLCONFIG on top of allnoconfig, so any
change made interactively in menuconfig is lost the next time `configure` runs
unless it is copied back into that file. This script diffs the baseline
(.config.baseline, saved right after `configure`) against the post-menuconfig
.config and emits only the symbols whose value actually changed, skipping the
ones mv100.config already pins.

Usage:
    python3 hi3798/capture-config-delta.py            # write mv100-delta.config
    python3 hi3798/capture-config-delta.py --stdout   # print instead
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
BUILD = HERE.parent.parent / "hi3798-linux-build" / "6.12-mv100"
CONFIG = BUILD / ".config"
BASELINE = BUILD / ".config.baseline"
CURATED = HERE / "mv100.config"
OUTPUT = HERE / "mv100-delta.config"

# Matches either "CONFIG_FOO=value" or "# CONFIG_FOO is not set".
LINE = re.compile(r"^(?:CONFIG_([A-Za-z0-9_]+)=.*|# CONFIG_([A-Za-z0-9_]+) is not set)$")


def load(path: pathlib.Path) -> dict[str, str]:
    """Return {symbol: raw line} for every symbol mentioned in a .config."""
    if not path.is_file():
        sys.exit(f"missing {path}; run ./hi3798/build-mv100.sh configure first")
    values: dict[str, str] = {}
    for line in path.read_text(errors="replace").splitlines():
        match = LINE.match(line.strip())
        if match:
            values[match.group(1) or match.group(2)] = line.strip()
    return values


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--stdout", action="store_true", help="print instead of writing a file")
    args = parser.parse_args()

    baseline = load(BASELINE)
    current = load(CONFIG)
    curated = load(CURATED)

    changed = {
        symbol: line
        for symbol, line in current.items()
        if baseline.get(symbol) != line and symbol not in curated
    }
    reverted = {
        symbol: line
        for symbol, line in baseline.items()
        if symbol in current and current[symbol] != line and symbol in curated
    }

    header = [
        "# Delta captured from an interactive menuconfig session.",
        "# Append the useful parts to mv100.config, then re-run",
        "#   ./hi3798/build-mv100.sh configure",
        "# to confirm the values survive allnoconfig.",
        f"# changed symbols      : {len(changed)}",
        f"# conflicts with mv100.config (NOT applied, review manually): {len(reverted)}",
        "",
    ]

    body = [f"{line}" for _, line in sorted(changed.items())]

    if reverted:
        body.extend(["", "# ---- below: menuconfig disagreed with mv100.config ----"])
        body.extend(f"# {line}" for _, line in sorted(reverted.items()))

    text = "\n".join(header + body) + "\n"

    if args.stdout:
        print(text, end="")
    else:
        OUTPUT.write_text(text)
        print(f"wrote {OUTPUT} ({len(changed)} changed, {len(reverted)} conflicts)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
