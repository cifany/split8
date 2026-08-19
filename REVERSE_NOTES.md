# 当前设备状态（供下一个模型继续）

## 设备
- OnePlus Pad 2 Pro / OPD2413 / 25091RP04C / Android 16 / KernelSU

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
- Android HAL 播 2ch 正常，说明设备 24 能写，只是不允许普通用户直接写 8ch
- 下一步逆向方向：
  1. 抓 audiohalservice.qti 对 PCM 24 的完整配置序列
  2. 查 HAL 是否设置隐藏 mixer 控件/TDM 槽位/通道映射
  3. 确认是否需要用 SNDRV_PCM_IOCTL_WRITEN_FRAMES 或 mmap
  4. 反编译 vendor audio HAL 的 speaker 后端配置
