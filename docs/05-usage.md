# 05 主机端使用

设备端只是“把 JPEG 显示出来 + 把触摸转发回去”，真正决定体验的是主机侧怎么用。
下面按系统给出推荐配置；驱动安装细节见
[host/windows/README.md](../host/windows/README.md) 与
[host/linux/README.md](../host/linux/README.md)。

## 1. 两种摆放方式

| 场景 | 设备配置 | 主机看到 |
| --- | --- | --- | --- |
| Linux（驱动默认 `rotate=90`） | `stream=1080x2160` `rotate=0` `host_rotate=90` | 2160×1080 横屏 |
| Linux（`host_native=1 rotate=0`） | `stream=1080x2160` `rotate=0` `host_rotate=0` | 1080×2160 竖屏 |
| Windows 横放 | `stream=2160x1080` `rotate=90` `host_rotate=0` | 2160×1080 横屏 |
| Windows 竖放 | `stream=1080x2160` `rotate=0` `host_rotate=0` | 1080×2160 竖屏 |

`rotate` 是设备端旋转，`host_rotate` 是主机驱动的旋转（只影响触摸和 HID 描述符），
两者必须与主机实际配置一致，否则触摸会偏移或镜像。

Linux 还有第三种等价写法：`stream=1080x2160` + 驱动 `rotate=90`。此时**旋转由主机
完成**，设备端 `rotate=0`，CPU 最省。两种写法二选一即可：

```ini
# Linux 推荐：主机旋转，设备零旋转
stream = 1080x2160
rotate = 0
host_rotate = 90
# /etc/modprobe.d/udisp_drm.conf: options udisp_drm rotate=90 host_native=0
```

```ini
# Windows 推荐：设备自己转
stream = 2160x1080
rotate = 90
host_rotate = 0
```

## 2. Windows

1. 安装签名 IDD 驱动
   （[下载](https://dl.espressif.com/AE/esp-iot-solution/xfz1986_usb_graphic_250224_rc_sign.exe)），重启；
2. `设置 → 系统 → 显示`，选中新显示器 → **扩展这些显示器**；
3. 拖动图标与实际位置一致；
4. 缩放建议 150%~200%；
5. 触摸：控制面板 → **平板电脑设置** → 配置 → 点一下副屏。

细节与排错见 [host/windows](../host/windows/README.md)。

## 3. Linux

```bash
git clone https://github.com/panyihang/USB-DRM-udisp-driver.git
cd USB-DRM-udisp-driver && make && sudo make install
sudo cp <repo>/host/linux/udisp_drm.conf /etc/modprobe.d/
sudo reboot
```

之后：

* GNOME / KDE：显示设置里出现一块 USB 显示器，直接拖拽排列；
* Sway：`swaymsg -t get_outputs` 找到 `USB-1`，`swaymsg output USB-1 position 1920 0`；
* X11：`xrandr --output USB-1 --right-of eDP-1`；
* 触摸：一般自动绑定；X11 下 `xinput map-to-output <id> USB-1`。

细节见 [host/linux](../host/linux/README.md)。

## 4. 触摸校准

触摸不需要 `xinput-calibrate` 之类的工具，因为：

1. 设备从 evdev 读到的是**面板物理坐标**（1080×2160，由 `sec,max_coords` 决定）；
2. 设备用**和画面完全相同的几何变换**做逆变换，得到主机帧缓冲坐标；
3. HID 报告直接给出主机坐标系下的 X/Y。

所以只要 `stream`/`rotate`/`host_rotate` 三者改对，触摸就是准的。若出现“左右反”“上下反”，说明
`rotate` 与实际摆放差了 180°，把 `rotate` 加 180 再试。

如果触摸只作用在主屏：Windows 用“平板电脑设置”指定屏幕；X11 用
`xinput map-to-output`；Wayland 一般按显示器自动绑定，可在
`gnome-control-center` 里检查“触摸屏”映射。

## 5. 多屏

* 一个主机可以接多块同类副屏（每块一个 USB 口、独立供电）；
* 驱动不会把多块屏拼成一个大桌面，每块都是独立显示器；
* 每块屏的 `stream` 可以不同（各自配置文件），主机侧按 EDID 分别识别。

## 6. 日常使用建议

| 场景 | 建议 |
| --- | --- |
| 看文档/终端 | 2160×1080，`jpeg_quality=7`，`fps=30`，文字更锐 |
| 看视频/演示 | `jpeg_quality=5`，`fps=30`，优先流畅 |
| 当监控/状态屏 | 竖放，720×1440 流（设备端放大），CPU 占用最低 |
| 演示给别人看 | 横放 2160×1080，Windows 缩放 200%，字体大 |

## 7. 与主屏的配合

副屏最常见的用法：

* 聊天/邮件/参考文档放副屏，主屏专注编码；
* 竖屏放终端 + 日志；
* OBS 预览、股票/监控面板；
* 笔记本合盖时当唯一屏幕（需要主机支持，且供电要够）。

副屏的 `fps` 只影响“画面变化时”的发送频率，静态画面下主机驱动只在
`keepalive_ms`（默认 10 s）时重发一帧，USB 几乎不占带宽。
