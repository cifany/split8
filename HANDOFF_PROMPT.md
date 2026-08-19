# split8 逆向排查交接文档

更新时间：2026-08-20

## 设备身份

实际硬件必须按以下信息处理：

- OnePlus Pad 2 Pro / OnePlus Pad 3
- 型号：OPD2413
- Android 16
- KernelSU
- 当前系统是二改包

注意：设备的 `ro.product.manufacturer`、`ro.product.device`、fingerprint 当前显示过 `Xiaomi`、`piano`、`25091RP04C`。这些是二改包的 ROM 属性，不能据此判断实际硬件为 Xiaomi Pad 8 Pro。硬件身份以用户确认的 OPD2413 为准。

## 设备命令方式

所有 Android 命令通过容器执行：

```sh
/root/android_bridge.sh "Android 上要执行的命令"
```

桥接恢复检测命令：

```sh
for i in $(seq 1 20); do
  timeout 3 bash -c 'echo > /dev/tcp/127.0.0.1/16666' 2>/dev/null && break
  sleep 5
done
/root/android_bridge.sh "echo ALIVE; getprop sys.boot_completed"
```

本次交接前已执行上述等待逻辑，但最后仍为 `ConnectionRefusedError: [Errno 111] Connection refused`，所以没有拿到新的设备输出。桥接恢复后必须先重新执行 ALIVE 检查。

## 严格禁止事项

不要执行以下操作：

- 不要重启 framework。
- 不要 `stop/start` 系统服务。
- 不要重新启用 `com.qti.phone`；当前已禁用，`enabled=3`。
- 不要重新启用 ProcSched；当前模块已禁用，`sched_util_clamp_min` 已复位 0。
- 不要修改 `/data/adb/service.d/qq_render_guard.sh`；QQ 后台渲染已绑定小核，mask=63，nice=19。
- 不要修改已经调好的 V4A 扬声器配置。
- 不要全盘执行 `grep -R`，会卡死桥接。
- 不要使用用户提供的 GitHub PAT 写入文档、脚本、日志或提交信息；凭据只用于需要时的临时认证。
- 任何音频实验都优先使用短时、静音、可回收的测试，不要直接播放大音量测试音。

## 已修复内容

- `com.qti.phone` 崩溃循环已禁用，不能恢复。
- ProcSched 模块已禁用，调度 clamp 已复位。
- QQ 后台渲染保护脚本已完成。
- V4A 扬声器链路已调好。
- 6 路 AW882xx 功放映射已确认。

## 六路功放与通道映射

功放：

- `aw_dev_0`：左上
- `aw_dev_1`：右下
- `aw_dev_2`：左下
- `aw_dev_3`：右上
- `aw_dev_5`：右高
- `aw_dev_6`：左高

DTS sound-channel 映射：

- ch0 = `9-0034`
- ch1 = `4-0035`
- ch2 = `9-0035`
- ch3 = `4-0034`
- ch5 = `4-0036`
- ch6 = `9-0036`
- ch4、ch7 为空

不要重新推导或修改以上映射，除非有新的硬件证据。

## 当前音频问题

目标：不依赖 Android 高通 Audio HAL，让 `split8` 对输入立体声做六路分频，并通过设备音频图驱动 6 路功放。

当前 split8 原始实现：

- 输入：2ch、16-bit WAV。
- 输出目标：card 0、PCM device 24。
- PCM 24：`TDM-LPAIF_WSA-RX-PRIMARY`。
- 目标输出：8ch、48 kHz、S16 interleaved。

PCM 24 的 `tinypcminfo`：

- Access：`0x000008`，即 `RW_INTERLEAVED`。
- Format：S16_LE、S24_LE、S32_LE、S24_3LE。
- Rate：8000 到 48000 Hz。
- Channels：1 到 8。
- Period：128 到 4096。
- Period count：2 到 128。

## 已证实的 PCM 24 结果

直接用 tinyalsa 的 `pcm_open` 打开 8ch 成功，但任何写入均返回 `EINVAL`。系统 `tinyplay` 也相同。

对 `/data/local/tmp/split8` 做过 syscall 抓取，顺序为：

```text
openat("/dev/snd/pcmC0D24p", O_RDWR|O_NONBLOCK) = 4
SNDRV_PCM_IOCTL_INFO = 0
SNDRV_PCM_IOCTL_HW_PARAMS = 0
SNDRV_PCM_IOCTL_SW_PARAMS = 0
mmap(status) = -1 ENXIO
SNDRV_PCM_IOCTL_SYNC_PTR = 0
SNDRV_PCM_IOCTL_PREPARE = 0
SNDRV_PCM_IOCTL_WRITEI_FRAMES = -1 EINVAL
```

因此：

- 不是 `HW_PARAMS` 失败。
- 不是 `SW_PARAMS` 失败。
- 不是 `PREPARE` 失败。
- 不是 tinyalsa 的 frame/byte 计算问题。
- `WRITEN_FRAMES` 不适用，因为设备只声明 `RW_INTERLEAVED`，没有 non-interleaved access。
- mmap 不是可用替代路径，status mmap 已返回 `ENXIO`。

在 Android HAL 普通播放、6 路 `aw_dev_*_switch` 全部 Enable 时，直接写 PCM 24 仍为 `EINVAL`，所以不能继续猜“只缺几个 mixer 开关”。

## HAL/PAL 逆向结论

真实 HAL 服务：

```text
/vendor/bin/hw/audiohalservice.qti
```

关键库：

```text
/vendor/lib64/hw/libaudiocorehal.qti.so
/vendor/lib64/libar-pal.so
/vendor/lib64/libagmclient.so
/vendor/lib64/libtinyalsa.so
```

