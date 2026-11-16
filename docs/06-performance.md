# 06 性能与调优

## 1. 带宽

一帧 1080×2160（2.33 MP）的 JPEG，4:2:0、质量 q6：

| 画面内容 | 单帧大小（经验值） |
| --- | --- |
| 纯色/渐变 | 40 ~ 90 KB |
| 普通桌面（窗口、文字） | 180 ~ 350 KB |
| 高细节（照片、视频） | 400 ~ 700 KB |

按 30 fps、300 KB/帧：

```
300 KB × 30 = 9 MB/s ≈ 72 Mbps
```

USB 2.0 High-Speed 的 bulk 实测吞吐在 RK3566 上约 **30 ~ 38 MB/s**，
余量充足。即使 60 fps + 600 KB/帧（36 MB/s）也只是刚好打满，所以默认 30 fps。

> 设备描述符是 USB 2.0（`bcdUSB = 0x0200`），即使插在 USB3 口上也按 HS 枚举。
> 这对 1080p30 的 JPEG 流足够；继续提高帧率请先降分辨率或质量。

主机端驱动有 `Bl`（缓冲上限）保护：Linux 驱动发现单帧超过 `max_frame` 会自动降质量，
日志里会出现 `frame ring full` 或 `EMSGSIZE`（`debug=1` 时可见）。

## 2. 解码预算

RK3566 是 4×Cortex-A55 @1.8 GHz，libjpeg-turbo 用 NEON 解码 1080×2160：

| 环节 | 估算耗时 | 说明 |
| --- | --- | --- |
| JPEG 解码（q6，2.33 MP） | 20 ~ 30 ms | 单线程，`TJFLAG_FASTDCT` |
| 合成（1:1 拷贝 + 裁剪） | 2 ~ 4 ms | 9.3 MB 内存拷贝 |
| 合成（90° 旋转） | 8 ~ 14 ms | 逐像素 gather，32×32 分块 |
| page flip 等待 | 0 ~ 16.7 ms | 60 Hz 面板，平均 8 ms |

也就是说 **1080×2160@30 接近这颗 SoC 的上限**，预计落在 22 ~ 28 fps（首次上机请以
设备端 stats 为准）；把流降到 720×1440 预计可以到 45 ~ 60 fps（设备端用最近邻放大，
文字会略软）。

设备端每 5 秒打印的统计就是最好的测量：

```
usb=1 frames=... (29.6 fps) failed=0 avg=21.3 ms 7.42 MB/s
```

* `fps`：实际解码上屏帧率
* `avg`：解码 + 合成的平均耗时（不含等 flip）
* `MB/s`：USB 收流速率
* `failed`：JPEG 解码失败次数（截断帧/损坏帧）

## 3. 调优旋钮

| 目标 | 调整 |
| --- | --- |
| 更流畅 | 降 `fps` 到 24；`jpeg_quality` 降到 4~5；或把 `stream` 改成 `720x1440` |
| 更清晰 | `jpeg_quality` 提到 7~8；`stream` 用 `2160x1080`（横放）或 `1080x2160`（竖放） |
| 降 CPU | `stream=720x1440` + `rotate=0`（竖放），解码量降到 1/2.25 |
| 降延迟 | 已经是最新帧优先；可把 `fps` 提到 60（带宽允许时），并保持 `jpeg_quality` 低 |
| 省电/降温 | `fps=20`、`brightness` 降到 120、`stream=720x1440` |

改完 `sudo systemctl restart tspi-usbmon`，主机侧建议重新插拔一次 USB，
让驱动重新读取接口字符串（分辨率/质量在枚举时确定）。

## 4. 主机侧观测

Linux 驱动打开 `debug=1` 后，`dmesg` 每帧/周期打印：

```
frame stats: fb=2160x1080 ... convert=2ms encode=8ms usb=10ms total=14ms fps=...
```

* `convert`：帧缓冲读取 + 旋转，正常个位数毫秒；
* `encode`：JPEG 编码；
* `usb`：bulk 发送耗时；
* 若 `frame ring full` 频繁出现，说明 USB 侧饱和 → 降质量/分辨率。

Windows 下没有等价日志，用任务管理器看 CPU，或直接观察画面是否卡顿。

## 5. 发热

持续 30 fps 解码 + 显示时：

```bash
watch -n2 'cat /sys/class/thermal/thermal_zone0/temp'
```

`> 80000`（80 ℃）会开始降频，表现为 fps 掉到 15 以下。加散热片、降 `stream`
或降 `fps` 都能缓解。

## 6. 为什么不用硬件解码

RK3566 的 VPU 主打 H.264/H.265，JPEG 解码走 MPP 并不比 libjpeg-turbo 有明显优势，
而且会引入 MPP/DRM 格式协商、dmabuf 导入等复杂度。当前方案在 1080p30 下 CPU
占用约 1.2 ~ 1.6 个核（4 核中的一部分），对一块副屏来说完全可接受。

如果后续要做 4K 或 60 fps，可以按这个顺序优化：

1. 用 RGA 做旋转/缩放（`librga` 的 `im2d`），把 `geom.c` 的 CPU 路径换掉；
2. 用 MPP 硬解 JPEG（若该 SoC 的 VPU 支持），输出 NV12 直接送 DRM plane；
3. 让主机改发 H.264（需要改主机驱动，不推荐）。
