# 当前设备状态（供下一个模型继续）

## 设备
- 实际硬件：OnePlus Pad 2 Pro / OnePlus Pad 3，型号 OPD2413，Android 16 / KernelSU
- 当前是二改包；`ro.product.*`/fingerprint 显示 Xiaomi / 25091RP04C / piano，仅是 ROM 属性，不能据此判断硬件型号

## 已修复
- com.qti.phone 崩溃循环：已禁用 enabled=3，不再拖垮 system_server
- ProcSched 模块：已禁用，sched_util_clamp_min=0，不再锁满频
- QQ 后台渲染：qq_render_guard.sh 已修，绑小核 mask=63，nice=19

## 音频/扬声器
- 6 路 AW882xx 功放：aw_dev_0(左上) aw_dev_1(右下) aw_dev_2(左下) aw_dev_3(右上) aw_dev_5(右高) aw_dev_6(左高)
- DTS sound-channel 映射：ch0=9-0034, ch1=4-0035, ch2=9-0035, ch3=4-0034, ch5=4-0036, ch6=9-0036；ch4/ch7 空
- 扬声器后端配置已改为 8ch：/vendor/etc/audio/sku_sun/resourcemanager_sun_mtp.xml
- V4A 扬声器配置：卷积(索尼醇音+.irs)+10段EQ+bass/clarity+multiband+field+diff+stereoImager

## 8ch PCM 未解问题
- PCM 设备 24 (TDM-LPAIF_WSA-RX-PRIMARY) tinypcminfo 声称 max 8ch
- 直接 open 8ch 成功，但 write 返回 EINVAL（NDK bionic 编译的 split8 和系统 tinyplay 都一样）
- `strace` 已精确确认 split8 的 ioctl 顺序：INFO/HW_PARAMS/SW_PARAMS/PREPARE 全成功，
  `SNDRV_PCM_IOCTL_WRITEI_FRAMES` 返回 EINVAL。
- PCM 24 只声明 `RW_INTERLEAVED`（access mask 0x8），所以 `WRITEN_FRAMES` 对应的
  non-interleaved 模式不适用；PCM status mmap 也返回 ENXIO，不存在可替代的直接 mmap 写法。
- 在 Android HAL 正常播放且 6 路 `aw_dev_*_switch` 全部 Enable 时，直接写 PCM 24 仍然
  EINVAL，排除“只差功放 mixer route”这一假设。
- 已确认 Android HAL 正常播放并不写 PCM 24。`audiohalservice.qti` 通过
  `/vendor/lib64/libar-pal.so` 的 PAL/AGM 建图；其打开的 PCM 16/17 fd 不是音频数据写入点，
  PCM 24 是物理 TDM/WSA backend，不是可由用户态直接灌数据的 front-end PCM。
- `libar-pal.so` 中有 `Session::setSlotMask`、`setTaggedSlotMask`、8 项 channel-map 日志，
  以及 `ResourceManager::allocateFrontEndIds`、`SessionAlsaPcm::open/write`。槽位与通道映射
  是 PAL 建图的一部分，不是 PCM 24 上独立可见的 tinymix 开关。
- 下一步应停止尝试 tinyalsa 直接写 PCM 24，改为让 split8 直接使用 PAL 或 AGM：创建
  8ch PCM front-end session，设置 `PAL_DEVICE_OUT_SPEAKER`/WSA backend、slot mask 和
  channel map，再由 PAL/AGM 连接到 PCM 24 backend。这样不经过 Android Audio HAL，
  但仍使用设备内核要求的 Qualcomm DSP graph 层。

## 本轮逆向文件
- 已拉取设备 `/vendor/lib64/libar-pal.so` 到本地 `reverse/libar-pal.so`（仅本地分析，
  不应提交该 vendor blob）。
- `Session::setSlotMask`：0x37e550，size 3756。
- `SessionAlsaPcm::open`：0x3a207c，`SessionAlsaPcm::write`：0x3ad944。

- 已新增 `pal_probe.c`：通过 `dlopen` 直接调用设备 `libar-pal.so`，请求 8ch/48k/S16 的
  `PAL_STREAM_DEEP_BUFFER -> PAL_DEVICE_OUT_SPEAKER`，只写 20 ms 全零数据。
- GitHub Actions run 32287330034 构建成功，产物在容器 `/root/split8-artifact/pal_probe`。
  因 Android 桥接断开，本轮尚未上传和运行；不能把“构建成功”误记为“PAL 8ch 已成功”。
