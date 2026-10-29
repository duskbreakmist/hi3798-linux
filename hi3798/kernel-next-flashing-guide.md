# 6.12 内核编译、TFTP 试启动与 eMMC 烧写

适用本项目 Hi3798MV100、SDK fastboot、COM8/115200。内核位于 eMMC 的 kernel 分区，SSD/eMMC 根文件系统由现有 bootargs/bootcmd 选择；刷内核不等于更换根文件系统。

## 1. 编译

WSL Ubuntu-24.04，以 ray 普通用户运行：

```bash
cd ~/workplace/hi3798-linux
./3start_make_cp.sh
```

成功后默认镜像为 `~/workplace/hi3798-linux-build/6.12-mv100/hi_kernel.bin`；`release/latest/` 保存同次配置、镜像、哈希和模块。默认 codex-lima-mp2 设备树。若本次编译失败，不要烧写遗留的旧 hi_kernel.bin。

## 2. 生成 TFTP 文件及本次专用命令

以下脚本将镜像补零到 512 字节边界，放入构建输出目录的 tftp/，并打印本次的精确写入长度及 CRC32。不会修改原镜像或访问板子。

```bash
python3 - <<'PY'
from pathlib import Path
import hashlib, zlib
out = Path.home() / 'workplace/hi3798-linux-build/6.12-mv100'
data = (out / 'hi_kernel.bin').read_bytes()
assert data[:4] == bytes.fromhex('27051956'), '不是 legacy uImage'
data += bytes((-len(data)) % 512)
assert 0 < len(data) <= 18 * 1024 * 1024, '超过 kernel 分区'
target = out / 'tftp/hi_kernel.bin'
target.parent.mkdir(exist_ok=True)
target.write_bytes(data)
blocks = len(data) // 512
print('TFTP 文件:', target)
print('SHA256:', hashlib.sha256(data).hexdigest())
print('字节数:', len(data), 'CRC32:', f'{zlib.crc32(data):08x}')
print('tftp 0x10000000 hi_kernel.bin')
print(f'crc32 0x10000000 0x{len(data):x}')
print(f'mmc write 0 0x10000000 0x1000 0x{blocks:x}')
print(f'mmc read 0 0x14000000 0x1000 0x{blocks:x}')
print(f'crc32 0x14000000 0x{len(data):x}')
PY
```

把打印出的 TFTP 文件复制到电脑 TFTP 服务的实际根目录，名字为 hi_kernel.bin。Windows TFTP 服务需要允许板子通过有线网卡访问 UDP；不要把 WSL 的 NAT 地址当作 fastboot 可直接访问的服务地址。

如果使用自定义 OUT_DIR，修改脚本的 out 路径使之对应此次输出。

## 3. 进入 fastboot、下载与 RAM 试启动

通过 Linux 执行 reboot，在 COM8 中按 Ctrl+C 中断自动启动，直到出现 fastboot#。若是 rootfs 选择菜单，按固件菜单说明中断；不要为了进入提示符随意选择会保存环境的选项。

```text
setenv ipaddr 192.168.37.10
setenv serverip 192.168.37.64
tftp 0x10000000 hi_kernel.bin
```

serverip 使用电脑**实际有线 IPv4 地址**，示例 192.168.37.64 不是固定要求。setenv 只改当次内存环境，此处不要 saveenv。

确认 Bytes transferred 与第二步打印的字节数一致，再执行第二步打印的 RAM crc32 命令；结果必须一致。

只想试启动、不刷 eMMC：

```text
bootm 0x10000000
```

bootm 会执行下载的内核，并沿用现有 bootargs。它不会写内核分区。RAM 试启动后若需永久刷入，重启回 fastboot，重新下载并校验。

## 4. 写入并读回验证

在已成功下载和校验的同一个 fastboot 会话中，执行第二步生成的 **mmc write、mmc read、读回 crc32** 三条命令。不要复用其他构建的长度。

读回 CRC32 与本机计算结果一致后：

```text
reset
```

分区依据：SDK emmc_partitions.xml 的 kernel 从 2 MiB 开始，长度 18 MiB。512 字节扇区对应起始 0x1000、最大长度 0x9000。历史示例中的长度 0x4000 只有 8 MiB，不能无条件用于未来镜像。mmc write 的第三、四个数是扇区号和扇区数，不是字节数。

本流程只写 kernel 分区，不写 bootloader、bootargs 或 rootfs，不改持久启动选择。执行 mmc write 前应已保留可恢复的原内核备份。

## 5. 启动确认与模块

```bash
uname -a
zcat /proc/config.gz | grep -E 'CONFIG_SND|CONFIG_HI3798MV100_DISPLAY_INIT'
dmesg | tail -40
```

若配置使用 M，需把同次 release/latest/modules/lib/modules/ 下对应版本的模块部署到实际根文件系统，并执行 depmod；不要在板上安装 WSL 交叉工具链。全部相关驱动为 Y 时无需另复制这些驱动的 ko。声卡是否出现用 `cat /proc/asound/cards` 和 `aplay -l` 检查，HDMI 是否实际出声仍需播放验证。
