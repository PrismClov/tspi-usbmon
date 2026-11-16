# 04 编译、部署与配置

## 1. 依赖

| 依赖 | 用途 | Ubuntu 包 |
| --- | --- | --- |
| libdrm | DRM/KMS 输出 | `libdrm-dev` |
| libjpeg-turbo（turbojpeg） | JPEG 解码 | `libturbojpeg0-dev` |
| pthread / libm | 线程、数学 | `libc6-dev` |
| gcc / make | 编译 | `build-essential` |

```bash
sudo apt install -y build-essential libdrm-dev libturbojpeg0-dev pkg-config
```

运行时还需要：内核开启 configfs + FunctionFS（见 03 文档）、`/dev/dri/card0`、
`/sys/class/udc/*`。服务以 root 运行（创建 gadget、拿 DRM master 都需要）。

## 2. 编译

### 在板子上编译（推荐）

```bash
./scripts/build.sh --test     # 先跑单元测试，再编译
# 产物：build/tspi-usbmon
```

### 交叉编译

需要 aarch64 工具链 + 带 `libdrm` / `libturbojpeg` 的 sysroot：

```bash
CROSS_COMPILE=aarch64-linux-gnu- SYSROOT=/path/to/sysroot ./scripts/build.sh
```

如果只是链接错误（找不到 `-ldrm` / `-lturbojpeg`），说明 sysroot 里没有对应的
`.so` 或头文件；把 `libdrm-dev`、`libturbojpeg0-dev` 的 arm64 包解到 sysroot 里即可。

### 单元测试

```bash
make test        # 协议解析、HID 描述符、几何/旋转、合成、配置解析
make stubs-check # 无 libdrm/turbojpeg 的机器上做语法检查（用 tests/stubs）
```

`tests/stubs/` 里的头文件只用于离线语法检查，**不要**用于真实构建。

## 3. 安装

```bash
sudo ./scripts/install-on-board.sh
```

安装内容：

| 路径 | 说明 |
| --- | --- |
| `/usr/local/bin/tspi-usbmon` | 主程序 |
| `/etc/tspi-usbmon.conf` | 配置（已存在则保留） |
| `/usr/share/tspi-usbmon/splash.jpg` | 待机图 |
| `/etc/systemd/system/tspi-usbmon.service` | 开机自启 |

从开发机远程部署：

```bash
BOARD=neons@192.168.1.50 ./scripts/deploy.sh
```

## 4. 运行与日志

```bash
systemctl status tspi-usbmon
journalctl -u tspi-usbmon -f
```

正常日志（每 5 秒一行统计）：

```
21:33:12.101 I tspi-usbmon 1.0.0 starting
21:33:12.115 I using DRM device /dev/dri/card0
21:33:12.180 I connector DSI-1 mode 1080x2160@59 crtc 61
21:33:12.201 I panel 1080x2160, stream 1080x2160, rotate 0, dest 1080x2160+0+0
21:33:12.230 I using UDC 'fe800000.usb'
21:33:12.240 I gadget bound to fe800000.usb
21:33:15.001 I USB enabled
21:33:20.004 I usb=1 frames=148 (29.6 fps) failed=0 avg=21.3 ms 7.42 MB/s
```

手动前台调试（不经过 systemd）：

```bash
sudo systemctl stop tspi-usbmon
sudo /usr/local/bin/tspi-usbmon -c /etc/tspi-usbmon.conf -v -v
```

命令行参数：

```
-c, --config <path>   配置文件（默认 /etc/tspi-usbmon.conf）
-s, --set <k=v>       临时覆盖一项配置，可重复
-v, --verbose         详细日志（-v -v 打开 debug）
-V, --version
-h, --help
```

例：临时用横屏模式跑一次

```bash
sudo tspi-usbmon -s stream=2160x1080 -s rotate=90 -v
```

## 5. 配置项

