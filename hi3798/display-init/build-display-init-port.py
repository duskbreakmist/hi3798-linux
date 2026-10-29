"""Recompile only MV100 boot display initialization, outside the SDK tree.

Builds an experimental kernel module; never deploys or enables it.
This initialization bridge is not a complete DRM/KMS driver.
"""
from pathlib import Path
import subprocess, json, shutil, argparse, os
parser = argparse.ArgumentParser()
parser.add_argument('--raw-only', action='store_true')
parser.add_argument('--out', type=Path)
args = parser.parse_args()
src = Path(__file__).resolve().parents[2]
sdk = Path(os.environ.get('MV100_SDK_ROOT', str(src.parent/'HiSTBLinux'))).expanduser().resolve()
boot = Path(os.environ.get('MV100_SDK_BOOT', str(sdk/'out/hi3798mv100/hi3798mv100_out/obj/source/boot'))).expanduser().resolve()
cross = os.environ.get('CROSS_COMPILE', 'arm-linux-gnueabihf-')
for dependency in [boot/'product/driver/disp/libdisp.a', boot/'product/driver/hdmi/libhdmi.a']:
    if not dependency.is_file():
        raise SystemExit('Missing prepared HiSTBLinux boot dependency: '+str(dependency)+'; set MV100_SDK_ROOT/MV100_SDK_BOOT after preparing the SDK boot build.')
product = boot/'product'
disp = product/'driver/disp'
out = (args.out or (src.parent/'hi3798-linux-build/display-init-port')).resolve()
out.mkdir(parents=True, exist_ok=True)
original_disp = disp
staged = out/'src'
shutil.copytree(disp/'vdp_v2_0', staged/'vdp_v2_0', dirs_exist_ok=True)
shutil.copytree(product/'driver/hdmi/hdmi_1_4', staged/'hdmi_1_4', dirs_exist_ok=True)
disp = staged
vdp = staged/'vdp_v2_0/hal/3798m/vdp_driver.c'
t = vdp.read_text(errors='surrogateescape')
for function in ['VDP_CBM_GetMAXLayer', 'VDP_GFX_SetLayerEnable']:
    position = t.index(function)
    guard = t.rfind('#ifndef __DISP_PLATFORM_BOOT__', 0, position)
    assert guard >= 0 and position - guard < 350
    t = t[:guard] + t[guard:].replace('#ifndef __DISP_PLATFORM_BOOT__', '#if 1 /* include graphics scanout helpers */', 1)
vdp.write_text(t, errors='surrogateescape')
osal = staged/'vdp_v2_0/com/drv_disp_osal.h'
t = osal.read_text().replace('#define DISP_IOADDRESS(a) (HI_U32 *)(a)',
    'extern void *mv100_mmio_address(unsigned long address);\n#define DISP_IOADDRESS(a) ((HI_U32 *)mv100_mmio_address(a))')
osal.write_text(t)
reg = staged/'hdmi_1_4/drv_reg_proc.h'
t = reg.read_text().replace('#define HDMI_REG_WRITE(addr, val) (*(volatile unsigned int *)(addr) = (val))',
    'extern void *mv100_mmio_address(unsigned long address);\n#define HDMI_REG_WRITE(addr, val) (*(volatile unsigned int *)mv100_mmio_address(addr) = (val))')
t = t.replace('#define HDMI_REG_READ(addr) (*(volatile unsigned int *)(addr))',
    '#define HDMI_REG_READ(addr) (*(volatile unsigned int *)mv100_mmio_address(addr))')
reg.write_text(t)
p = staged/'hdmi_1_4/drv_hlviic.c'
t = p.read_text()
for bank in ['SLV0','SLV1']:
    t = t.replace(f'(volatile HI_U32 *)(HDMI_TX_{bank}_ADDR_BASE + RegAddr * 4)',
                  f'(volatile HI_U32 *)mv100_mmio_address(HDMI_TX_{bank}_ADDR_BASE + RegAddr * 4)')
p.write_text(t)
shim = out/'include'
shim.mkdir(exist_ok=True)
shutil.copytree(product/'driver/include_inc', shim, dirs_exist_ok=True)
(shim/'uboot.h').write_text('#include <stddef.h>\n#include <stdio.h>\n#include <string.h>\nvoid udelay(unsigned long);\nvoid mdelay(unsigned long);\nunsigned long get_timer(unsigned long);\n')
(shim/'exports.h').write_text('#include <uboot.h>\n')
(shim/'common.h').write_text('#include <uboot.h>\n')
(shim/'delay.h').write_text('void udelay(unsigned long);\nvoid mdelay(unsigned long);\n')
# These units need declarations only, not a userspace C runtime. Kernel-side
# adapters will supply logging/memory functions; do not mix glibc float ABIs.
(shim/'stdio.h').write_text('#include <stddef.h>\n#include <stdarg.h>\nint printf(const char *, ...);\nint snprintf(char *, size_t, const char *, ...);\n')
(shim/'string.h').write_text('#include <stddef.h>\nvoid *memcpy(void *, const void *, size_t);\nvoid *memset(void *, int, size_t);\nint memcmp(const void *, const void *, size_t);\nsize_t strlen(const char *);\n')
report = Path(os.environ.get('MV100_REPORT_DIR', str(out/'reports'))).expanduser().resolve()
report.mkdir(parents=True, exist_ok=True)
includes = [shim, product/'driver/include', product/'driver/include_inc',
            boot/'fastboot/arch/arm/include', boot/'fastboot/include',
            sdk/'source/boot/fastboot/arch/arm/include', sdk/'source/boot/fastboot/include',
            sdk/'source/common/include', sdk/'source/common/drv/include', product/'driver/pq']
