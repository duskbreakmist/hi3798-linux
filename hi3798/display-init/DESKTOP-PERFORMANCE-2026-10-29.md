# 6.12 桌面流畅度修正与 Mali 后续排查

日期：2026-10-29。接续 `kernel-next-display-gpu-results-2026-10-29.md`。

## 当前结果

用户反馈桌面比旧内核卡顿后，检查发现 Xorg 被原配置固定为 fbdev，使用 shadow framebuffer；实际 ray 会话的 xfwm4 软件合成也处于开启状态。CPU 已是 performance / 1000000 kHz，不能把此次改善归因于新增 CPU 超频。

现已保留以下组合，用户现场确认“现在流畅不少”：

```conf
Section "Device"
    Identifier "MV100 DRM software"
    Driver "modesetting"
    Option "kmsdev" "/dev/dri/card0"
    Option "AccelMethod" "none"
    Option "ShadowFB" "false"
    Option "SWcursor" "true"
EndSection
```

Xorg 日志确认 `glamor disabled`、`ShadowFB: preferred NO, enabled NO`、`Damage tracking initialized`；RandR 枚举 1920×1080、60Hz。该60Hz是模式信息，不是测得的动画帧率；本轮没有做 FPS 对比或压力测试。实际 ray 会话的 `xfconf-query -c xfwm4 -p /general/use_compositing` 为 false。

主要改变是取消 Xorg shadow 缓冲及软件合成开销。内核 simpledrm 仍在 `simpledrm_primary_plane_helper_atomic_update()` 中调用 `drm_fb_blit()`，把发生变化的区域复制到固定扫描缓冲，**不是零拷贝，也不是已实现 GPU 桌面加速**。因同时修改两个选项，不能独立量化各选项的贡献。

## 检查方法与截图

第一次软件 modesetting 试运行正常，但90秒回滚计时器到期恢复 fbdev；这次 Xorg 正常退出，内核 uptime 连续，不能误写成又一次板子重启。最终设置由 `keep-verified-software-kms.py` 重新应用并验证后保留。

功能检查 `verify-direct-kms-window.py` 在 X11 左上角临时映射一个96×96、override-redirect的纯红窗口，等待一次刷新，从显示桥接的只读 status 读取**物理扫描缓冲首像素**，要求 RGB=ff0000；销毁窗口后要求原像素恢复。该流程通过后才删除 pending 标记、停止180秒回滚计时器并保留配置。检查器 stdout 没有另存原始JSON文件，接受状态和当前配置保存在 `desktop-final-state.txt`；不补造具体像素值。

这项检查只验证小范围刷新、撤销与物理缓冲一致性；不是性能基准或 HDMI 外部采集。用户对实际桌面及流畅度的现场确认与其共同构成证据。

截图通过 Xorg `xwd -root -screen` 采集，Windows Pillow 按XWD头中的 byteorder、stride、RGB mask 转为PNG，1920×1080。由于 modesetting 分配独立 DRM dumb framebuffer，不能继续假定 `/dev/fb0` 就是 Xorg 正在显示的缓冲。

原始文件在工作区 `reports/2026-10-29/kernel-next/`：

- `desktop-modesetting.xwd`、`desktop-modesetting.png`：当前 LXQt 桌面截图，已人工查看PNG。
- `Xorg-modesetting.log`：最终 Xorg 配置、damage tracking、键鼠接入。
- `Xorg-first-software-kms.log`：较早软件 modesetting 试运行的日志，不是原fbdev日志。
- `legacy-fbdev.conf`：原来的完整4.4/fbdev配置。
- `desktop-final-state.txt`：内核、boot ID、CPU、配置、服务、合成状态。

## 持久化与4.4恢复

板上安装 `/usr/local/share/mv100-desktop/profile.py` 与 `legacy-fbdev.conf`，启用 `mv100-desktop-profile.service`，在 LightDM 前选择配置：

- MV100、内核≥5且card0绑定simple-framebuffer：使用本轮已验证的 modesetting CPU路径。
- 旧内核或设备条件不匹配：恢复原fbdev配置。设备就绪最多等3秒，不引入无限等待。

服务已手动运行成功，当前配置保持不变；安装后没有为了重复验收而再次重启。4.4选择分支完成代码检查，尚未实际回旧内核测试。ray的合成设置保存在其自己的xfconf配置中；最初以root执行时误连root会话总线，随后已改为读取实际xfwm4进程的会话环境并以ray设置，最终读取false。

可重部署脚本为 `scripts/kernel-next/mv100-desktop-profile.py`。首次安装必须显式提供原fbdev配置，不接受把modesetting配置当作旧配置备份：