`libar-pal.so` 导出的接口包括：

```text
pal_init
pal_deinit
pal_stream_open
pal_stream_start
pal_stream_write
pal_stream_stop
pal_stream_close
```

还发现：

```text
Session::setSlotMask
SessionAlsaVoice::setTaggedSlotMask
ResourceManager::allocateFrontEndIds
SessionAlsaPcm::open
SessionAlsaPcm::write
```

PAL 源码参考仓库已在本地下载：

```text
/root/split8/reverse/arpal-lx
```

设备 `libar-pal.so` 已拉取到：

```text
/root/split8/reverse/libar-pal.so
```

该 `reverse/` 目录已被 `.gitignore` 忽略，不应提交 vendor blob 或参考源码。

### 重要判断

Android HAL 正常播放时没有直接向 PCM 24 写数据。PCM 24 是物理 WSA/TDM backend，不是普通用户态应该直接灌数据的 front-end PCM。

PAL/AGM 的正常逻辑应为：

```text
pal_stream_open
  -> 分配 front-end
  -> 设置 stream/device media config
  -> 设置 slot mask / channel map
  -> 连接 WSA backend
pal_stream_start
pal_stream_write
```

所以当前可行方向不是继续修改 tinyalsa 的 WRITEI/WRITEN/mmap，而是让 split8 直接调用设备 PAL，绕过 Android Audio HAL，但保留 Qualcomm PAL/AGM 必需的 DSP graph 建立过程。

## 已提交的代码改动

仓库：

```text
/root/split8
```

已推送提交：

```text
dcb431a  add direct PAL 8-channel probe
e028377  clarify hardware identity and probe status
```

新增：

```text
/root/split8/pal_probe.c
```

探针行为：

- `dlopen("/vendor/lib64/libar-pal.so")`。
- `dlsym` 加载 PAL 函数，不直接链接 vendor blob。
- 请求 `PAL_STREAM_DEEP_BUFFER`。
- 输出 `PAL_AUDIO_OUTPUT`。
- 48 kHz、16-bit、8ch。
- 设备 `PAL_DEVICE_OUT_SPEAKER`。
- 使用标准 8ch PAL channel map：FL、FR、C、LFE、LB、RB、LS、RS。
- `pal_stream_write` 只写约 20 ms 的全零数据。
- 探针结束时 stop、close、deinit。

GitHub Actions 构建已成功：

```text
run id: 32287330034
```

产物已下载到：

```text
/root/split8-artifact/pal_probe
/root/split8-artifact/split8
```

`pal_probe` 是 Android arm64 可执行文件，尚未在设备运行。不要把 CI 构建成功误认为 PAL 8ch 已经成功。

## 桥接恢复后的下一步

### 1. 先确认设备

```sh
/root/android_bridge.sh "echo ALIVE; id; getprop sys.boot_completed; getprop ro.product.model; getprop ro.product.device"
```

只记录输出，不根据 ROM 的 Xiaomi/piano 属性改变硬件身份判断。

### 2. 上传静音探针

推荐使用 base64，避免桥接对二进制传输造成问题：

```sh
base64 -w0 /root/split8-artifact/pal_probe | \
  /root/android_bridge.sh 'base64 -d > /data/local/tmp/pal_probe && chmod 755 /data/local/tmp/pal_probe'
```

### 3. 执行一次探针

```sh
BRIDGE_TIMEOUT=30 /root/android_bridge.sh '\
logcat -c; \
timeout 15 /data/local/tmp/pal_probe 2>&1; \
rc=$?; echo RC=$rc; \
echo LOG; \
logcat -d -b all -t 500 2>/dev/null | grep -Ei "PAL:|SessionAlsaPcm|allocateFrontEnd|Opening PCM|channel map|slot.mask|speaker" | tail -220'
```

注意：当前媒体会话可能仍在占用 speaker。若 `pal_stream_open` 返回 busy/资源冲突，只记录结果，不重启 framework、不 stop/start audio 服务。必要时等现有播放自然停止后再执行一次。

### 4. 结果判定

重点记录：

```text
pal_init=...
pal_stream_open=...
pal_stream_start=...
pal_stream_write=...
```

判定：

- `open/start/write` 均成功：说明可以把 split8 的分频输出从 tinyalsa 迁移到 PAL 直连。
- `open` 成功、`start` 失败：重点查设备配置、speaker backend、资源冲突和 PAL channel map。
- `open` 失败：重点查设备 vendor PAL ABI、二改包的 audio XML 与开源 PAL 版本差异。
- `write` 失败：抓 PAL 日志和 `libar-pal` 的 buffer size/config 要求，不再回到 PCM 24 直接 WRITEI。

## 当前不要做的事情

- 不要上传或运行带非零音频的测试版本。
- 不要直接修改 `resourcemanager_sun_mtp.xml`、mixer paths、V4A 或功放映射。
- 不要修改 audio HAL 服务配置。
- 不要 stop/start `vendor.audio-hal-aidl`、`audioserver` 或其他系统服务。
- 不要尝试通过启用 `aw_dev_*` mixer 控件来替代 PAL graph。
- 不要提交 `/root/split8/reverse/libar-pal.so`。

## 交接时的当前状态

- 本地仓库工作区应干净。
- bridge 在本次生成文档前连续 20 次等待仍然 `ConnectionRefused`。
- `pal_probe` 已构建，尚未上机。
- 核心判断是：PCM 24 不能作为独立可写的 8ch 用户态端点，下一步验证 PAL 直连探针。
