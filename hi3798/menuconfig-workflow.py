#!/usr/bin/env python3
"""Safe out-of-tree menuconfig, persistent overrides, build and release copy."""
from __future__ import annotations
import argparse
from datetime import datetime
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from zoneinfo import ZoneInfo

SRC = Path(__file__).resolve().parent.parent
HERE = SRC / 'hi3798'
LINE = re.compile(r'^(?:CONFIG_([A-Za-z0-9_]+)=(.*)|# CONFIG_([A-Za-z0-9_]+) is not set)$')


def config_values(path: Path) -> dict[str, str]:
    result = {}
    for line in path.read_text().splitlines():
        match = LINE.match(line)
        if match:
            result[match[1] or match[3]] = match[2] if match[1] else 'n'
    return result


def stamp() -> str:
    return datetime.now(ZoneInfo('Asia/Shanghai')).strftime('%Y%m%d-%H%M%S-%f')


def atomic_copy(source: Path, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix=target.name + '.', dir=target.parent)
    os.close(fd)
    temporary = Path(name)
    try:
        shutil.copyfile(source, temporary)
        temporary.chmod(0o644)
        temporary.replace(target)
    finally:
        temporary.unlink(missing_ok=True)


def execute(command: list[str], env: dict[str, str]) -> None:
    print('+ ' + shlex.join(command), flush=True)
    subprocess.run(command, env=env, cwd=SRC, check=True)


def make(out: Path, target: str, env: dict[str, str], *extra: str) -> None:
    execute(['make', '-C', str(SRC), 'O=' + str(out), target, *extra], env)


def backup(out: Path) -> None:
    if (out / '.config').is_file():
        target = out / 'config-backups' / (stamp() + '.config')
        atomic_copy(out / '.config', target)
        print('配置备份：' + str(target), flush=True)


def reference_config(out: Path, profile: str, env: dict[str, str], override: Path | None) -> Path:
    reference = out / 'config-reference'
    reference.mkdir(parents=True, exist_ok=True)
    requested = reference / 'requested.config'
    command = [sys.executable, str(HERE / 'make-profile.py'), profile, str(requested)]
    command += ['--user-config', str(override)] if override else ['--without-user']
    execute(command, env)
    configured = dict(env, KCONFIG_ALLCONFIG=str(requested))
    make(reference, 'allnoconfig', configured)
    return reference / '.config'


def save(out: Path, profile: str, env: dict[str, str]) -> None:
    make(out, 'olddefconfig', env)
    execute([sys.executable, str(HERE / 'verify-mv100.py'), str(out / '.config')], env)
    baseline = config_values(reference_config(out, profile, env, None))
    current = config_values(out / '.config')
    changed = {key: value for key, value in current.items() if value != baseline.get(key, 'n')}
    temporary = out / 'menuconfig.override.pending'
    lines = ['# Saved by the MV100 menuconfig workflow; applied after the project/profile fragments.',
             '# Review with git diff. Changes to existing pinned symbols are preserved too.', '']
    lines += ['# CONFIG_' + key + ' is not set' if value == 'n' else 'CONFIG_' + key + '=' + value
              for key, value in sorted(changed.items())]
    temporary.write_text('\n'.join(lines).rstrip() + '\n')
    replay = config_values(reference_config(out, profile, env, temporary))
    differences = [key for key in current.keys() | replay.keys()
                   if current.get(key, 'n') != replay.get(key, 'n')]
    if differences:
        raise RuntimeError('配置不能完整重建；尚未覆盖已保存配置。差异：' + ', '.join(sorted(differences)[:25]))
    make(out, 'savedefconfig', env)
    folder = HERE / 'configs'
    atomic_copy(temporary, folder / (profile + '.override.config'))
    atomic_copy(out / '.config', folder / (profile + '.config'))
    defconfig = SRC / 'arch/arm/configs/hi3798mv100_codex_defconfig' if profile == 'bootloader' else folder / (profile + '_defconfig')
    atomic_copy(out / 'defconfig', defconfig)
    temporary.unlink()
    print(f'已保存 {len(changed)} 个差异，完整重建比对通过。', flush=True)
    print('用户配置：' + str(folder / (profile + '.override.config')), flush=True)
    print('完整配置：' + str(folder / (profile + '.config')), flush=True)
    print('defconfig：' + str(defconfig), flush=True)


