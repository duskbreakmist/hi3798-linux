# Hi3798MV100 Linux 6.12 port and developer workflow

源码仓库：https://github.com/duskbreakmist/hi3798-linux

日常分支：`codex/hi3798-6.12-bringup`，基于 gregkh/linux 的 Linux **6.12.114**。这是完整内核源码的 fork，加上本板移植提交；没有向上游发起 PR，也不需要先 `git pull` 更新版本。

本机 WSL 源码：`/home/ray/workplace/hi3798-linux`。默认编译目录：`/home/ray/workplace/hi3798-linux-build/6.12-mv100`。默认 bootloader profile 保留从 fastboot 环境选择 eMMC/SSD 根分区的方式；它不写入 bootcmd。

## 日常操作

在 WSL Ubuntu-24.04 中，以 ray 普通用户运行，不用 sudo：

```bash
cd ~/workplace/hi3798-linux
./1start_menuconfig.sh     # 首次生成配置；已有配置则保留，进入菜单
./2just_menuconfig.sh      # 继续调整当前输出目录的配置
./3start_make_cp.sh        # 保存配置、编译、打包、复制镜像到输出目录
```

菜单中 Y 是内置，M 是模块，N 是关闭。退出时保存。三个脚本都位于内核源码根目录，名字沿用旧 SDK；**不会执行 distclean，不会刷板，不会重启板子**。第 1、2 个脚本退出菜单后自动保存可跟踪配置；第 3 个再次保存、编译并生成发布目录。

```bash
./3start_make_cp.sh --save-only         # 类似旧 SDK 的第 3 个脚本：仅保存，不编译
./1start_menuconfig.sh --reset         # 备份当前配置，再从项目配置和用户改动重建
JOBS=14 ./3start_make_cp.sh             # 指定并行编译数
./3start_make_cp.sh --copy-to /mnt/h/tftp/hi_kernel.bin
```

`--copy-to` 只复制文件；目录已存在时写入其下 `hi_kernel.bin`，否则按文件名处理。源目录之外的默认构建目录中也会生成 `hi_kernel.bin`，不必使用此参数。

默认镜像为 **codex-lima-mp2**。需要其他设备树时使用 `--variant codex-lima` 等；默认日常配置保留 MP2 GPU 与显示初始化。`--variant` 选择打包的 DT，不会自动更改内核 Kconfig。

## 配置保存与 Git

| 文件 | 用途 |
| --- | --- |
| `hi3798/mv100.config` 等 | 项目基础配置和启动 profile |
| `hi3798/configs/bootloader.override.config` | menuconfig 相对于基础配置的改动；包含对已固定选项的修改 |
| `hi3798/configs/bootloader.config` | 实际完整配置快照 |
| `arch/arm/configs/hi3798mv100_codex_defconfig` | Kconfig 生成的最小 defconfig |
| 输出目录 `.config` | 当前实际编译配置 |
| 输出目录 `config-backups/` | 每次操作前的配置备份，不进 Git |

基础配置、profile、用户 override 依次合并。保存时通过 Kconfig 重新生成，并逐项比较配置，确认改动能恢复后才更新源码中的保存文件。因此重新 configure 不会把已保存的 menuconfig 修改覆盖掉。关键启动选项仍由 `verify-mv100.py` 检查；禁用它们会阻止保存和编译，避免生成明显不能启动的配置。

修改后可自己提交：

```bash
git diff --stat
git diff -- hi3798/configs/bootloader.override.config
git add hi3798/configs/bootloader.override.config hi3798/configs/bootloader.config arch/arm/configs/hi3798mv100_codex_defconfig
git commit -m "arm: hi3798: update daily kernel configuration"
git push
```

首次克隆此移植分支：

```bash
git clone --single-branch --branch codex/hi3798-6.12-bringup https://github.com/duskbreakmist/hi3798-linux.git
cd hi3798-linux
```

建议完整保留当前 6.12.114 基础，后续升级补丁版本单独开分支处理。不要在日常分支直接 pull 上游 master；它不是本板使用的 6.12 移植分支。`origin` 指向个人 fork，`upstream` 指向 kernel.org 稳定源码。

