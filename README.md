# tspi-usbmon

把 **立创泰山派 RK3566** + **天马 TL060FVXS07/08 6.0" 1080×2160 MIPI 屏** 做成一块
**USB 副屏**：一根 USB 线插到电脑上，Windows 或 Linux 就多出一块支持 5 点触摸的
扩展显示器，板子由这根线供电（建议外加 5V）。

本项目复用乐鑫 [esp-iot-solution usb_extend_screen](https://github.com/espressif/esp-iot-solution/tree/master/examples/usb/device/usb_extend_screen)
的 **udisp 协议**，因此主机端不需要写任何驱动：

* **Windows**：官方签名 IDD 驱动（`303a:2986`），装完即用；
* **Linux**：`udisp_drm.ko`，标准 DRM/KMS 显示器，GNOME/KDE/Sway/X11 直接识别。

设备端通过 FunctionFS 接收逐帧 JPEG。RK3566 加速构建使用 MPP 硬解和 RGA
旋转缩放，通用构建使用 libjpeg-turbo 软件解码。两者均通过 DRM/KMS 驱动 1080×2160 MIPI-DSI 面板。

```
┌──────────────┐   USB 2.0 HS (bulk)   ┌───────────────────────────────┐
│  Windows IDD │ ──── JPEG 帧 + 帧头 ──▶│ FunctionFS  ffs.udisp          │
│  或 Linux    │                        │  ep1 OUT ──▶ 帧重组 ──▶ JPEG 解码│
│  udisp_drm   │ ◀── HID 触摸报告 ───── │  ep3 IN  ◀── 触摸映射           │
└──────────────┘                        │            │                   │
                                        │            ▼                   │
                                        │  DRM/KMS 三缓冲 page flip      │
                                        │            │                   │
                                        │  MIPI-DSI1 4 lane 1080×2160    │
                                        └───────────────────────────────┘
```

## 特性

* 原生 1080×2160@~60Hz 面板输出，JPEG 流实时解码上屏
* 设备端旋转（0/90/180/270）+ 等比缩放/黑边，适配横竖摆放
* 5 点电容触摸反向转发（HID digitizer，含 Windows 需要的 Feature Report 2）
* 无主机时显示待机图，插线自动切换
* 主机端零改动：兼容 ESP32-P4 官方 IDD 驱动与开源 Linux DRM 驱动
* 配置化：分辨率、旋转、帧率、JPEG 质量、背光、连接器都可调

## 硬件清单

| 部件 | 说明 |
| --- | --- |
| 立创泰山派 RK3566（2G/4G 均可） | 运行 Ubuntu 22.04（[tspi-linux-sdk](https://github.com/CmST0us/tspi-linux-sdk)，内核 5.10.198） |
| 天马 TL060FVXS07 / TL060FVXS08 6.0" 1080×2160 MIPI 屏 | 4 lane DSI + incell 触摸 |
| [mipi-hub](https://gitee.com/opengisbook/mipi-hub-official) 泰山派转接板 | 31pin 0.3mm + 6pin 0.5mm 反向 FPC |
| USB 数据线 + **外接 5V** | 电脑 USB 口 500mA 不够点亮背光，见 [硬件说明](docs/02-hardware.md) |

## 快速开始

```bash
# 0) 宿主机：准备 SDK
git clone --recursive https://github.com/CmST0us/tspi-linux-sdk.git
export SDK_DIR=$PWD/tspi-linux-sdk

# 1) 内核：面板 DTS + 触摸驱动 + 配置片段（可加 --build-dtb 顺手编 DTB）
./scripts/kernel-integrate.sh
cd $SDK_DIR && ./build.sh tspi-rk3566-ubuntu-panfrost_defconfig && ./build.sh
# 用 rkflash.sh 烧录 update.img，详见 docs/03-kernel.md

# 2) 应用：建议直接在板子上编译
scp -r . neons@<板子IP>:~/tspi-usbmon
ssh neons@<板子IP>
sudo apt install -y build-essential libdrm-dev libturbojpeg0-dev pkg-config
cd ~/tspi-usbmon && ./scripts/build.sh --test
sudo ./scripts/install-on-board.sh

# 3) 主机：Windows 装 IDD 驱动；Linux 编 udisp_drm.ko
#    见 host/windows/README.md / host/linux/README.md

# 4) 插上 USB，副屏出现
journalctl -u tspi-usbmon -f      # 设备端应有 usb=1 frames=... fps
```

## 文档

| 文档 | 内容 |
| --- | --- |
| [01 架构与协议](docs/01-architecture.md) | 数据流、16 字节帧头、接口字符串、线程模型 |
| [02 硬件与接线](docs/02-hardware.md) | 屏参、转接板、FPC 方向、供电预算 |
| [03 内核适配](docs/03-kernel.md) | DTS、触摸驱动、内核配置、编译烧录 |
| [04 编译与部署](docs/04-build-and-deploy.md) | 交叉编译、systemd、配置项全表 |
| [05 主机端使用](docs/05-usage.md) | Windows/Linux 预设、触摸校准、多屏 |
| [06 性能与调优](docs/06-performance.md) | 带宽/解码预算、参数怎么调 |
| [07 故障排查](docs/07-troubleshooting.md) | 黑屏、不枚举、触摸错位 |
| [08 备选方案](docs/08-alternatives.md) | USB 网卡 + 远程桌面、ESP32-P4 参考方案 |
| [09 Buildroot 实测](docs/09-buildroot-bringup.md) | 泰山派 Buildroot 镜像上的实测、android_usb 限制、attach 模式 |

## 目录结构

```
src/                设备端守护进程
  gadget.c          configfs + FunctionFS，收帧 / 发 HID
  proto.c           16 字节帧头解析与重同步
  jpegdec.c         MPP 硬解到 DMA-BUF，或 libjpeg-turbo 解码到 BGRX
  geom.c            旋转 / 缩放 / 黑边 / 触摸逆映射
  display.c         DRM/KMS 三缓冲 page flip
  touch.c           evdev 多点触摸 → HID 报告
kernel/dts/         天马屏 DTS（替换 SDK 的 dsi dtsi）
kernel/configs/     内核配置片段
scripts/            内核集成、编译、部署、待机图生成
deploy/             systemd 单元与默认配置
host/               Windows / Linux 主机端说明
tests/              协议、HID、几何、合成的单元测试
```

## 与参考项目的区别

| | 参考项目（oshwhub 6.2" USB 副屏） | 本项目 |
| --- | --- | --- |
| 主控 | RK3506G2 | 泰山派 RK3566 |
| 屏幕 | 1280×452 MIPI（2 lane） | 天马 1080×2160（4 lane） |
| 设备端渲染 | LVGL + DRM | 无 UI 框架，JPEG → DRM 直出 |
| 主机协议 | udisp（ESP32-P4 兼容） | **相同**，主机驱动原样可用 |
| 触摸 | axs15260 | Samsung sec_ts（incell） |
| 待机画面 | LVGL 页面 + 视频 | 单张 JPEG（可替换） |

## 状态说明

* 设备端程序 `make test` 的协议/HID/几何/合成单元测试全部通过；全部源码在
  `-Wall -Wextra` 下可编译。
* 内核 DTS 基于 [mipi-hub](https://gitee.com/opengisbook/mipi-hub-official) 中已被
  验证过的泰山派适配（作者 sigmenx），本项目修掉了其中 `brightness-levels`
  的语法错误并显式设置 `dr_mode = "peripheral"`。
* Windows 主机连接 RK3566 的 USB 副屏已在实机验证。首次点亮可参考[故障排查](docs/07-troubleshooting.md)。

## 解码规格与副屏实测

瑞芯微为 RK3566 标注的 **4K@60 fps 视频解码**适用于 H.265、H.264 和 VP9。
该项规格没有给出逐帧 JPEG 的帧率。[瑞芯微 RK3566 产品页](https://www.rock-chips.com/a/cn/product/RK35xilie/2021/0113/1273.html)

本项目通过 USB 2.0 Bulk 传输逐帧 JPEG。在 Windows 提前通知驱动、1920×1080 输入、
JPEG 质量 70 的动态画面测试中，副屏在连续 60 秒内实际呈现了 3,323 张不同画面，
平均 **55.38 fps**，逐秒为 **46～60 fps**。这里按面板翻转完成计数，包含从主机供帧到显示的整条链路。[实测方法与结果](docs/06-performance.md)

## 已知限制

* Windows 横放实测配置为 `stream=1920x1080`、`rotate=90`、`host_rotate=0`，见 [1080p 配置](deploy/1080p60/tspi-usbmon.conf)。
* 必须重新编译内核（面板 DTS + 触摸驱动 + gadget 配置），不能只装应用；
* 官方镜像的 **adb 会占用 UDC**，需要先停掉或设置 `takeover_udc = 1`；
* **供电**：PC 单个 USB 口通常不够，强烈建议带电源 HUB / 外接 5V；
* 图形桌面会抢占 DRM master，作为副屏使用时建议关闭显示管理器。

## 参考

* 协议与主机驱动：<https://github.com/espressif/esp-iot-solution/tree/master/examples/usb/device/usb_extend_screen>
* Linux DRM 主机驱动：<https://github.com/panyihang/USB-DRM-udisp-driver>
* 屏幕适配与 DTS：<https://gitee.com/opengisbook/mipi-hub-official>
* 参考的 USB 副屏项目：<https://oshwhub.com/an_ye/project_wgtodlyq>
* 泰山派 Linux SDK：<https://github.com/CmST0us/tspi-linux-sdk>

## License

MIT（`src/`、`scripts/`、`docs/`）。内核 DTS 与触摸驱动沿用其上游 GPL-2.0 许可，
`kernel/dts/tspi-rk3566-dsi-v10-tianma.dtsi` 以 GPL-2.0 分发。
