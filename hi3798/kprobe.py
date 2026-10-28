#!/usr/bin/env python3
"""Non-interactive replacement for menuconfig's search and dependency resolver.

menuconfig is a TUI, so an agent cannot drive it. But everything it displays
comes from two places that are both reachable from a script:

  * the dependency text  -> `depends on`, `select`, `if`, `menu ... depends on`
                            are plain text in the Kconfig tree; parsed here.
  * the dependency truth -> whether a symbol is actually visible is decided by
                            the kernel's own Kconfig parser. We ask it directly
                            by running `make olddefconfig` on a scratch build
                            directory, which costs ~8 s and cannot lie.

Two modes:

  kprobe.py --search PATTERN
      Equivalent of menuconfig's `/` search: every symbol whose name or prompt
      matches, with type, current value, location, depends-on terms, selects
      and the enclosing gates that hide it.

  kprobe.py SYMBOL [SYMBOL ...]
      Compute the smallest set of symbols that must be enabled for the
      requested ones to become visible. Iterates: enable, ask the real parser,
      inspect what is still hidden, add the gates it needs, repeat.
      Prints a ready-to-merge fragment.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
SRC = HERE.parent
BUILD = SRC.parent / "hi3798-linux-build" / "6.12-mv100"
MAKE_ENV = {"ARCH": "arm", "CROSS_COMPILE": "arm-linux-gnueabihf-"}

SKIP_DIRS = {"Documentation", "tools", "scripts", "samples", "usr"}

SYMBOL_DEF = re.compile(r"^(menuconfig|config)\s+([A-Za-z0-9_]+)\s*$")
BLOCK_START = re.compile(r"^(config|menuconfig|choice|menu|if|endif|endmenu|endchoice|source|comment|mainmenu)\b")
IDENT = re.compile(r"\b([A-Z][A-Z0-9_]*)\b")
CONFIG_LINE = re.compile(r"^(?:CONFIG_([A-Za-z0-9_]+)=(.*)|# CONFIG_([A-Za-z0-9_]+) is not set)$")


def logical_lines(path: pathlib.Path) -> list[tuple[int, str]]:
    """Join backslash continuations but keep the first physical line number."""
    out: list[tuple[int, str]] = []
    pending = ""
    start = 0
    for number, raw in enumerate(path.read_text(errors="replace").splitlines(), 1):
        text = raw.rstrip()
        if pending:
            text = pending + " " + text.strip()
        else:
            start = number
        if text.endswith("\\"):
            pending = text[:-1].rstrip()
            continue
        pending = ""
        out.append((start, text))
    if pending:
        out.append((start, pending))
    return out


def symbols_in(expression: str) -> list[str]:
    """Extract config symbol names from a Kconfig expression."""
    stripped = re.sub(r'"[^"]*"', " ", expression)
    seen: list[str] = []
    for match in IDENT.finditer(stripped):
        name = match.group(1)
        if name not in seen:
            seen.append(name)
    return seen


class KconfigIndex:
    def __init__(self, root: pathlib.Path):
        self.root = root
        self.files = self._discover()
        self.lines: dict[pathlib.Path, list[tuple[int, str]]] = {}
        self.defs: dict[str, tuple[pathlib.Path, int]] = {}
        self._index_defs()

    def _discover(self) -> list[pathlib.Path]:
        found = []
        for path in self.root.rglob("Kconfig*"):
            if not path.is_file():
                continue
            if any(part in SKIP_DIRS for part in path.relative_to(self.root).parts):
                continue
            found.append(path)
        return sorted(found)

    def _index_defs(self) -> None:
        for path in self.files:
            for index, (_, text) in enumerate(self.logical_lines(path)):
                match = SYMBOL_DEF.match(text.strip())
                if match and match.group(2) not in self.defs:
                    self.defs[match.group(2)] = (path, index)

    def logical_lines(self, path: pathlib.Path) -> list[tuple[int, str]]:
        if path not in self.lines:
            self.lines[path] = logical_lines(path)
        return self.lines[path]

    def block(self, name: str) -> tuple[pathlib.Path, list[tuple[int, str]]] | None:
        """Return the definition's file and the statements belonging to it."""
        if name not in self.defs:
            return None
        path, start = self.defs[name]
        lines = self.logical_lines(path)
        body = [lines[start]]
        for number, text in lines[start + 1 :]:
            if BLOCK_START.match(text.strip()):
                break
            body.append((number, text))
        return path, body

    def depends(self, name: str) -> list[str]:
        found: list[str] = []
        entry = self.block(name)
        if not entry:
            return found
        for _, text in entry[1]:
            stripped = text.strip()
            if stripped.startswith("depends on"):
                for symbol in symbols_in(stripped[len("depends on") :]):
                    if symbol not in found:
                        found.append(symbol)
        return found

    def selects(self, name: str) -> list[str]:
        found: list[str] = []
        entry = self.block(name)
        if not entry:
            return found
        for _, text in entry[1]:
            stripped = text.strip()
            if stripped.startswith("select"):
                for symbol in symbols_in(stripped[len("select") :]):
                    if symbol not in found:
                        found.append(symbol)
        return found

    def type_and_prompt(self, name: str) -> tuple[str, str]:
        entry = self.block(name)
        if not entry:
            return "?", ""
        for _, text in entry[1]:
            stripped = text.strip()
            for kind in ("bool", "tristate", "string", "int", "hex"):
                if stripped.startswith(kind):
                    prompt = re.search(r'"([^"]*)"', stripped)
                    return kind, prompt.group(1) if prompt else ""
        return "?", ""

    def _gates_at(self, path: pathlib.Path, upto: int) -> list[str]:
        """Symbols guarding a position, from if/menu blocks in this one file."""
        lines = self.logical_lines(path)
        stack: list[list[str]] = []
        for index in range(min(upto, len(lines))):
            stripped = lines[index][1].strip()
            if stripped.startswith("if "):
                stack.append(symbols_in(stripped[3:]))
            elif stripped.startswith("endif"):
                if stack:
                    stack.pop()
            elif stripped == "menu" or stripped.startswith("menu "):
                stack.append(self._menu_deps(lines, index))
            elif stripped.startswith("endmenu"):
                if stack:
                    stack.pop()
            elif stripped.startswith("choice"):
                stack.append([])
            elif stripped.startswith("endchoice"):
                if stack:
                    stack.pop()
        found: list[str] = []
        for group in stack:
            for symbol in group:
                if symbol not in found:
                    found.append(symbol)
        return found

    def gates(self, name: str, depth: int = 0) -> list[str]:
        """Symbols guarding the definition, including gates inherited through
        `source` chains -- e.g. NFS_FS sits under `if NETWORK_FILESYSTEMS`,
        which lives in fs/Kconfig, not in fs/nfs/Kconfig where it is defined."""
        entry = self.defs.get(name)
        if not entry or depth > 8:
            return []
        path, start = entry
        inherited: list[str] = []
        parent = self.sourcer_of(path)
        if parent:
            parent_path, parent_line = parent
            inherited = self.gates_at_path_line(parent_path, parent_line, depth + 1)
        return inherited + [s for s in self._gates_at(path, start) if s not in inherited]

    def gates_at_path_line(self, path: pathlib.Path, line: int, depth: int = 0) -> list[str]:
        if depth > 8:
            return []
        outer: list[str] = []
        parent = self.sourcer_of(path)
        if parent:
            outer = self.gates_at_path_line(parent[0], parent[1], depth + 1)
        return outer + [s for s in self._gates_at(path, line) if s not in outer]

    @staticmethod
    def _menu_deps(lines: list[tuple[int, str]], index: int) -> list[str]:
        deps: list[str] = []
        for _, text in lines[index + 1 :]:
            stripped = text.strip()
            if BLOCK_START.match(stripped) and not stripped.startswith("depends"):
                break
            if stripped.startswith("depends on"):
                deps.extend(symbols_in(stripped[len("depends on") :]))
        return deps

    def sourcer_of(self, path: pathlib.Path) -> tuple[pathlib.Path, int] | None:
        """Find the `source` statement pulling `path` in: (parent, line index)."""
        relative = path.relative_to(self.root).as_posix()
        needle = f'"{relative}"'
        for candidate in self.files:
            if candidate == path:
                continue
            for index, (_, text) in enumerate(self.logical_lines(candidate)):
                stripped = text.strip()
                if stripped.startswith("source") and needle in stripped:
                    return candidate, index
        return None

    def search(self, pattern: str) -> list[str]:
        regex = re.compile(pattern, re.IGNORECASE)
        hits = []
        for name in self.defs:
            _, prompt = self.type_and_prompt(name)
            if regex.search(name) or regex.search(prompt):
                hits.append(name)
        return sorted(hits)