def release(out: Path, profile: str, variant: str, env: dict[str, str], destination: Path | None) -> None:
    image = out / ('uImage-mv100-' + variant)
    if not image.is_file():
        raise RuntimeError('缺少镜像：' + str(image))
    folder = out / 'release' / stamp()
    folder.mkdir(parents=True)
    for source in sorted(out.glob('uImage-mv100-*')):
        atomic_copy(source, folder / source.name)
    atomic_copy(image, folder / 'hi_kernel.bin')
    atomic_copy(out / '.config', folder / 'kernel.config')
    atomic_copy(out / 'defconfig', folder / 'defconfig')
    make(out, 'modules_install', env, 'INSTALL_MOD_PATH=' + str(folder / 'modules'))
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=SRC, text=True).strip()
    dirty = bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=SRC))
    metadata = {'profile': profile, 'dtb_variant': variant, 'source_commit': head,
                'source_dirty': dirty, 'sha256': hashlib.sha256(image.read_bytes()).hexdigest(),
                'config_sha256': hashlib.sha256((out / '.config').read_bytes()).hexdigest(),
                'kernel_release': subprocess.check_output(['make', '-s', '--no-print-directory', '-C', str(SRC), 'O=' + str(out), 'kernelrelease'], env=env, text=True).strip(),
                'flashed': False}
    (folder / 'build.json').write_text(json.dumps(metadata, indent=2) + '\n')
    atomic_copy(image, out / 'hi_kernel.bin')
    latest = out / 'release/latest'
    if latest.exists() and not latest.is_symlink():
        raise RuntimeError('拒绝替换非符号链接：' + str(latest))
    link = out / ('release/.latest-' + stamp())
    link.symlink_to(folder.name, target_is_directory=True)
    link.replace(latest)
    if destination:
        destination = destination.expanduser().resolve()
        if destination.is_relative_to(SRC):
            raise RuntimeError('--copy-to 必须在内核源码目录之外')
        if destination.is_dir():
            destination /= 'hi_kernel.bin'
        atomic_copy(image, destination)
        print('已复制：' + str(destination), flush=True)
    print('发布目录：' + str(folder), flush=True)
    print('默认镜像：' + str(out / 'hi_kernel.bin'), flush=True)
    print('SHA256：' + metadata['sha256'], flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['start', 'continue', 'save', 'build'])
    parser.add_argument('--profile', choices=['bootloader', 'direct', 'ssd'], default=os.environ.get('MV100_PROFILE', 'bootloader'))
    parser.add_argument('--reset', action='store_true', help='备份当前.config后，从项目及已保存用户配置重新生成')
    parser.add_argument('--no-ui', action='store_true', help='只运行olddefconfig并保存，用于非交互检查')
    parser.add_argument('--save-only', action='store_true', help='只保存配置，不编译')
    parser.add_argument('--variant', choices=['codex', 'codex-io', 'codex-display', 'codex-lima', 'codex-lima-mp2'], default=os.environ.get('MV100_DTB_VARIANT', 'codex-lima-mp2'))
    parser.add_argument('--copy-to', type=Path, help='另复制hi_kernel.bin到指定文件或已有目录，不烧写')
    args = parser.parse_args()
    if os.geteuid() == 0:
        raise SystemExit('请以普通开发用户运行，不要使用sudo。')
    out = Path(os.environ.get('OUT_DIR', str(SRC.parent / 'hi3798-linux-build/6.12-mv100'))).expanduser().resolve()
    if out.is_relative_to(SRC):
        raise SystemExit('OUT_DIR必须在源码树之外')
    if os.environ.get('KCONFIG_CONFIG', '.config') != '.config':
        raise SystemExit('这些便捷脚本使用输出目录的.config；请先取消自定义KCONFIG_CONFIG。')
    env = dict(os.environ, ARCH='arm', CROSS_COMPILE=os.environ.get('CROSS_COMPILE', 'arm-linux-gnueabihf-'), OUT_DIR=str(out), MV100_PROFILE=args.profile)
    if not shutil.which(env['CROSS_COMPILE'] + 'gcc'):
        raise SystemExit('找不到ARM编译器；Ubuntu安装gcc-arm-linux-gnueabihf。')
    out.mkdir(parents=True, exist_ok=True)
    with (out / '.mv100-workflow.lock').open('a') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise SystemExit('同一输出目录已有另一个便捷脚本运行。')
        metadata = out / '.mv100-profile'
        if metadata.exists() and metadata.read_text().strip() != args.profile and not (args.action == 'start' and args.reset):
            raise SystemExit('输出目录属于另一profile；使用1start_menuconfig.sh --reset或单独OUT_DIR。')
        if args.reset and args.action != 'start':
            raise SystemExit('--reset只适用于1start_menuconfig.sh')
        if args.action != 'start' and not (out / '.config').is_file():
            raise SystemExit('尚无.config；先运行./1start_menuconfig.sh。')
        backup(out)
        if args.action == 'start' and (args.reset or not (out / '.config').is_file()):
            execute(['bash', str(HERE / 'build-mv100.sh'), 'configure'], env)
        metadata.write_text(args.profile + '\n')
        print(f'源码：{SRC}\n输出：{out}\nprofile：{args.profile}', flush=True)
        if args.action in ['start', 'continue']:
            make(out, 'olddefconfig' if args.no_ui else 'menuconfig', env)
        save(out, args.profile, env)
        if args.action == 'build' and not args.save_only:
            execute(['bash', str(HERE / 'build-mv100.sh'), 'build'], env)
            release(out, args.profile, args.variant, env, args.copy_to)


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error))
