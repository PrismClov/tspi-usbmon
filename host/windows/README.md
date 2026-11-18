# Windows 主机端（IDD 驱动）

Windows 使用乐鑫 esp-iot-solution 里的签名版 IDD 驱动，设备和它之间的协议与
ESP32-P4 官方示例完全一致（VID/PID `303a:2986`、16 字节帧头、JPEG 负载、5 点触摸 HID）。

## 1. 安装驱动

下载并安装官方签名版：

<https://dl.espressif.com/AE/esp-iot-solution/xfz1986_usb_graphic_250224_rc_sign.exe>

安装后建议重启一次。设备管理器里应出现：

* `显示适配器` 下多一个 USB 显示设备；
* `人体学输入设备` 下多一个 HID 兼容触摸屏。

> 驱动只支持 Windows 10 / 11。自己编译驱动需要开测试模式，参考
> [windows_driver/README_cn.md](https://github.com/espressif/esp-iot-solution/blob/master/examples/usb/device/usb_extend_screen/windows_driver/README_cn.md)。

## 2. 横屏 / 竖屏

Windows 驱动不做旋转：它把 `R<宽>x<高>` 当成显示器分辨率，画面原样发下来。
天马这块面板原生是 **1080×2160 竖屏**，所以有两种用法：

### 横着摆（推荐，2160×1080 桌面）

板子上改 `/etc/tspi-usbmon.conf`：

```ini
stream = 2160x1080
rotate = 90
host_rotate = 0
```

设备会先声明 2160×1080 的横向显示器，收到画面后在设备端顺时针旋转 90° 送屏。
改完 `sudo systemctl restart tspi-usbmon`，再在 Windows 里重新插拔一次 USB。

### 竖着摆（1080×2160 桌面）

```ini
stream = 1080x2160
rotate = 0
host_rotate = 0
```

这时设备端零旋转，CPU 占用最低。

### 非 18:9 的分辨率

比如想用 1920×1080：

```ini
stream = 1920x1080
rotate = 90
host_rotate = 0
```

设备端旋转后是 1080×1920，面板上下各留 120 像素黑边（等比缩放、居中，不会拉伸）。

## 3. 显示设置

`设置 → 系统 → 显示`：

1. 选中新出现的显示器，选择 **扩展这些显示器**；
2. 拖动图标到主屏的右侧/左侧，与实际摆放一致；
3. 6 寸 1080p 面板约 403 ppi，建议缩放 **150% ~ 200%**；
4. 若想让副屏成为主屏，勾选“使之成为我的主显示器”。

## 4. 触摸

Windows 10/11 会自动把 HID 数字转换器当作触摸屏。第一次使用建议校准映射：

1. 控制面板 → **平板电脑设置** → *配置* → **设置**；
2. 提示“触摸此屏幕”时，点一下 USB 副屏；
3. 如果触摸落到了主屏，重复一次并注意点的是副屏。

> 固件必须正确返回 HID Feature Report 2（Contact Count Maximum = 5），
> 否则 Windows 不会启用触摸。本项目已按官方固件返回 `{0x02, 0x05}`。

## 5. 常见问题

| 现象 | 处理 |
| --- | --- |
| 设备管理器里是“未知 USB 设备 (设备描述符请求失败)” | 多半是供电不足或线材问题，见根目录 README 的供电章节；换 USB3 口 / 带电源 HUB |
| 装了驱动但没有第二个显示器 | 设备端 `journalctl -u tspi-usbmon` 看是否 `usb=1` 且 `frames` 在涨；主机 `设备管理器 → 显示适配器` 是否报错 |
| 画面卡顿 / 撕裂 | 降低 `jpeg_quality`（如 4）、把 `fps` 降到 24、确认走的是 USB3 口 |
| 画面糊 | 提高 `jpeg_quality` 到 8；确认 `stream` 与面板比例一致 |
| 触摸没反应 | 设备端日志有没有 `USB enabled`；`dmesg` 是否有 HID 枚举；重新执行第 4 步 |
| 插上后板子反复重启 | 供电不足（PC 口 500mA 不够），必须外接 5V |

## 6. 多屏

驱动支持多个同类设备，但每个设备需要独立的 USB 口和供电；多块屏之间不能拼接成一个大桌面。

## 7. 1080p 提前通知驱动

在上游源码 chuanjinpang/win10_idd_xfz1986_usb_graphic_driver_display 的提交
`c2de0f56f778904d663d8e2dc5a71b4a6b270ecc` 上应用本目录的
`early-notify.patch`（在上游仓库根目录执行 `git apply <补丁路径>`）。
补丁复用 staging 纹理，在提交 GPU 复制后通知 IddCx，再回读和编码。
诊断级别 4 的 FRAME 日志关联 Windows 帧号和协议中的 10 位帧号。