includes += [product/'driver/graphic/include', sdk/'source/msp/drv/hifb/include',
             sdk/'source/msp/drv/hifb/inc']
includes += [staged/'hdmi_1_4', product/'driver/graphic/gfx',
             product/'logo/include']
includes += [disp/'vdp_v2_0'/x for x in ['alg','com','drv','hal/common','hal/3798m']]
flags = ['-O2', '-marm', '-mfloat-abi=soft', '-ffreestanding', '-fno-builtin',
         '-fshort-wchar', '-mno-unaligned-access', '-fno-unwind-tables', '-fno-asynchronous-unwind-tables',
         '-fno-pic', '-fno-pie', '-fno-stack-protector',
         '-D__DISP_PLATFORM_BOOT__', '-D__DISP_SELF_START__',
         '-D__DISP_D0_FOLLOW_D1__', '-DHI_BUILD_IN_BOOT', '-DCHIP_TYPE_hi3798mv100']
flags += ['-DHI_HDMI_4K_SUPPORT', '-DHI_MINIBOOT_SUPPORT']
flags += ['-I'+str(x) for x in includes]
objects = subprocess.check_output([cross+'ar', 't', str(original_disp/'libdisp.a')], text=True).splitlines()
records = []
with (report/'display-init-recompile.log').open('w') as log:
    for obj in objects:
        candidates = list((disp/'vdp_v2_0').rglob(Path(obj).with_suffix('.c').name))
        if len(candidates) > 1:
            # include_src.mak selects hal/common for these duplicated units.
            candidates = [p for p in candidates if p.parent in
                          (disp/'vdp_v2_0/hal/common', disp/'vdp_v2_0/hal/3798m')]
        if len(candidates) != 1:
            raise RuntimeError((obj, candidates))
        result = subprocess.run([cross+'gcc', *flags, '-c', str(candidates[0]), '-o', str(out/obj)], stdout=log, stderr=log)
        records.append({'source':str(candidates[0]), 'result':result.returncode})
        if result.returncode:
            break
    if not any(r['result'] for r in records):
        hdmi = product/'driver/hdmi'
        hdmi_objects = subprocess.check_output([cross+'ar', 't', str(hdmi/'libhdmi.a')], text=True).splitlines()
        for obj in hdmi_objects:
            source = staged/'hdmi_1_4'/Path(obj).with_suffix('.c')
            result = subprocess.run([cross+'gcc', *flags, '-c', str(source), '-o', str(out/('hdmi-'+obj))], stdout=log, stderr=log)
            records.append({'source':str(source), 'result':result.returncode})
            if result.returncode:
                break
(report/'display-init-recompile.json').write_text(json.dumps(records, indent=2)+'\n')
print(json.dumps(records, indent=2))
if any(r['result'] for r in records):
    print((report/'display-init-recompile.log').read_text()[-5500:])
    raise SystemExit(1)
compiled = [out/obj for obj in objects] + [out/('hdmi-'+obj) for obj in hdmi_objects]
templates = Path(__file__).resolve().parent
subprocess.run([cross+'gcc', *flags, '-c', str(templates/'display-pdm-defaults.c'), '-o', str(out/'pdm-defaults.o')], check=True)
compiled += [out/'pdm-defaults.o']
subprocess.run([cross+'ld', '-r', '-o', str(out/'display-init.o'), *map(str, compiled)], check=True)
subprocess.run([cross+'objcopy', '--redefine-sym', 'printf=mv100_sdk_printf',
                '--redefine-sym', 'udelay=mv100_sdk_udelay', '--redefine-sym', 'mdelay=mv100_sdk_mdelay',
                str(out/'display-init.o'), str(out/'display-init-lib.o_shipped')], check=True)
if args.raw_only:
    print('Built raw initialization object: '+str(out/'display-init-lib.o_shipped'))
    raise SystemExit(0)
shutil.copy2(templates/'display-linux-adapter.c', out/'display-linux-adapter.c')
(out/'Makefile').write_text('obj-m += mv100_display_bridge.o\nmv100_display_bridge-y := display-linux-adapter.o display-init-lib.o\n')
subprocess.run(['make', '-C', str(src),
                'O='+os.environ.get('OUT_DIR', str(src.parent/'hi3798-linux-build/6.12-mv100')),
                'ARCH=arm', 'CROSS_COMPILE='+cross, 'M='+str(out), 'modules'], check=True)
dependencies = subprocess.check_output([cross+'nm', '-u', str(out/'display-init.o')], text=True)
(report/'display-init-port-undefined.txt').write_text(dependencies)
print('Combined initialization object; remaining adapters:\n'+dependencies)
