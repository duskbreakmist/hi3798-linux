# 6.12 配置补全与启动问题调查

## 配置补全

依据 `wb/kernel-next-config-migration-audit-2026-10-29.md`，在内核源码加入 `hi3798/migration-frameworks.config`，按基础配置、框架配置、启动 profile、用户 override 的顺序合并。重新生成后完整配置往返比对通过，用户 override 为空，没有覆盖已有用户改动。

本批补入：

- ALSA、PCM、SoC 音频框架及 USB 音频驱动，剔除 Intel HDA/PCI 音频驱动。
- V4L2、Media Controller、VB2、USB UVC，以及通用 mem2mem 去隔行入口。后者用于由 Kconfig 正确选中隐藏的 V4L2_MEM2MEM_DEV/VB2 DMA-contig 框架，并不是 MV100 VDEC 解码器。
- GPIO 字符设备、PL061、I2C 字符接口/mux、SPI/PL022/spidev。
- RTC、hwmon、thermal/OF、CPU cooling 框架。
- dynamic debug、perf events、ftrace、函数/调度/系统调用追踪。
- SquashFS 及 zlib/LZ4/XZ/Zstd 解压。

27 个关键内置符号记录在 `hi3798/migration-required.json`，由 `verify-mv100.py` 检查。此次也确认了两层媒体门控：MEDIA_PLATFORM_SUPPORT 和 MEDIA_PLATFORM_DRIVERS；仅设置旧菜单符号或隐藏的 V4L2_MEM2MEM_DEV=y 不足以生效。

审计工具已复制到源码 `hi3798/config-audit.py`，构建入口新增 `migration-audit-check.py`，有旧配置时在编译前生成对比报告，候选 gap 相对上次不得增加；首次以上一轮 984 为基准。新克隆找不到旧 SDK 配置时明确报告跳过对照，27 项关键符号检查仍执行。可用 MV100_OLD_CONFIG 指定旧配置位置。

原工具口径：984 → **908**，下降 76 项；新配置启用值数量 826 → 931。原工具将所有非 n 的数字/字符串也计入“启用符号”，且跨架构符号的存在不代表适合本板，因此这些数字是筛选候选统计，不能把余下 908 项一概视为必须开启。也不能为减少数字而启用其他 SoC 驱动。详细对比在 WSL 输出目录 `6.12-mv100/config-migration-audit/`。

框架开启不等于板载音频/视频/传感器已经驱动；此次没有改用其他 SoC 的 DT compatible。

## 无线与恢复事件

初次检查没有 wlan 接口，USB2 控制器下没有发现无线设备。rtl8xxxu 已内置，RTL8188EU 固件也已内置，属于当次硬件没有枚举，而不是遗漏 WLAN 配置。真正原因还需要结合当次 USB 电源/PHY/控制器日志确认，不能宣称根因已修复。

本轮发生操作错误：将 USB1 hub 下 `0bda:9201` RTL9201 存储桥误判为无线设备的安装模式，并执行标准 EJECT。根盘随即出现 EIO，SSH 后来超时。此前没有核对块设备与该 USB 接口的关联，是不应省略的检查。

经用户交出 COM8 并登录 root 后接管，重新绑定 `1-1.3:1.0` 返回设备忙；随后串口和 SSH 确认系统已经经历重新启动、根盘恢复，不能将其归因于重新绑定成功。当前 /dev/sda4 以 ext4 rw 挂载，启动日志报告 journal recovery complete，SSH 正常。没有执行在线 fsck；如需完整文件系统检查，应从 eMMC 启动后卸载 SSD 再检查。

重启后的真实无线设备为 **USB2 2-2、0bda:0179、RTL8188ETV**，rtl8xxxu 加载固件 22.0，成功关联 AP 并取得 IPv4。存储设备 `0bda:9201` 同时仍存在。因此不能把无线恢复称为模式切换修复，也没有部署任何针对 9201 的自动切换规则。

## 屏幕启动慢

- 内核 HDMI 初始化约在 uptime 0.64 秒完成。
- LightDM 一次日志显示启动 Xorg 后等待约 65 秒才收到就绪通知。
- 后一次 Xorg 日志：23.585 → 56.640 秒位于 glamor 初始化；56.838 → 86.876 秒位于 GLX/AIGLX 初始化，后者约 30.04 秒。
- 因此主要等待在 Mesa/Xorg 初始化阶段，不是 HDMI 初始化本身。NTFS 挂载曾耗时 14.5 秒，但不足以解释两个独立的 30 秒等待。
- 没有发现 Lima fault/timeout 日志。root 和 ray 的 Mesa shader cache 都存在，不能仅凭延迟就认定缓存未开启或 CPU 太慢。
- 尝试独立 GLX 查询时发现板上未安装 glxinfo；尚未获得等待系统调用的有效 trace，没有修改桌面合成器或关闭硬件加速碰运气。

## 温度传感器

SDK `source/msp/drv/pm/drv_pmoc_intf.c` 的 mpu_init_temperature/mpu_get_temperature 有 MV100 实现：PMC 基址 0xf8a23000，PMC10 写 0x07200000；从 PMC12/PMC13 的 8 个字节取均值，换算 `raw*180/255-40` 摄氏度，再按 OTP 0x258 的符号/幅度校正。PMC 寄存器宏使用 u32 指针，偏移单位是 4 字节，不能直接把寄存器编号当字节偏移。

6.12 的 hisilicon,tsensor 对应 hi6220 的驱动实现，不是看到 compatible 相同就能认定支持 MV100。MV100 需要专用驱动/DT 和校准读取，本批先补齐 thermal/hwmon 框架，不写未经验证的传感器寄存器或套用 hi6220 公式。

## mpv/V4L2 加速

当前运行的旧 6.12 配置关闭 MEDIA_SUPPORT；没有 /dev/video*、/dev/media* 或原厂 VDEC 设备。源码 drivers/media/platform 没有 MV100 VDEC 后端。新版配置将提供媒体框架，但不能仅开启 V4L2 选项就让 mpv 的 v4l2m2m 获得芯片硬解。

此前 4.4 下已完成的独立 softfp mpv-hisi + 真正 hisi_h264 + hisimali 入口仍保留在 SSD；它不是假的 stub，也不能直接假定兼容 6.12。缺的是相应厂商内核 VDEC/MMZ/共享缓冲驱动接口。Lima Mali-450 加速 GLES 渲染，不替代 VDEC。

可行后续路线：将 SDK VDEC 及内存/同步依赖适配 6.12，先恢复独立解码/帧导出，再选择继续使用既有私有 decoder，或增加 V4L2 mem2mem 接口供标准 FFmpeg/mpv 调用。需要实际解码输出、画面和 CPU 证据验收，不能以 /dev/video 节点或 mpv 枚举后端作为成功依据。