def read_config(path: pathlib.Path) -> dict[str, str]:
    values: dict[str, str] = {}
    if not path.is_file():
        return values
    for line in path.read_text(errors="replace").splitlines():
        match = CONFIG_LINE.match(line.strip())
        if not match:
            continue
        if match.group(1):
            values[match.group(1)] = match.group(2)
        else:
            values[match.group(3)] = "n"
    return values


def run_olddefconfig(config: pathlib.Path, workdir: pathlib.Path) -> None:
    workdir.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(config, workdir / ".config")
    subprocess.run(
        ["make", "-C", str(SRC), f"O={workdir}", "olddefconfig"],
        env={**__import__("os").environ, **MAKE_ENV},
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.STDOUT,
    )


def set_symbols(config: pathlib.Path, names: list[str]) -> None:
    if not names:
        return
    command = [str(SRC / "scripts" / "config"), "--file", str(config)]
    for name in names:
        command.extend(["--enable", name])
    subprocess.run(command, check=False, stdout=subprocess.DEVNULL)


def search_mode(index: KconfigIndex, config: dict[str, str], pattern: str) -> int:
    hits = index.search(pattern)
    if not hits:
        print(f"no symbol matches {pattern!r}")
        return 1
    for name in hits:
        kind, prompt = index.type_and_prompt(name)
        value = config.get(name, "invisible")
        print(f"{name}  [{kind}] = {value}")
        if prompt:
            print(f"  prompt   : {prompt}")
        path, _ = index.defs[name]
        print(f"  location : {path.relative_to(SRC)}")
        deps = index.depends(name)
        if deps:
            print(f"  depends  : {', '.join(deps)}")
        gates = index.gates(name)
        if gates:
            print(f"  gates    : {', '.join(gates)}")
        sel = index.selects(name)
        if sel:
            print(f"  selects  : {', '.join(sel)}")
        print()
    return 0