## 编译依赖与 SDK

Ubuntu 24.04：

```bash
sudo apt install build-essential gcc-arm-linux-gnueabihf binutils-arm-linux-gnueabihf libncurses-dev flex bison bc libssl-dev libelf-dev python3 u-boot-tools device-tree-compiler rsync kmod
```

日常配置启用了 SDK 衍生的显示初始化。虽然 Linux/DRM/Lima 源码已在仓库中，**显示初始化仍需要原 HiSTBLinux SDK 的准备好的 boot 源码目录**，不能把它描述为完全脱离 SDK 的显示驱动。原始 SDK 源码、libdisp.a/libhdmi.a 等构建文件没有上传到此 fork；脚本只读取库中的对象名，再从 SDK 源码重新编译。

默认查找内核源码同级的 `HiSTBLinux`；可改为：

```bash
export MV100_SDK_ROOT=/path/to/HiSTBLinux
# 若 SDK boot 输出布局不同，可再指定：
export MV100_SDK_BOOT=/path/to/prepared/obj/source/boot
```

支持 `CROSS_COMPILE`、`OUT_DIR`、`JOBS`。输出目录必须在源码树外。不同 profile 使用单独 `OUT_DIR`，或显式 `1start_menuconfig.sh --profile ssd --reset`；后续命令使用同一 `--profile`。日常推荐 bootloader profile。

SDK 的重新编译日志默认在生成对象旁的 `vendor/reports/`，不再依赖 Windows H 盘；也可设置 `MV100_REPORT_DIR`。

## 编译输出与部署边界

```text
../hi3798-linux-build/6.12-mv100/
  hi_kernel.bin                 # 默认 codex-lima-mp2 的 uImage，可交给已有 TFTP/烧写流程
  release/latest/               # 指向此次发布目录
    hi_kernel.bin
    uImage-mv100-*
    kernel.config
    defconfig
    build.json                  # 源码提交、是否有未提交修改、镜像/配置 SHA256
    modules/lib/modules/...     # 同一次编译的模块
```

发布脚本只在离线输出目录安装模块；不会覆盖板上 `/lib/modules`。若启用 M，部署内核时需要同步对应 kernel release 的模块。仅复制镜像不能部署新增模块。

## 当前硬件边界

截至本次发布前的实机记录：1 GiB RAM、CPU 日常上限 **1 GHz**、eMMC 50 MHz、Mali-450 MP2 200–400 MHz devfreq。1.2 GHz 压测曾失败，1.5 GHz 也未稳定，不能宣称支持日常运行。显示路径为 simpledrm 加 SDK 衍生 HDMI/G0 初始化，固定 1080p60；MP2 包含本芯片完成轮询补偿。细节见源码 `hi3798/display-init/` 中研究记录。本次任务只整理源码和编译流程，没有重新烧写或改变这些运行策略。

## 本次验证

完成当前日常配置保存/完整重建比对，以及将基础配置中 `PRINTK_TIME=y` 改为关闭、保存、reset 后保持关闭，再恢复日常配置的往返检查。另执行一次完整构建入口的增量编译、DT 校验、镜像打包和模块发布目录生成。没有重复板上测试。

## 配置框架补全

后续已按 4.4 迁移审计补齐 ALSA、媒体、总线、RTC、thermal/hwmon 和追踪框架。见 [配置迁移与启动调查](CONFIG-MIGRATION-2026-10-29.md)。构建前检查 27 个必要内置符号，并在旧配置可用时生成完整迁移对照。框架开启不代表板载音频、VDEC 或温度驱动已完成。

## HDMI 音频

已接入 MV100 原生 ALSA HDMI PCM，固定 48kHz/双声道/S16_LE。声卡名 MV100HDMI；plughw 支持软件格式转换，桌面 PulseAudio 可自动选择声卡。实际出声和短连续播放已经验证。过程、边界与播放命令见 [音频记录](kernel-next-hdmi-audio-2026-10-29.md)，[烧写指南](kernel-next-flashing-guide.md)。
