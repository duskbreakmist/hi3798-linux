# 当前频率与拖动撕裂修正

日期：2026-10-29。接续单PP Lima/glamor部署，已通过Windows SSH重新核对板上状态；SDK/内核读取与工具修改在WSL Ubuntu-24.04/ray完成。

## 当前开放档位

| 项目 | 当前结果 | 边界 |
| --- | --- | --- |
| CPU | performance，当前/上限1000000 kHz，开放400/600/800/1000MHz | 正常内核没有开放1200/1500MHz |
| 1200MHz | 之前RAM诊断短检查通过，随后恢复1GHz | 仅标称选择器和一次16MiB检查，非物理测频/长期稳定验收 |
| 1500MHz | 之前切换后板子重启，未完成检查 | 尚不可用，没有重新尝试或加压 |
| eMMC | 实际ios clock=50000000Hz，8-bit，MMC high-speed，3.3V | CIU源时钟100MHz；BIU父时钟200MHz不是eMMC总线频率；不是HS200/HS400 |
| GPU | 标称工作选择200MHz，当前单PP兼容模式 | CCF仍是固定rate建模，无devfreq调频；空闲runtime suspend会门控，不表示一直实际振荡200MHz |

CPUFreq源文件的1200/1500档位由allow_overclock显式opt-in控制，默认启动会屏蔽。旧SDK对QFP216部分1200请求会转换为1000，不能只凭旧sysfs数字认定物理频率。此前检查依据见 `kernel-next-perf-1g-results.md`。

本轮只读CRG/PMC模块得到：

```text
CRG48=00000700 CRGd4=00000010 CRG124=00000206 CRG154=055611d1
PMC48=00fff011 PMC54=00105fff PMCb0=00000001
```

CRG154 bit[7:5]=6是实际GPU选择状态，CRG124 bit[2:0]=6是请求值；按本机SDK MV100表均对应标称200MHz。这是寄存器加SDK表的推导，不是示波器测频。模块只读一直供电的CRG/PMC，读完立即卸载，正常产生out-of-tree taint；没有访问关闭的GPU窗口或改频率/电压。

参考分析对“固定200MHz时钟模型无法进行调频、无devfreq节点”的判断成立，但 `0xd4` 只是GPU门控/复位，不足以实现调频。SDK `mali4xx_clk.c:clk_gpu_switch_frequency_normally()` 使用CRG0x124选择与PMC0x54请求/趋势握手；真实状态需读CRG0x154。GPU应建独立频率源，不能直接改公共200m而影响MMC/SDIO总线。

本机SDK MV100分支默认345600kHz、MAX_FREQ=432000kHz，表内另有500000选项及400000/375000/300000/250000/200000；没有600000档。因此不采用“MV100通常400–600MHz”作为本板提频依据。仍需真实时钟建模及稳定验证后才能开放新的工作档位；本轮没有执行超频。

## 用户现象与处理

用户明确原现象是“仅拖动时横向断裂，停下即恢复”，故优先按撕裂处理，不能直接断言旧窗口damage遗漏。原状态是GPU glamor开启、xfwm4 use_compositing=false、vblank_mode=auto。

本机Xorg 21.1.7的modesetting手册没有TearFree选项，模块strings也无TearFree，因此没有写入无效配置。[Debian对应版本手册](https://manpages.debian.org/bookworm/xserver-xorg-core/modesetting.4.en.html)列出的PageFlip是DRI3翻页开关，并不是simpledrm已有硬件vblank同步的证明。

通过实际ray会话的D-Bus，以ray执行：

```sh
xfconf-query -c xfwm4 -p /general/vblank_mode -s xpresent
xfconf-query -c xfwm4 -p /general/use_compositing -s true
```

xfwm4为4.18.0，编译支持XPresent和内建合成。先设置180秒回退试验，用户反馈横向断裂消失，但拖动桌面图标时看到旧/新两处图标。继续确认后用户答复：“现在拖动窗口完全正常，桌面图标会松开旧图消失”。这是按住拖动时的预览，不能写成已证实的画面残留。

试运行timer在后续整理时已到期，曾自动恢复旧设置；接受脚本最初因timer已卸载返回非零，已修为检查实际合成状态并容忍timer不存在。随后已明确重新应用true/xpresent，保留该组合；GPU glamor、单PP DT及频率均未改变，没有切换CPU绘图或重启桌面。

当前用户现场验收覆盖本次窗口移动和桌面图标拖放，不泛化为所有视频/全屏呈现都已硬件无撕裂。simpledrm仍复制到固定扫描缓冲，原生G0 vblank/pageflip仍属后续驱动工作；此次无需为窗口拖动而立即重写显示驱动。

## 集成与验证

ray的xfconf设置持久保存。部署辅助 `desktop-session-performance.py` 改为默认keep，可显式指定 `--compositing on --vblank-mode xpresent`，避免下一次部署又无条件关掉已验收的合成器。后端选择脚本不覆盖用户xfconf偏好。

最终检查读取true/xpresent，GPU仍为Lima/glamor；本轮dmesg未发现GPU job timeout/task error/Oops/panic。保留原始false/auto供回退，没有做超频、I/O压力或重复GPU基准测试。最终采集见 `reports/2026-10-29/kernel-next/clocks-compositor-final-state.txt`。
