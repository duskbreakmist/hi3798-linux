#!/usr/bin/env python3
"""Compare the vendor 4.4 config against the running 6.12 config.

The point is to find what the 4.4 kernel had enabled that the 6.12 kernel does
not, and to separate two very different cases:

  * the symbol still exists upstream -> a real migration gap we can act on
  * the symbol was removed/renamed upstream -> nothing to port, informational

Read-only: writes only CSV/TXT reports under --out.
"""

from __future__ import annotations

import argparse
import collections
import csv
import re
from pathlib import Path

CONFIG_RE = re.compile(r"^(CONFIG_[A-Za-z0-9_]+)=(.*)$")
UNSET_RE = re.compile(r"^# (CONFIG_[A-Za-z0-9_]+) is not set$")
KCONFIG_RE = re.compile(r"^\s*(?:menu)?config\s+([A-Za-z0-9_]+)\s*$")

# Prefix -> 影响面，仅用于把清单按子系统归类，便于逐个判断。
CATEGORIES = [
    ("声音", ("CONFIG_SOUND", "CONFIG_SND")),
    ("温度/热", ("CONFIG_THERMAL", "CONFIG_HWMON", "CONFIG_CPU_THERMAL")),
    ("调频调压", ("CONFIG_CPU_FREQ", "CONFIG_PM_DEVFREQ", "CONFIG_PM_OPP", "CONFIG_REGULATOR")),
    ("视频/媒体", ("CONFIG_VIDEO", "CONFIG_MEDIA", "CONFIG_DVB", "CONFIG_V4L", "CONFIG_VIDEOBUF")),
    ("GPU/DRM", ("CONFIG_DRM", "CONFIG_FB")),
    ("存储/文件系统", ("CONFIG_MMC", "CONFIG_BLK", "CONFIG_EXT4", "CONFIG_F2FS", "CONFIG_BTRFS",
                   "CONFIG_SQUASHFS", "CONFIG_EXFAT", "CONFIG_NTFS", "CONFIG_SDIO")),
    ("网络", ("CONFIG_NET", "CONFIG_IP", "CONFIG_BRIDGE", "CONFIG_VETH", "CONFIG_WIREGUARD",
            "CONFIG_NETFILTER", "CONFIG_BT", "CONFIG_WLAN", "CONFIG_CFG80211", "CONFIG_MAC80211")),
    ("USB", ("CONFIG_USB",)),
    ("GPIO/I2C/SPI/PWM", ("CONFIG_GPIO", "CONFIG_I2C", "CONFIG_SPI", "CONFIG_PWM", "CONFIG_PINCTRL")),
    ("RTC/看门狗", ("CONFIG_RTC", "CONFIG_WATCHDOG")),
    ("加密/安全", ("CONFIG_CRYPTO", "CONFIG_SECURITY", "CONFIG_KEYS", "CONFIG_HARDENED")),
    ("调试/可观测", ("CONFIG_DEBUG_FS", "CONFIG_FTRACE", "CONFIG_KPROBES", "CONFIG_MAGIC_SYSRQ",
                 "CONFIG_DYNAMIC_DEBUG", "CONFIG_PERF_EVENTS")),
]


def parse_config(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in path.read_text(errors="replace").splitlines():
        match = CONFIG_RE.match(line)
        if match:
            values[match.group(1)] = match.group(2)
            continue
        match = UNSET_RE.match(line)
        if match:
            values[match.group(1)] = "n"
    return values


def kconfig_symbols(tree: Path) -> dict[str, str]:
    """Map every symbol the 6.12 tree still knows about to its defining file."""
    symbols: dict[str, str] = {}
    for kconfig in tree.rglob("Kconfig*"):
        text = str(kconfig)
        if "/.git/" in text:
            continue
        try:
            for line in kconfig.read_text(errors="replace").splitlines():
                match = KCONFIG_RE.match(line)
                if match and match.group(1) not in symbols:
                    symbols[match.group(1)] = str(kconfig.relative_to(tree))
        except OSError:
            continue
    return symbols


def categorize(symbol: str) -> str:
    for name, prefixes in CATEGORIES:
        if symbol.startswith(prefixes):
            return name
    return "其他"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--old", required=True, type=Path)
    parser.add_argument("--new", required=True, type=Path)
    parser.add_argument("--tree", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()

    old = parse_config(args.old)
    new = parse_config(args.new)
    known = kconfig_symbols(args.tree)
    args.out.mkdir(parents=True, exist_ok=True)

    old_on = {s for s, v in old.items() if v != "n"}
    new_on = {s for s, v in new.items() if v != "n"}

    missing = sorted(old_on - new_on)
    gap = [s for s in missing if s.replace("CONFIG_", "") in known]
    gone = [s for s in missing if s.replace("CONFIG_", "") not in known]
    added = sorted(new_on - old_on)
    changed = sorted(s for s in old_on & new_on if old[s] != new[s])

    def write(name: str, header: list[str], rows: list[list[str]]) -> None:
        with (args.out / name).open("w", newline="", encoding="utf-8") as handle:
            writer = csv.writer(handle)
            writer.writerow(header)
            writer.writerows(rows)

    write("gap-still-exists.csv", ["symbol", "old4.4", "kconfig-file", "category"],
          [[s, old[s], known[s.replace("CONFIG_", "")], categorize(s)] for s in gap])
    write("gone-upstream.csv", ["symbol", "old4.4", "category"],
          [[s, old[s], categorize(s)] for s in gone])
    write("changed-value.csv", ["symbol", "old4.4", "new6.12", "category"],
          [[s, old[s], new[s], categorize(s)] for s in changed])
    write("added-in-6.12.csv", ["symbol", "new6.12", "category"],
          [[s, new[s], categorize(s)] for s in added])

    per_category: dict[str, int] = collections.Counter(categorize(s) for s in gap)
    lines = [
        "4.4 启用符号总数: %d" % len(old_on),
        "6.12 启用符号总数: %d" % len(new_on),
        "4.4 有而 6.12 没有: %d" % len(missing),
        "  └ 6.12 仍存在该符号（真正的漏迁）: %d" % len(gap),
        "  └ 6.12 已无此符号（上游删除/改名）: %d" % len(gone),
        "两边都有但取值不同: %d" % len(changed),
        "6.12 新增: %d" % len(added),
        "",
        "漏迁项按子系统分布:",
    ]
    for name, count in per_category.most_common():
        lines.append("  %-14s %d" % (name, count))
    summary = "\n".join(lines)
    (args.out / "summary.txt").write_text(summary + "\n", encoding="utf-8")
    print(summary)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
