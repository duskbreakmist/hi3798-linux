# 6.12：1 GiB 与硬件功能候选（2026-10-28）

当前为开发过程记录，尚未刷写本轮候选。eMMC kernel 分区仍保留先前 SSD/NTFS/WiFi 版本。

## 已验证一次的项目

- `uImage-mv100-perf-1g` 通过 TFTP RAM 引导，4,846,131 字节，耗时 2.8 秒；SHA256 `c9d31a841cd55d7fcd79022124a3deec4a796ac26fb821b414873e9f2fa25a2e`。日志：`reports/2026-10-28/kernel-next/perf-1g-ram-boot-2.txt`。
- SSD `/dev/sda4` 正常启动，SSH 可用；`free -m` 显示 Mem total 1001 MiB，`/proc/iomem` RAM 为 `00000000-3fffffff`。SDK cfg.mak 未启用 TEE，原 DT 已声明 1 GiB，本轮将命令行 mem=512M 改为 mem=1G。旧 MMZ 参数不代表新内核预留了 256 MiB；以后启用私有媒体 DMA 前必须重新划分保留区域。
- CPUFreq 暴露 400/600/800/1000 MHz，userspace 单次设置 400 MHz 再恢复 1000 MHz，sysfs 回读一致，然后恢复 performance governor。数值来自 SDK 时钟选择器表，未独立测量物理 CPU 频率。
- eMMC ios：50,000,000 Hz、8 位、MMC high-speed、3.3 V。读 kernel 分区前 1 MiB 成功，SHA256 `5ae32b99178bf369cb9e99144292c28193acbe3d64f2a3bb6caa91b3b991ba60`。未进行写入压力测试。
- `/dev/dma_heap/system` 与 `/dev/dma_heap/reserved` 存在；eMMC 原本已使用控制器内置 IDMAC，不依赖新增通用 DMAengine。
- LZ4 zram 已由原有 `zram.service` 创建 512 MiB swap。禁用重复且失败的 `zramswap.service`，保留原有服务。

## 修正与待验收

- ARM DT 路径的 CONFIG_CMDLINE_EXTEND 把内核参数放在 bootloader 参数后；首个候选重复的 root=/dev/sda4 会覆盖 bootroot 的选择。已从附加命令行移除 root=，在 DT chosen 中设置仅无 ATAG 时使用的 SSD 默认值。后续镜像需验收。bootcmd 由 bootloader 执行，内核接收的是 bootargs；不让 Linux 执行 bootcmd。
- SDK restart 使用 WDTLOAD=0x100；通用 SP805 使用 0，本板实际有数分钟复位延迟。已加入仅 MV100 使用 0x100 的兼容修正，待下个候选验证。
- ARM 架构计时器只读观察：PFR1=00011011，但 CNTFRQ=0、1ms 计数增量=0。继续使用工作的 SP804；没有盲目切换架构计时器。
- simpledrm/fbcon 已内建，但未生成 fb0/DRI：无有效显示交接描述。SDK Logo_Main 只有取得有效显示参数与 Logo checkflag 才调用显示初始化。没有证据证明当前 fastboot 已完成 HDMI/G0 初始化，因此未凭空指定 framebuffer 地址。
- Mali DT 继续禁用，之前重复绘制曾卡死；不能据此版本宣称硬件加速桌面已完成。

## 1200/1500 MHz 实验

仅诊断 profile 显式 opt-in，固定启动电压，不改电压或重编 PLL，磁盘分区保持不挂载。每次试验开启 8 秒看门狗、单次短内存检查，然后恢复 1 GHz。

首个诊断候选在进入 init 前停住，未执行命令。源码确认 cpufreq_table_validate_and_sort 会覆盖 driver init 中设置的 policy->max，原 performance governor 可能直接请求最大实验档位；该候选已弃用。修正版改为诊断 profile 默认 userspace，并在 target_index 拒绝自动 governor 请求 1 GHz 以上档位。正常 bootloader profile 不暴露高档位。

SDK 对 QFP216 的部分“1200 MHz”请求实际转换为 1000 MHz，随后软件读数仍记为 1200；此前 4.4 的 sysfs 数字不能单独证明真实频率。高频是否可用，以后续实测记录为准，当前不宣称成功。