| 键 | 默认 | 说明 |
| --- | --- | --- |
| `stream` | `1080x2160` | 上报给主机的画面尺寸。Linux 横屏用 1080x2160（驱动自己转），Windows 横屏用 2160x1080 |
| `rotate` | `0` | 设备端把收到的画面顺时针旋转多少度后上屏 |
| `host_rotate` | `90` | 主机驱动发送前的旋转量：Linux 默认 `rotate=90` → 90；Windows / `host_native=1` → 0。只影响触摸坐标与 HID 描述符 |
| `fps` | `30` | 上报刷新率（影响主机 EDID / 发送节流） |
| `jpeg_quality` | `6` | 上报 JPEG 质量 1..10 |
| `max_frame_kb` | `4096` | 单帧上限；同时作为 `Bl` 上报 |
| `card` | `/dev/dri/card0` | DRM 设备 |
| `connector` | `auto` | `auto` 优先找 DSI；也可写 `DSI-1` 或连接器 id |
| `mode` | `auto` | `auto` 用首选模式；也可写 `1080x2160` |
| `brightness` | `200` | 背光 0..255；`-1` 表示不动 |
| `touch` | `auto` | 触摸设备路径；`auto` 扫描 `/dev/input/event*` |
| `udc` | `auto` | UDC 名；`auto` 取 `/sys/class/udc` 第一个 |
| `takeover_udc` | `0` | 为 1 时自动解绑占用 UDC 的其它 gadget（如 adbd） |
| `vid` / `pid` | `0x303a` / `0x2986` | **必须保持**，主机驱动按它匹配 |
| `product` | `auto` | 自动生成带 `R...Ejpg...Fps...Bl...` 的产品字符串 |
| `interface` | `auto` | 同上，接口字符串 |
| `splash` | `/usr/share/tspi-usbmon/splash.jpg` | 待机图 |
| `verbose` | `1` | 0/1/2 |

### 几何规则

`stream` → 旋转 → 等比缩放到面板并居中，多余部分黑边：

```
rot=0   : dst = stream（1:1）
rot=90  : stream 1080x2160 → 旋转后 2160x1080 → 放到 1080x2160 面板上会缩成 1080x540
rot=90  : stream 2160x1080 → 旋转后 1080x2160 → 正好铺满
```

所以 `rotate` 要和你选的 `stream` 配套：**横屏桌面就声明横屏尺寸再转 90°**。

### 触摸映射

触摸坐标按“面板像素 → 逆设备变换 → stream → 逆 host_rotate → host 帧缓冲”换算，
因此 `rotate`、`host_rotate`、`stream` 改动后触摸自动跟着对齐，不需要额外校准。
唯一例外是面板被物理倒装（`rotate=180`）时需要确认 `sec,max_coords` 的方向。

匹配矩阵（常见组合）：

| 场景 | `stream` | `rotate` | `host_rotate` | 主机看到 |
| --- | --- | --- | --- | --- |
| Linux，默认驱动 `rotate=90` | 1080x2160 | 0 | 90 | 2160×1080 |
| Linux，`host_native=1 rotate=0` | 1080x2160 | 0 | 0 | 1080×2160 |
| Windows，横放 | 2160x1080 | 90 | 0 | 2160×1080 |
| Windows，竖放 | 1080x2160 | 0 | 0 | 1080×2160 |

## 6. 升级

```bash
# 只换二进制
scp build/tspi-usbmon neons@<board>:/tmp/
ssh neons@<board> 'sudo systemctl stop tspi-usbmon && \
  sudo install -m755 /tmp/tspi-usbmon /usr/local/bin/ && \
  sudo systemctl start tspi-usbmon'
```

或者重新 `./scripts/deploy.sh`（会保留 `/etc/tspi-usbmon.conf`）。

## 7. 卸载

```bash
sudo systemctl disable --now tspi-usbmon
sudo make uninstall
```

gadget 目录会留在 `/sys/kernel/config/usb_gadget/tspi_udisp`（重启自动消失）；
`/dev/ffs-udisp` 的 functionfs 挂载可以用 `sudo umount /dev/ffs-udisp` 清掉。
