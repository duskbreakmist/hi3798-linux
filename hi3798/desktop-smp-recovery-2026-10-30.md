# 桌面异常、辅核超时与 4.4 对照

本轮目标仍为解决 6.12 桌面启动慢、窗口带着壁纸移动和图标拖动异常。中途用户报告 htop 核心 offline，转而优先验证 CPU 硬件。

## 当前实机状态

用户明确要求“刷4.4核验证”，已恢复当初从本板备份的原版 **4.4.35-s40 #4 SMP Fri Jun 5 18:12:33 CST 2026**，正常从 eMMC `/dev/mmcblk0p4` 启动。

实测：`/sys/devices/system/cpu/online` 为 **0-3**，`/proc/cpuinfo` 有 processor 0、1、2、3；日志 CPU1/2/3 依次在 0.134/0.146/0.158 秒上线，`[0.158114] Brought up 4 CPUs`。因此当前证据指向 6.12 移植/配置回归，不能把辅核超时当作 CPU 已损坏。

当前串口为 **COM7、115200**；COM8 为旧记录。电脑有线地址仍 192.168.37.64，但自动路由曾选择 WLAN；SSH 用 `-b 192.168.37.64` 后可正常连接 `hi3798_37_10`。TFTP 使用 `--server-ip 192.168.37.64 --port COM7`。没有改持久网络或 bootargs，用户此前已自行切到 eMMC 根分区。

## 原版恢复与备份

- 原 4.4 完整分区：`artifacts/kernel-next/backups/stage1-20261027/mmcblk0p3-kernel.bin`，18MiB，SHA256 `80a420664bee2e113b8d2c3748ce85d707f5a4071ee228810f4b562d4d3657cb`。
- 恢复前 6.12 完整分区已取回：`reports/2026-10-29/desktop-repaint/kernel-before-restore44.bin`，SHA256 `b5a26a0e3d16f6cb7f519d4036147b30f372b8b5fffbe419662551f683e2d96c`。
- 只写 `/dev/mmcblk0p3`，start=4096、size=36864 扇区，整分区读回 SHA256 与原 4.4 备份一致。未恢复旧环境分区，未改 rootfs。
- 恢复工具：`scripts/kernel-next/restore-original44-kernel.py`。串口捕获在 reboot 命令真正执行前已到期，不能把只有 login 的 serial 文件当作启动证据；**4.4 四核结论来自启动后的 SSH uname、sysfs、cpuinfo、dmesg 实测输出**。

## 6.12 故障证据

用户先贴出 CPU1、CPU2 各 5 秒 `failed to boot: -110`，只有 CPU0、CPU3。随后从 eMMC 启动的完整日志显示 CPU1/2/3 全失败，仅 CPU0，额外延迟 15 秒。完整用户日志附件在本会话提供，不把其日期当作当前编译时间：6.12 采用固定 KBUILD 时间。

源配置 SMP=y、NR_CPUS=4、HOTPLUG_CPU=n，不是运行时主动热插拔。音频初始化晚于 SMP 上线阶段，但新增内核配置、代码布局、跨重启硬件状态等仍须逐项排查，不能凭执行先后直接排除本轮集成回归。

第一尝试仅增加每核复位断言、按 SDK 顺序将 pen 发布移到电源/时钟恢复后，并添加超时寄存器日志。候选 SHA256 `09cbe2f55096953939f971edd075b21412f4c2d54344387958a8d44b383491d7`，6MiB，COM7 TFTP/CRC32 验证后 RAM 引导：**仍失败**。CPU1/2/3 pen 各为 1/2/3，reset 依次 00ccc001/00888001/00000001，clock=00000700。没有将此未通过候选写入 eMMC。对应源码差异尚未提交，不能作为已解决修复。

SDK 真正 MV100 路径为 `arch/arm/mach-hi3798mx/`，不是 mach-hisi 或 mach-hi3798mv2x。SDK 的 headsmp 在进入 pen 前做 TLB/I-cache/L1 D-cache 初始化；6.12 当前把 L1 初始化留到通用 `__v7_ca7mp_setup`。SDK 每个 CPU 启动前调用 set_scu_boot_addr，6.12 只在 prepare 写一次。安全启动另有 SC_GEN1 邮箱路径，但旧配置与运行 4.4 的查询均未发现启用 CONFIG_SECURE_EXTENSION，不能直接认定应使用安全路径。

4.4 实际运行配置启用 DYNAMIC_FTRACE=y；本轮补全的 6.12 FUNCTION_TRACER=y、DYNAMIC_FTRACE=n，为静态追踪插桩。应单独检查这项性能/早期启动差异，尚未证明它是辅核超时根因。

## 桌面与根盘问题仍未完成

6.12 当前桌面为 modesetting/glamor + Mesa pl111 KMSRO entry + Lima MP2，xfwm4 compositing=true、vblank_mode=xpresent。实际会话总线就是 `/run/user/1000/bus`。

