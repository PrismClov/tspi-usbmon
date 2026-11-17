# Linux 主机端（USB DRM 驱动）

Linux 主机使用 [panyihang/USB-DRM-udisp-driver](https://github.com/panyihang/USB-DRM-udisp-driver)，
它把本设备枚举成一块标准 DRM/KMS 显示器（含 fbdev 模拟），GNOME/KDE/Sway/X11 都能直接识别。

## 1. 安装驱动

本仓库已内置驱动源码（`udisp_drm/`，来自
[panyihang/USB-DRM-udisp-driver](https://github.com/panyihang/USB-DRM-udisp-driver)，GPL-2.0），
一条命令完成编译 + 安装 + 加载：

```bash
sudo ./setup-udisp-driver.sh
```

脚本会自动装 `linux-headers-$(uname -r)` 和 `build-essential`（如果缺），
编译 `udisp_drm.ko`、安装 modprobe 配置并加载。

安装脚本会把 `udisp_drm.ko` 放到 `/lib/modules/$(uname -r)/extra/` 并执行 `depmod`。

### 本地补丁（相对上游）

`udisp_drm/src/udisp_drm_usb.c` 带两处针对 1080×2160 大帧的修改，已在文件里用
`Local patch (tspi-usbmon)` 注释标出：

1. **TX 槽自动降级**：设备上报的 `Bl` 是 4 MiB 时，上游会 `kmalloc` 4 个 4 MiB 缓冲区，
   在碎片化的系统上 probe 直接 `-ENOMEM`。现在按 2 的幂递减到能分配成功的大小，并把
   `frame_limit` 同步收紧（编码器会自动降质量，不会产生发不出去的帧）。
2. **旋转暂存改 `vmalloc`**：该缓冲只做 CPU 侧拼图、不参与 DMA，避免高 order `kmalloc` 失败。

## 2. 加载参数

设备原生是 **竖屏 1080×2160**，Linux 驱动默认 `rotate=90`，即：

* 主机侧看到 **2160×1080 横屏** 显示器；
* 驱动在发送前把桌面顺时针旋转成 1080×2160 的 JPEG；
* 设备端 `rotate = 0`，直接 1:1 上屏，解码后不需要再做旋转（这是最省 CPU 的用法）。

推荐配置：

```bash
sudo tee /etc/modprobe.d/udisp_drm.conf >/dev/null <<'EOF'
options udisp_drm debug=0 connector_type=usb host_native=0 rotate=90 \
                  jpeg_quality=6 keepalive_ms=10000
EOF
```

| 参数 | 说明 |
| --- | --- |
| `rotate=90` | 主机帧缓冲横屏，发送前转成竖屏。想竖着用面板就改成 `host_native=1 rotate=0`。 |
| `jpeg_quality` | 1..10，不填则使用设备上报的 `Ejpg`（本设备默认 6）。画质优先调高，帧率优先调低。 |
| `keepalive_ms` | 静态画面下每隔多久重发一帧，避免设备侧误判。 |
| `connector_type` | 建议 `usb`；有些桌面环境对 `usb` 类型的连接器识别更好。 |
| `debug=1` | 打印 `frame stats`（convert/encode/usb/fps），调优时打开。 |

改完 `sudo reboot`（或 `sudo modprobe -r udisp_drm && sudo modprobe udisp_drm`）。
建议在图形会话启动前加载，避免 Mutter 缓存过热插拔状态。

## 3. 自动加载

驱动的 USB ID 表里已经有 `303a:2986`，正常插上就会通过 modalias 自动加载。
如果桌面环境启动较早没认到，加一条 udev 规则：

```bash
sudo install -m644 99-udisp.rules /etc/udev/rules.d/99-udisp.rules
sudo udevadm control --reload
```

## 4. 显示设置

* GNOME：`设置 → 显示器`，会多出一块 `USB` 显示器，拖到右侧即可扩展。
* KDE：`系统设置 → 显示和监视器`。
* Sway/wlroots：`swaymsg output USB-1 enable position 1920 0`（名字用 `swaymsg -t get_outputs` 查）。
* X11：`xrandr --output USB-1 --right-of eDP-1`（`xrandr` 里名字可能是 `USB-1`）。

## 5. 触摸

设备会枚举成一个 5 点 HID 数字转换器（touchscreen）。绝大多数桌面会自动把它绑定到
USB 显示器上。X11 下若映射错屏幕：

```bash
xinput list                                  # 找到 "tspi" / "Touch Screen"
xinput map-to-output <id> USB-1
```

## 6. 验证与排查

```bash
dmesg | grep -i udisp
# display info: product='tspi_R1080x2160_Ejpg6_Fps30_Bl4194304' ... dev=1080x2160 host=2160x1080 ...
lsmod | grep udisp
ls /sys/class/drm/                           # 应出现 card1-USB-1 之类
```

设备端确认收到画面：

```bash
journalctl -u tspi-usbmon -f
# 正常时每 5 秒一行： usb=1 frames=... (29.x fps) avg=... ms ... MB/s
```

如果 `frames` 一直是 0：先看设备端 `cat /sys/kernel/config/usb_gadget/tspi_udisp/UDC` 是否有值，
再看主机 `lsusb | grep 303a` 是否出现 `303a:2986`。
