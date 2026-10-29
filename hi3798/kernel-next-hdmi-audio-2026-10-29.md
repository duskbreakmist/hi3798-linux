# Linux 6.12 HDMI 音频适配

## 原因和实现

初始板上没有 /proc/asound、/dev/snd，aplay -l 报无声卡。运行的还是补全 ALSA 之前的内核；原 4.4 的 hisi-sndcard 驱动也没有移植。SDK 的 ALSA shim 依赖 AO/DSP/MMZ 私有栈，不能单纯打开 SND_SOC 或搬 Hi6210 驱动解决。

本轮新增原生 ALSA 驱动 sound/arm/hi3798mv100-hdmi-pcm.c，CONFIG_SND_HI3798MV100_HDMI=y、CONFIG_SND_ARM=y。display/lima/mp2 DT 继承新增音频节点。只支持 MV100/S40、48 kHz、双声道 S16_LE PCM 播放，不宣称录音、SPDIF、压缩音频直通、多声道或热插拔能力。

硬件链路：ALSA 管理的 DMA 一致性缓冲 → AIAO TX3 I2S → HDMI transmitter P1 → HDMI 线缆。DMA 使用正常 Linux DMA API、32 位设备地址及内存屏障，不使用固定 MMZ 地址或原厂二进制库。周期通知暂用 2ms hrtimer 读取硬件 RPTR，尚未接 AIAO 中断。

SDK 寄存器依据：

- aiao_v1_1/ao/drv_ao_op.c：S40 HDMI0 使用 AIAO_PORT_TX3。
- common/src/hal_aiao.c、hal_aiao_priv.c、common/include/hal_aiao_common.h：TX3 源和 TX_CRG3，48k/256Fs，491.52MHz PLL 表值 0x333333；BCLK 除4、FSCLK 除64。
- CRG70 位定义来自 source/common/drv/include/hi3798mv100/hi_reg_crg.h，仅更改 AIAO 复位/时钟位。
- hdmi_1_4/hdmi_drv/si_audio.c、si_hdmitx.c、si_infofrm.c、si_defstx.h：HDMI I2S 输入、IEC60958 通道状态、N=6144、内部 CTS/MCLK、Audio InfoFrame header/checksum 与重复发送。

HDMI P1 基址 0xf8ce0400，各 8 位寄存器以 32 位、4 字节间隔访问；AIAO 基址 0xf8cd0000，TX3 起始 0x2300，缓冲寄存器起始 0x2380。没有操作 USB 接口或其他块设备。

## 迭代与验证

第一候选经 COM8/TFTP 下载 5,861,835 字节，3.4 秒，RAM CRC32 bb2f6e9d 与本机一致；bootm 校验通过，没有写 eMMC 或 saveenv。板上声卡注册为 MV100HDMI，aplay -l 确认 HDMI stereo 48k 设备。

首次自动测试因 /dev/mem 不存在而在播放前退出，不能把它算作一次失败的物理声音验证。改用 ALSA proc 状态后，3 秒测试音暴露持续满缓冲问题：硬件指针停在 280 帧、appl_ptr=16664，aplay 返回 EIO。

期间用户亲自执行 `aplay -D plughw:0,0 /usr/share/sounds/alsa/Front_Center.wav`，明确反馈“可以正常发音了”。这是第一版物理 HDMI 发声的有效确认，不能替代长时间连续播放验证。

发现并修正：AIAO 环形缓冲把 RPTR==WPTR 解释为空，而 ALSA 可以填满整个缓冲。修正版在满缓冲时暂时保留 128 字节，待 DMA 读指针推进后发布剩余尾部，并在每次写满时处理，而不是仅处理首次启动。

只读观测接口：`/sys/bus/platform/devices/f8cd0000.audio/dma_status`，提供 running、TX_CTRL、WPTR、RPTR、WS/BCLK 计数；不要求启用 /dev/mem。

修正版已再次 RAM 启动并完成同一个 3 秒连续播放回归，aplay exit_code=0，经历 RUNNING → DRAINING → 正常结束。ALSA hw_ptr 从 1688 增长到 139920，AIAO 硬件 RPTR 持续推进，WS 计数在约 0.16 秒间隔增长约 7680，符合约 48kHz。没有再次出现停在 280 帧的 EIO。完整采样为 `reports/2026-10-29/hdmi-audio/continuous-playback-result.json`。

桌面 PulseAudio 已自动选择 `alsa_output.platform-f8cd0000.audio.stereo-fallback` 为默认 sink，格式 `s16le 2ch 48000Hz`。空闲时 SUSPENDED 为正常状态，不代表没有声卡；本轮没有重置用户桌面合成设置或替换 Mesa/EGL 库。

最终镜像 5,860,963 字节，SHA256 `b28dad72e5beb7ac41bbc3de9e1920a2e3891d2993cd0f2e8eeda4fb893cda85`。已写入 `/dev/mmcblk0p3`，该分区 start=4096、size=36864 个扇区，未挂载。写入长度补齐为 5,861,376 字节，fsync 后逐字节读回比对通过。没有写其他分区、没有修改启动环境、没有再要求额外重启；当前 RAM 运行版本与写入镜像相同。

原 18MiB 内核分区已保存为 `reports/2026-10-29/hdmi-audio/kernel-before-audio.bin`，备份 SHA256 `8b0bfbd3819494f4f4ae39c5158d395c91afa64cd9c3ef4f2cc60ac35e87626c`，开发机与板上备份一致。安装原始记录保存在板上 `/run/mv100-hdmi-kernel-install.json`，并复制回开发机归档。

最终 RAM 引导的 Windows 包装程序在串口日志包含替换字符时遇到控制台编码异常；TFTP 长度/CRC32、bootm 自校验均已通过，Linux 继续正常启动。已修正包装程序标准输出的错误字符处理，这不是板子内核异常。

限制：只有固定格式 PCM 和一次短连续播放回归经过验证，不宣称长时间压力、低延迟、音画同步、多声道或压缩直通验收。hrtimer 的实际通知分辨率也受现有系统定时器限制，后续若需低延迟应适配 AIAO 周期中断。

## 手动播放

```bash
cat /proc/asound/cards
aplay -l
aplay -D plughw:CARD=MV100HDMI,DEV=0 /usr/share/sounds/alsa/Front_Center.wav
```

plughw 负责把原音频格式转换成此版支持的 48k/stereo/S16_LE。稳定卡名比固定 card0 更适合插有 USB 声卡的情形。

标准桌面 mpv 显式选择 ALSA：

```bash
mpv --ao=alsa --audio-device=alsa/plughw:CARD=MV100HDMI,DEV=0 /path/to/video.mp4
```

该命令只指定音频，不会自动提供 VDEC 硬解；旧独立 softfp mpv-hisi 仍依赖其私有解码/显示路径，不能与本次音频适配混为一谈。桌面 PulseAudio 的默认输出应选实际 MV100 声卡，不是 Dummy Output。

编译、RAM 试启动与永久烧写分别记录；RAM 通过不代表已经永久刷入。通用步骤见 [烧写指南](kernel-next-flashing-guide.md)。
