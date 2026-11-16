# 07 故障排查

分四层定位，逐层排除，不要一上来就改代码。

```
① 面板/内核 ──▶ /sys/class/drm/card0-DSI-1/status 是不是 connected？背光亮不亮？
② 应用     ──▶ systemctl status tspi-usbmon / journalctl 有没有报错？
③ USB      ──▶ 主机 lsusb 有没有 303a:2986？dmesg 有没有枚举？
④ 主机驱动 ──▶ 主机 dmesg 有没有 udisp / IDD 错误？显示设置里有没有新显示器？
```

先跑一键诊断：

```bash
sudo ./scripts/diag.sh
```

## 1. 屏幕不亮

| 检查 | 命令 / 现象 | 处理 |
| --- | --- | --- |
| 背光 | `cat /sys/class/backlight/backlight/brightness` | 0 就写个 80 试试 |
| DSI 连接器 | `cat /sys/class/drm/card0-DSI-1/status` | `disconnected` → DTS 没生效或 FPC 没插好 |
| FPC 方向 | 31pin 与 6pin 都要**反向** | 拔下来反插试试 |
| 内核日志 | `dmesg \| grep -i dsi` | 有 `failed to attach panel` 之类先解决 |
| 供电 | 板子是否反复重启 | 换带电源 HUB / 外接 5V |

背光亮但白屏/花屏，通常是：

* 时序/ lane 数不对（确认 4 lane、157 MHz）；
* `panel-init-sequence` 没执行（复位脚 GPIO3_C1 不对）；
* 排线接触不良（尤其 31pin 0.3 mm 很容易歪）。

## 2. 应用起不来

```bash
journalctl -u tspi-usbmon -n 50 --no-pager
```

| 日志 | 原因 | 处理 |
| --- | --- | --- |
| `cannot open DRM device` | 没有 `/dev/dri/card0` | 内核没开 DRM 或面板 DTS 没生效 |
| `drmSetMaster failed` | 图形桌面占着 master | `systemctl disable --now gdm3` |
| `SetCrtc failed` | CRTC/连接器不匹配 | 用 `-s connector=DSI-1` 指定；看 `modetest` |
| `CREATE_DUMB ... failed` | CMA 不足 | 内核 cmdline 加 `cma=128M`，或改小 `stream` |
| `no UDC in /sys/class/udc` | dwc3 没进 peripheral | 见 02 文档第 4 节，确认 DTS `dr_mode` |
| `cannot mount functionfs` | 内核没开 F_FS | `modprobe usb_f_fs`；检查 `CONFIG_USB_CONFIGFS_F_FS` |
| `bind UDC ... failed` | UDC 被占用/名字不对 | `cat /sys/class/udc` 后 `-s udc=<名字>`；`UDC is already used by gadget 'xxx'` 说明 adb 占着，`systemctl stop adbd` 或配置 `takeover_udc = 1` |

临时前台跑，能直接看到全部日志：

```bash
sudo systemctl stop tspi-usbmon
sudo /usr/local/bin/tspi-usbmon -v -v
```

## 3. 主机看不到设备

```bash
# 主机
lsusb | grep -i 303a
dmesg -w      # 插线时看有没有 new high-speed USB device
```

| 现象 | 处理 |
| --- | --- |
| 完全没有新设备 | 线是“充电线”没有数据线芯；换线；确认板子 UDC 已绑定 |
| `device descriptor read/64, error -71` | 供电/信号完整性；换短一点的线、换口 |
| 出现 `303a:2986` 但 Windows 报未知设备 | 装 IDD 驱动；确认 PID 是 2986（不是 2987） |
| Linux 没自动加载 udisp_drm | `sudo modprobe udisp_drm`；`dmesg \| grep udisp` 看 probe 错误 |
| 设备端日志没有 `USB enabled` | 主机没真正枚举成功，回到上一条 |

## 4. 枚举了但没画面

```bash
journalctl -u tspi-usbmon -f
# 看是否出现 "USB enabled" 与 frames 递增
```

* `usb=1` 但 `frames=0`：主机驱动没发数据。
  * Linux：`dmesg | grep "display info"` 看是否解析到分辨率；
  * Windows：设备管理器里显示适配器是否报错（代码 10/43）。
* `frames` 在涨但屏幕黑：`failed` 是否也涨？`avg` 是否很大？DPMS 是否被关？
* 画面撕裂/乱序：检查主机是否开了 HDR/夜间模式（部分合成器会改变像素格式），
  或把 `jpeg_quality` 降到 5 试试。
* 画面只占一角/有黑边：`stream` 与 `rotate` 不匹配，见 05 文档第 1 节。

## 5. 触摸问题

| 现象 | 处理 |
| --- | --- |
| 设备端没有触摸设备 | `ls /dev/input/`、`dmesg \| grep sec_ts`；`i2cdetect -y 1` 看 0x48 |
| 主机没有 HID 触摸 | 设备端 `USB enabled` 后才上报；Windows 需要 Feature Report 2（本项目已实现） |
| 触摸落在主屏 | Windows：平板电脑设置；X11：`xinput map-to-output` |
| 位置镜像/旋转错 | `rotate` 与实际摆放差 180°，调整 `rotate` |
| 只能单点 | 触摸驱动/固件问题；确认 `ABS_MT_SLOT` 存在（`evtest`） |

## 6. 卡顿 / 掉帧

1. 看设备端统计 `avg`：>30 ms 说明解码跟不上 → 降 `stream` 或 `jpeg_quality`；
2. 看主机 `dmesg` 的 `frame stats`：`usb` 时间大 → 换 USB 口/线，或降质量；
3. 看温度：`> 80 ℃` 会降频；
4. 看供电：电压跌落会触发 USB 重枚举（`dmesg` 里反复 `USB disconnect`）。

## 7. 拔插后不恢复

正常流程：`DISABLE` → 关闭 ep → 等待再次 `ENABLE`。如果卡住：

```bash
sudo systemctl restart tspi-usbmon
# 还不行就解绑再绑
echo '' | sudo tee /sys/kernel/config/usb_gadget/tspi_udisp/UDC
```

反复出现请附上 `scripts/diag.sh` 的输出和当时的 `journalctl`。

## 8. 完全恢复默认

```bash
sudo systemctl disable --now tspi-usbmon
sudo rm -f /etc/systemd/system/tspi-usbmon.service /usr/local/bin/tspi-usbmon
sudo rm -rf /usr/share/tspi-usbmon
sudo umount /dev/ffs-udisp 2>/dev/null
```

内核侧回滚：把 `kernel-integrate.sh` 生成的 `.orig` 还原，重新编译烧录即可。
