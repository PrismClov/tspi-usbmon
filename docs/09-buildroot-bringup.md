# 09 在泰山派 Buildroot 镜像上的实测记录

> 这一页记录的是**实机**（板子：`lckfb tspi V10 Board`，Buildroot 镜像）上跑出来的结果，
> 和前面按 SDK/Ubuntu 规划的路径不完全一样，按实际情况调整。

## 实测环境

| 项目 | 值 |
| --- | --- |
| OS | Buildroot 2018.02-rc3 |
| 内核 | Linux 4.19.232（Rockchip BSP） |
| libc | glibc 2.35（`GLIBC_2.35` 符号齐全） |
| 面板 | `card0-DSI-1: connected`，模式 **1080x2160**，`panel-simple-dsi` 已加载 |
| 触摸 | `Samsung Electronics Touchscreen 1223` → `/dev/input/event3`，MT，0..1080 / 0..2160 |
| 依赖库 | `/usr/lib/libdrm.so.2`、`/usr/lib/libturbojpeg.so.0` 都在 |
| UDC | `fcc00000.dwc3`，启动时由 `/etc/init.d/S50usbdevice` 建 `rockchip` gadget，只含 `ffs.adb` |

这意味着**不需要再改内核/DTS**就能跑我们的显示程序——面板和触摸已经是通的。

## 交叉编译

板子的 glibc 2.35 和 `~/Desktop/dev/arm-gnu-toolchain-11.3.rel1` 完全匹配，可以直接：

```bash
TC=~/Desktop/dev/arm-gnu-toolchain-11.3.rel1-x86_64-aarch64-none-linux-gnu
# 头文件：libdrm-2.4.115 源码里的 xf86drm*.h + include/drm/*.h，turbojpeg.h
# 库：从板子上 adb pull /usr/lib/libdrm.so.2.4.0 与 libturbojpeg.so.0.2.0
$TC/bin/aarch64-none-linux-gnu-gcc -O2 -std=gnu11 -D_GNU_SOURCE \
  -Isrc -Ilibdrm-2.4.115 -Ilibdrm-2.4.115/include/drm -I. \
  src/*.c -o tspi-usbmon \
  -Lsysroot/usr/lib -ldrm -lturbojpeg -lpthread -lm
```

产物约 170 KB，`NEEDED` 只有 `libdrm.so.2 / libturbojpeg.so.0 / libm / libc`，板子上全都满足。

## 已验证的部分

### 1. 显示链路（DRM + JPEG + 合成）

```bash
/tmp/tspi-usbmon -s no_gadget=1 -s splash=/tmp/splash.jpg -v
```

`/sys/kernel/debug/dri/0/summary`：

```
Video Port1: ACTIVE
    Connector: DSI-1
    Display mode: 1080x2160p59
    Smart0-win0: ACTIVE
        format: XR24
        src: 1080 x 2160  dst: 1080 x 2160
        buf[0]: addr: 0x23d4000 pitch: 4352
```

`pitch 4352 = 1088*4`，是 Rockchip 对 dumb buffer 做 64 字节对齐的结果，属正常。

### 2. 收帧 → 解码 → 上屏（TCP 调试通道）

USB gadget 暂时不可用（见下），所以加了一个调试入口：`tcp_port`，从
`127.0.0.1:<port>` 读**完全相同的 16 字节帧头 + JPEG**，主机侧用 `adb forward` 接进来。

```bash
adb forward tcp:16666 tcp:6666
adb shell '/tmp/tspi-usbmon -s tcp_port=6666 -v >/tmp/um.log 2>&1 &'
./tools/hostsim.py tools/testframes --tcp 127.0.0.1:16666 --fps 10 --seconds 10
```

实测（1080x2160、20 帧循环、q80）：

```
sent 80 frames in 8.0s (10.0 fps)
usb=1 frames=70 (10.0 fps) failed=0 avg=64.5 ms 0.99 MB/s
```

`failed=0`，VOP 正在扫描新缓冲，说明**协议解析、turbojpeg 解码、裁剪/合成、page flip 全部正确**。

> 注意：`adb forward tcp:5555 tcp:5555` 会让 adbd 自己在设备端监听 5555，
> daemon 再绑同一端口会 `EADDRINUSE`。用 6666 之类的端口，并保持两端端口不同。

### 3. 性能

1080x2160 一帧的 `avg` 约 **64 ms**（解码 + 合成 + 等 flip），也就是这个分辨率下
大约 15 fps 上限；把 `stream` 降到 `720x1440`（解码量 1/2.25）或降低 JPEG 质量会明显改善。
正式跑之前应先测 `cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq`，
Buildroot 默认 governor 可能没跑满频。

## USB gadget：这块板子的特殊限制

### 现象

自己建 configfs gadget（`/sys/kernel/config/usb_gadget/tspi_udisp`）会失败：

```
mkdir: cannot create directory '.../usb_gadget/zzz': Cannot allocate memory
kobject_add_internal failed for android0 with -EEXIST
sysfs: cannot create duplicate filename '/devices/virtual/android_usb/android0'
```

