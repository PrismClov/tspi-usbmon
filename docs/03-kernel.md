# 03 内核适配（泰山派 / RK3566 / Linux 5.10）

目标：让内核把天马屏点亮（`/dev/dri/card0` 上出现 `DSI-1` 连接器），并让 incell
触摸出现在 `/dev/input/event*`。应用层（本项目）只依赖这两件事。

## 1. 基线

* SDK：[CmST0us/tspi-linux-sdk](https://github.com/CmST0us/tspi-linux-sdk)
  （Rockchip `rk4.1-202305`，kernel **5.10.198**，Ubuntu 22.04 rootfs）
* 板级 defconfig：`tspi-rk3566-ubuntu-panfrost_defconfig`
* 设备树：`kernel/arch/arm64/boot/dts/rockchip/tspi-rk3566-user-v10-linux.dts`

> 如果你用的是立创官方 Ubuntu 镜像而不是这个 SDK，内核/DTB 版本对不上，最稳妥的
> 做法仍是按本文重新编一版内核。

## 2. 一键集成

```bash
export SDK_DIR=$HOME/tspi-linux-sdk
./scripts/kernel-integrate.sh            # 只改源码
./scripts/kernel-integrate.sh --build-dtb  # 顺带编译 DTB 验证语法
```

脚本会做四件事（每步都可重复执行）：

1. 把 `kernel/dts/tspi-rk3566-dsi-v10-tianma.dtsi` 覆盖到
   `kernel/arch/arm64/boot/dts/rockchip/tspi-rk3566-dsi-v10.dtsi`（原文件备份为 `.orig`）；
2. 把 `tspi-rk3566-user-v10-linux.dts` 里被注释掉的
   `// #include "tspi-rk3566-dsi-v10.dtsi"` 打开；
3. 把 `my_sec_ts/` 触摸驱动拷进 `drivers/input/touchscreen/`，并在该目录的
   `Kconfig` / `Makefile` 里挂上 `CONFIG_TOUCHSCREEN_SEC_TS_1223`；
4. 安装 `kernel/configs/tspi-usbmon.config`，并把它追加到
   `device/rockchip/.chips/rk3566_rk3568/tspi-rk3566-*_defconfig` 的
   `RK_KERNEL_CFG_FRAGMENTS`。

触摸驱动默认从 gitee 稀疏克隆（约 400 MB，仅首次）；已有仓库时可直接指定：

```bash
MIPI_HUB_DIR=~/mipi-hub-official ./scripts/kernel-integrate.sh
```

## 3. 手动步骤（脚本之外想自己改时看）

### 3.1 打开 DSI 节点

`tspi-rk3566-user-v10-linux.dts` 里这一行默认是注释的：

```dts
// #include "tspi-rk3566-dsi-v10.dtsi"
```

改成：

```dts
#include "tspi-rk3566-dsi-v10.dtsi"
```

### 3.2 DTS 关键点

`kernel/dts/tspi-rk3566-dsi-v10-tianma.dtsi` 主要内容：

| 节点 | 作用 |
| --- | --- |
| `backlight` | PWM5 背光，`brightness-levels = 0..100`，`max_brightness = 100` |
| `&pwm5` | 打开 PWM5 |
| `&pinctrl` | 复位脚 GPIO3_C1、触摸中断 GPIO1_A0 |
| `&i2c1` + `touchscreen@48` | `sec,sec_ts`，`max_coords = 1080, 2160` |
| `&dsi1_in_vp1` / `&video_phy1` / `&route_dsi1` | 显示通路走 VP1 + DSI1 PHY |
| `&dsi1` + `panel@0` | `simple-panel-dsi`，4 lane、RGB888、video+burst、时序表 |
| `&usbdrd_dwc3` | `dr_mode = "peripheral"`，Type-C 固定为设备口 |

与原版 mipi-hub dtsi 的差异：

* 原文件 `brightness-levels` 里混进了 `tai` 这个非法 token，`dtc` 会直接报错；
* 原文件 `width-mm/height-mm = 135/216` 是 10 寸屏的尺寸，6 寸应为 68×136；
* 增加了 `dr_mode = "peripheral"`；
* 亮度表改成 0..100，方便应用按百分比写 sysfs。

### 3.3 触摸驱动

驱动来自 mipi-hub 的 `TL060FVXS07/泰山派/my_sec_ts/`，是 Samsung 的 `sec_ts`
内核驱动 + TL060FVXS07 的固件（`s6d6ft0_v1.10_20170918.i`，约 600 KB）：

```bash
cp -r my_sec_ts $SDK_DIR/kernel/drivers/input/touchscreen/
```

`drivers/input/touchscreen/Kconfig`：

```kconfig
if INPUT_TOUCHSCREEN
	source "drivers/input/touchscreen/my_sec_ts/Kconfig"
```

`drivers/input/touchscreen/Makefile`：

```make
obj-$(CONFIG_TOUCHSCREEN_SEC_TS_1223) += my_sec_ts/
```

然后在内核配置里打开：

```
CONFIG_TOUCHSCREEN_SEC_TS_1223=y
```

### 3.4 内核配置片段

`kernel/configs/tspi-usbmon.config` 里有四组配置：

```ini
# USB gadget / FunctionFS
CONFIG_USB_GADGET=y
CONFIG_USB_CONFIGFS=y
CONFIG_USB_CONFIGFS_F_FS=y
CONFIG_USB_F_FS=y
CONFIG_USB_DWC3=y
CONFIG_USB_DWC3_DUAL_ROLE=y
CONFIG_USB_DWC3_OF_SIMPLE=y

# DRM / DSI / 背光
CONFIG_DRM_ROCKCHIP=y
CONFIG_ROCKCHIP_DW_MIPI_DSI=y
CONFIG_DRM_PANEL_SIMPLE=y
CONFIG_BACKLIGHT_PWM=y
CONFIG_PWM_ROCKCHIP=y

# 触摸
CONFIG_INPUT_EVDEV=y
CONFIG_TOUCHSCREEN_SEC_TS_1223=y

# 双缓冲需要的连续内存
CONFIG_CMA=y
CONFIG_CMA_SIZE_MBYTES=64
```

这些符号大多在 `rockchip_linux_defconfig` 里已经 `=y`，片段只是兜底。编译前可以确认：

```bash
cd $SDK_DIR/kernel
grep -E "CONFIG_USB_CONFIGFS_F_FS|CONFIG_DRM_PANEL_SIMPLE|CONFIG_ROCKCHIP_DW_MIPI_DSI" .config
```

## 4. 编译与烧录

```bash
cd $SDK_DIR
./build.sh tspi-rk3566-ubuntu-panfrost_defconfig
./build.sh                 # 全量：uboot + kernel + rootfs + update.img
# 或只重编内核
./build.sh kernel
```

烧录（板子进 MASKROM：按住 MASKROM/BOOT 键再点复位，或空片首次上电自动进入）：

```bash
sudo ./rkflash.sh update          # 整包 update.img
sudo ./rkflash.sh                 # 分区小包
```

> 该 SDK 使用 FIT 启动镜像（`RK_USE_FIT_IMG=y`），DTB 打包在 `boot.img` 里，
> **不能**简单替换 `/boot` 下的 dtb 文件；改完 DTS 请重新编译内核并烧录。

### 串口

115200 8N1，能最早看到 `rockchip-drm` / `dw-mipi-dsi` 的报错，黑屏时优先看串口。

## 5. 上电验证

```bash
# 1) 面板是否被识别
dmesg | grep -iE "dw-mipi-dsi|panel|rockchip-drm|dsi"
ls /sys/class/drm/
cat /sys/class/drm/card0-DSI-1/status        # connected
cat /sys/class/drm/card0-DSI-1/modes         # 1080x2160

# 2) 背光
ls /sys/class/backlight/
cat /sys/class/backlight/backlight/max_brightness   # 100
echo 80 | sudo tee /sys/class/backlight/backlight/brightness

# 3) 触摸
dmesg | grep -i sec_ts
ls /dev/input/by-path/ | grep -i touch
sudo evtest /dev/input/eventX                # 应能看到 ABS_MT_*

# 4) USB 设备控制器
ls /sys/class/udc/                            # fe800000.usb 之类
```

`modetest -M rockchip` 能列出连接器与模式（`apt install libdrm-tests`）。

## 6. 常见内核侧问题

| 现象 | 原因 / 处理 |
| --- | --- |
| `card0-DSI-1` 是 `disconnected` | DTS 没生效（include 未打开 / FIT 没重烧）；FPC 插反或没插到位 |
| 有 `DSI-1` 但黑屏 | 背光没开、`panel-init-sequence` 没执行、复位脚不对；串口看 DSI 报错 |
| 花屏/闪屏 | 时序或 lane 数不对；确认 4 lane、157 MHz、`video+burst` |
| `sec_ts` 探测失败 | I2C1 未打开、地址不是 0x48、中断脚被复用；`i2cdetect -y 1` 看 0x48 |
| 触摸坐标反了/只在一个角 | `sec,max_coords` 与实际不符；或应用侧 `rotate` 配错（见 05） |
| 开机卡在内核 logo 反复重启 | 供电不足，见 02 文档 |
| 应用报 `SetCrtc failed` / 花屏 | 图形桌面占了 DRM master；`systemctl disable --now gdm3` |

## 7. 关于控制台输出

启用 DSI 后，内核 console 可能也输出到这块屏，启动日志会闪一下。要干净的画面：

* 内核 cmdline 只保留串口 console：`console=ttyFIQ0,115200`；
* 关掉 getty：`systemctl disable getty@tty1`；
* 关掉图形登录：`systemctl disable --now gdm3`。

这些都在 `parameter-buildroot-fit.txt` / extlinux 配置里改。