def resolve(index: KconfigIndex, base: pathlib.Path, targets: list[str]) -> int:
    scratch = pathlib.Path(tempfile.gettempdir()) / "kprobe-scratch"
    if scratch.exists():
        shutil.rmtree(scratch)
    scratch.mkdir(parents=True)

    wanted: list[str] = list(targets)
    applied: list[str] = []
    satisfied: set[str] = set()

    for round_number in range(1, 7):
        candidate = scratch / f"round{round_number}.config"
        shutil.copyfile(base, candidate)
        set_symbols(candidate, [s for s in wanted if s not in applied] + applied)
        applied = list(dict.fromkeys(wanted + applied))

        run_olddefconfig(candidate, scratch / f"out{round_number}")
        values = read_config(scratch / f"out{round_number}" / ".config")

        print(f"[round {round_number}] enabled {len(applied)} candidate(s)")
        newly_satisfied = [
            target for target in targets
            if values.get(target, "n") in ("y", "m") and target not in satisfied
        ]
        for target in newly_satisfied:
            satisfied.add(target)
            print(f"  visible: {target} = {values[target]}")

        unmet = [target for target in targets if target not in satisfied]
        if not unmet:
            print("\nall requested symbols are visible")
            extra = [s for s in applied if values.get(s) in ("y", "m")]
            print("\n# fragment to merge (only symbols that ended up set)")
            print("\n".join(f"CONFIG_{s}=y" for s in extra))
            return 0

        added = False
        for target in unmet:
            deps = index.depends(target)
            gates = index.gates(target)
            blocked_by = [s for s in deps + gates if values.get(s, "n") not in ("y", "m")]
            if blocked_by:
                print(f"  {target} hidden by: {', '.join(blocked_by)}")
                for symbol in blocked_by:
                    if symbol not in applied:
                        applied.append(symbol)
                        added = True
            else:
                entry = index.block(target)
                print(f"  {target} still hidden but its depends are met"
                      f"{'' if entry else ' (symbol not found in tree)'}")
                for symbol in deps + gates:
                    if symbol not in applied:
                        applied.append(symbol)
                        added = True

        if not added:
            print(f"\nstalled; cannot make {', '.join(unmet)} visible")
            return 1
        wanted = applied

    print("\niteration limit reached; results incomplete")
    return 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("symbols", nargs="*", help="CONFIG names to make visible, without prefix")
    parser.add_argument("--search", help="list symbols matching this regex (menuconfig / search)")
    parser.add_argument("--base", type=pathlib.Path, default=BUILD / ".config",
                        help="baseline .config (default: the build directory's)")
    args = parser.parse_args()

    index = KconfigIndex(SRC)
    config = read_config(args.base)

    if args.search:
        return search_mode(index, config, args.search)
    if not args.symbols:
        parser.error("give at least one symbol or use --search")

    return resolve(index, args.base, [s.replace("CONFIG_", "") for s in args.symbols])


if __name__ == "__main__":
    raise SystemExit(main())