这块 Rockchip 4.19 内核带了 `android_usb`，启动后就注册了
`/devices/virtual/android_usb/android0`；**之后任何新建 gadget 都会撞名失败**，
先解绑 `rockchip` gadget 也没用。所以“设备端自建 gadget”这条路在 Buildroot 镜像上走不通
（Ubuntu 5.10 SDK 镜像没有这个驱动，原方案仍然成立）。

### 可用的替代：attach 模式

把 `ffs.udisp` 作为**第二个 function 挂进启动时已有的 `rockchip` gadget**：

```bash
G=/sys/kernel/config/usb_gadget/rockchip
UDC=$(cat $G/UDC)

echo "" > $G/UDC                       # 解绑（adbd 会退出）
mkdir -p $G/functions/ffs.udisp
ln -sfn $G/functions/ffs.udisp $G/configs/b.1/f2
mkdir -p /dev/ffs-udisp
mount -t functionfs udisp /dev/ffs-udisp

/tmp/tspi-usbmon -s attach=1 -v &      # 只写描述符，不绑定 UDC
# 等 /tmp/tspi-usbmon.ready 出现
mount -t functionfs adb /dev/usb-ffs/adb 2>/dev/null
pidof adbd || start-stop-daemon --start --background --exec /usr/bin/adbd
sleep 2
echo "$UDC" > $G/UDC                   # 此时 adb 与副屏描述符都已就绪
```

实测结果：

* `bind rc=0`，内核日志 `configfs-gadget gadget: high-speed config #1: b`；
* daemon 收到 `USB enabled`（说明主机确实 set_configuration 成功）；
* 主机 `lsusb` 能看到 `2207:0006` 的复合设备。

**恢复顺序很重要**：`adbd` 在解绑时会退出并释放 FunctionFS 描述符，必须先重启 adbd、
再绑定 UDC，否则 gadget 绑定失败、设备直接不枚举（会掉到需要拔插 USB 才能恢复）。

### 主机侧直通/权限

VM 里实测两个坑，物理机直连一般不会遇到：

1. **运行期重新枚举**时 VMware 直通会把设备丢掉；做法是让设备在开机时就以最终形态
   绑定（S51 脚本），插上线后只枚举一次，就稳定了。
2. 设备节点的 `uaccess` 不生效，adb 报 `no permissions`。加一条 udev 规则：
   `SUBSYSTEM=="usb", ATTR{idVendor}=="303a", ATTR{idProduct}=="2986", MODE="0666"`。

### 最终形态：开机即复合设备（deploy/S51usbdisplay）

装好 `deploy/S51usbdisplay` 与 `deploy/tspi-usbmon.buildroot.conf` 后，开机流程变成：

1. `S50usbdevice` 建 `rockchip` gadget（原样不动）；
2. `S51usbdisplay` 解绑 UDC，把 `ffs.udisp` 链成 **f1**、`ffs.adb` 链成 f2
   （保证主机侧 vendor 接口是 interface 0），并把 VID/PID 改成 `303a:2986`；
3. 以 `attach=1` 启动 daemon（只写描述符、不绑定），等 `/tmp/tspi-usbmon.ready`；
4. 重启 adbd（它会重写自己的描述符），然后绑定 UDC；
5. 绑定失败时自动回退成「只有 adb」的原状，保证还能连上板子。

实测结果（VM 里）：

```
# 板子
09:00:29 composite bound (udisp + adb, 303a:2986)

# 主机 lsusb
Bus 002 Device 002: ID 303a:2986 rockchip rk3xxx

# 接口布局
intf 0: class=0xff sub=0x00 proto=0x00  ep 0x01(out)/0x81(in)   ← 视频
intf 1: class=0x03 (HID)                 ep 0x82(in)            ← 触摸
intf 2: class=0xff sub=0x42 proto=0x01   ep 0x02/0x83           ← adb

# HID 被主机识别为触摸屏
I: Bus=0003 Vendor=303a Product=2986
N: Name="rockchip rk3xxx"
H: Handlers=event6   PROP=2 (DIRECT)  ABS=3

# 发帧
host:  done: 93 frames in 10.0s (9.3 fps)
board: usb=1 frames=70 (9.5 fps) failed=0 avg=69.2 ms 0.94 MB/s
```

15 fps 输入时：`frames=260 (14.1 fps) avg=55.6 ms`，CPU 1.8 GHz、温度 54 ℃，
说明瓶颈在解码本身，不在降频。

## 下一步（按优先级）

1. **把 attach 流程做成开机脚本**：修改 `S50usbdevice` 或新增 `S51usbdisplay`，
   开机时把 `ffs.udisp` 并进 gadget 并绑定一次。这样主机只在开机时枚举一次，
   VMware/Windows 的直通都稳定，也是这块板子上的“正式形态”。
2. **改 gadget 的 VID/PID**：`rockchip` gadget 现在是 `2207:0006`，而 Windows IDD 和
   Linux `udisp_drm` 都按 `303a:2986` 匹配。在未绑定状态下
   `echo 0x303a > idVendor; echo 0x2986 > idProduct` 即可（adb 在 Linux 下按接口类识别，不受影响）。
3. **换 Ubuntu 5.10 SDK 镜像**：如果不想受 `android_usb` 限制，按 03 文档刷 SDK 镜像，
   设备端自建 gadget 的原方案可以直接用。
4. 性能调优：先把 CPU governor 拉到 performance，再决定 `stream`/`jpeg_quality`。