```sh
python3 /run/mv100-desktop-profile.py install \
  --legacy-config /root/mv100-software-kms-trial/original.conf
```

手动回退可运行 `python3 /usr/local/share/mv100-desktop/profile.py legacy`，再重启LightDM。若要持久停用自动选择，还需 `systemctl disable mv100-desktop-profile.service`。重新启用合成时，必须在ray实际桌面会话中执行 `xfconf-query -c xfwm4 -p /general/use_compositing -s true`。

旧试运行 guard 已 disabled、pending 已删除、回滚timer inactive。试验配置中的进程级 `MESA_LOADER_DRIVER_OVERRIDE=pl111` wrapper和LightDM覆盖均已撤销；没有全局替换图形库。

## Mali 桌面尝试与重启边界

本轮开始时仍运行上一阶段的 GPU PMU RAM 候选，具有renderD128。尝试将Xorg换为modesetting/glamor，并只在Xorg进程设置 `MESA_LOADER_DRIVER_OVERRIDE=pl111`，利用已安装Mesa的generic kmsro入口做实验；该名称不表示本板存在PL111控制器。

Xorg加载glamoregl并进入GLX初始化后，后续SSH检查发现板子已经重启，回到eMMC中保存的GPU关闭内核，桌面初始化未完成。没有捕获到重启瞬间的串口、panic或GPU fault证据，因此只能记录时间上的关联，**不能断言已经证明是GPU、watchdog或某个具体寄存器导致**。原始完整glamor失败日志未保全，后续软件试验已覆盖Xorg轮转文件。

75秒的最初回滚是/run中的transient timer，重启后会丢失，不能保护这种跨重启失败。已立即从 `/root/mv100-xorg-backup-20261029/fbdev` 恢复原配置，再进行带持久boot guard的软件路径试验。后续GPU桌面试验必须先保留跨重启回退和持续串口记录，不能直接重跑旧 `try-kmsro-xorg.py`。

当前 `/dev/dri` 只有card0，没有renderD128；这是持久化显示基线的预期状态。此次未刷写新内核，也未将未完成的glamor路径保存为默认。上一阶段256×256单帧渲染通过的结论仍有效，但不能推广成整个桌面加速通过。

Mesa官方说明Lima只负责渲染，桌面输出还要求可共享缓冲的独立显示驱动/kmsro：[Lima文档](https://docs.mesa3d.org/drivers/lima.html)。下一步应分别验证Lima持续任务、DRM PRIME缓冲共享与Xorg接入，并评估用G0原生DRM扫描替代simpledrm拷贝；仅设置renderer名字或强制别名不足以完成这条链路。

## 外部电源/时钟控制的只读调查

继续对照本机SDK `source/msp/drv/gpu/utgard/platform/mali450/mali4xx_clk.c`、`mali4xx_reg.h` 和 `source/common/drv/include/hi3798mv100/hi_reg_crg.h`。除已修正的Mali内部PMU域mask外，SDK `hisi_pmc_setparameter()` 还设置片外PMC及CRG状态机；当前Lima移植没有对应平台初始化。该差异需要核对，但还不是已证实的重启原因。

正常内核未提供 `/dev/mem`，直接Python只读映射失败。因此改用单次只读诊断模块，限定MV100，ioremap一直供电的CRG/PMC，读取后立即iounmap并卸载；没有访问已关闭的GPU寄存器窗口，也没有写MMIO。模块为out-of-tree，正常产生taint标志；不作为持久驱动安装。

实际读回（GPU节点关闭的正常显示内核）：

```text
CRG48=00000700 CRGd4=00000010 CRG124=00000206 CRG154=055611d1
PMC48=00fff011 PMC54=00105fff PMCb0=00000001
```

SDK MV100字段定义：CRG154 bit[7:5] 是实际GPU时钟选择状态，当前为6；CRG124 bit[2:0] 是请求选择，当前也是6。按本芯片SDK选择表，6对应**标称200MHz**；这是寄存器状态与SDK表的推导，不是外部测频。当前读取发生在GPU关闭时，也不能倒推前一次glamor运行中完全相同。

PMCb0 bit0计数bypass为1，SDK会清除bit0/1；CRG124 bit9 begin_cfg_bypass为1，SDK会清除；PMC54低16位为5fff，SDK初始化设2fff。PMC48 power-time读回00fff011，而SDK初始化值是00ffff88。尚未向板上写入这些SDK初始化值，未尝试GPU升频/改电压。

证据为 `gpu-soc-controls-readonly-module.txt`、`gpu-soc-probe-build.log`，诊断源码 `scripts/kernel-next/mv100-gpu-soc-probe.c`。下一候选应先明确这些控制与Mali PMU/runtime PM的配合，保留故障日志，再做一次针对新假设的功能试验。