Xorg 记录 glamor 初始化约 35 秒、GLX 初始化约 30 秒。EGL 全系统调用 strace 45 秒未完成，被 timeout 清理，没有拿到单个 30 秒内核等待的确证。应减少追踪范围，按调用阶段/真实像素定位，不能凭此认定 shader cache、CPU 或 CMA 为根因。CMA32MiB 当时余量约 1.9MiB，但 simpledrm/Lima 使用 shmem，未证明是 CMA 分配问题。

截图在 `reports/2026-10-29/desktop-repaint/desktop-before.png`。生成了 `x11-repaint-probe.py` 和 `analyze-egl-trace.py`，但当时 SSH 被重置，**尚未上传或运行窗口探针**。不要把脚本存在当作完成像素验证。

当时串口实际记录 SSD sda4 ext4 目录读取 error -5 / EIO，SSH 握手重置。用户已重启并切到 eMMC，SSD sda4 未挂载，尚未执行本轮离线 fsck。此类 EIO 在 2026-10-27、内核迁移之前已有记录（见 stage1-results），不能直接归因于此次音频或图形驱动。根盘问题需独立处理，避免拿不稳定文件系统继续做图形归因。

## 2026-10-30 最终修复、用户验收与烧录

本文前面的“尚未完成”均为调查时快照，以下为最终状态。

- 恢复 `CONFIG_DYNAMIC_FTRACE=y`，保留 FUNCTION_TRACER 和音频框架；6.12 两次 RAM 启动均恢复 0-3 四核，最新日志 0.265833 秒完成 SMP。失败的手工复位顺序实验已撤回。当前证据支持配置修复有效，不把二进制布局因素排除在外；verify-mv100.py 已增加保护。
- simpledrm 在 MV100 上创建的 shmem BO 使用 WC，匹配 Lima DMA-BUF 的映射属性，避免缓存别名导致 CPU shadow 拷贝读旧像素；其他平台仍保持原策略。四次颜色窗口移动的 X11 像素检查通过；用户最终确认窗口/图标改善，鼠标无拖影和黑块残留。
- Mesa KMSRO 原先依次探测 vc4、etnaviv、msm、panfrost，每次约 7.5 秒才轮到 lima。新增进程局部 libkmsro-lima-select.so，明确启用时直接打开并核实 renderD128 驱动为 lima。EGL 初始化约 0.4–0.6 秒，仍为 Mali450；最新 Xorg GLX 24.601→24.630 秒（约 0.03 秒），此前约 30 秒。仅 Xorg 和 mv100-mesa-run 启用，不全局污染 softfp SDK runtime。
- 交叉编译库：`hi3798/display-init/rootfs/build-kmsro-select.sh /home/ray/workplace/hi3798-linux-build/userspace`。将结果安装到板上 `/usr/local/lib/mv100/libkmsro-lima-select.so`，同时部署已更新的 profile 和应用包装器。内核 3start_make_cp.sh 不会自动安装这份用户态库。
- SSD 在 4.4/eMMC 下卸载后，先只读检查，再执行一次 `e2fsck -f -p -z`，重放日志并清理 orphan_present，退出 1 表示已修复；undo 与日志在 reports/2026-10-29/desktop-repaint。此次 6.12 运行未见新的 I/O error。
- HDMI MV100HDMI 声卡仍在，保留此前用户实听成功的 PCM 实现。

正式烧录镜像 5,941,291 字节，SHA256 `37ce9a4d09ef8dbd18092d8f797394eca888375f3680e6f1ddbe26694422d416`；来源 hi_kernel-wc.bin。检查 uImage header/data CRC、load/entry、p3 几何后，仅写内核 p3，512 字节补齐，fsync 并逐字节回读通过。

原 4.4 完整分区哈希仍为 `80a420664bee2e113b8d2c3748ce85d707f5a4071ee228810f4b562d4d3657cb`，烧录前已核实。板上备份目录 `/var/backups/mv100-desktop-20261030/` 包含 kernel-original44.bin、bootenv-before.bin、install.json。持久 bootargs root 从 mmcblk0p4 改为 sda4，rootfs_sel=1；先校验原环境 CRC，备份整个 p2，保留 bootcmd 与其他变量，写入64KiB环境后回读整个p2一致。未改 bootloader。

验证范围：两次修正后 RAM 启动（包含6.12暖重启）、一次有限窗口探针、用户屏幕验收及正式烧录回读；未做长时间压力测试。当前串口 COM7/115200，TFTP/SSH 有线地址192.168.37.64。

正式烧录后已执行一次 reboot：自动从 eMMC 内核进入 `/dev/sda4`，运行 6.12.114-hi3798-mv100-exp+，CPU online=0-3，SMP 0.265844 秒，Xorg 正常启动。首次 SSH 探测发生在网络就绪前而超时，随后连接成功；串口正常到登录提示。
