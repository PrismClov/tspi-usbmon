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
* `avg`：解码 + 合成 + 显示提交/等待的平均耗时；当前实现包含 flip。
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


## 7. 本机实测与已部署优化（2026-11-18）

Windows IDD → USB → RK3566 Buildroot，1920×1080 输入，90° 旋转并铺满
1080×2160 面板。以下为这块板子的实际日志，不能用前文估算替代。

| 阶段 | 原版质量 60 | 优化版质量 80 |
| --- | --- | --- |
| JPEG 解码 | 约 37 ms | 约 26～31 ms |
| 旋转与缩放 | 约 90 ms | 约 13～15 ms |
| 显示提交与等待 | 约 8～9 ms | 约 7～10 ms |
| 单帧合计 | 约 135 ms | 约 51～54 ms |
| 动态桌面实际帧率 | 约 7.4 fps | 约 18～20 fps |

测试画面内容会影响解码耗时；静止桌面不连续送帧。优化版固定文字加移动矩形
测试记录到 18.3～19.7 fps。质量 80 下重新运行原版也仍约 133～138 ms/帧。
这不是保证所有内容均达到上述帧率，也尚未达到 30 fps。

实现：
- `WITH_RGA=1` 编译时启用 RGA 同步旋转缩放，直接写 DRM 导出的 DMA-BUF。
  保留 CPU 路径，在 RGA 初始化/执行失败时回退。非 Rockchip 构建不启用此选项。
- 专用可执行文件静态链接 libjpeg-turbo 2.1.5.1 的 ARM64 NEON 版本，
  不替换系统共享库；使用精确 DCT。
- Windows 枚举配置同步为 `Ejpg8`，保持 1920×1080、Fps30、Bl4096。
- 每 5 秒记录 `timing jpeg=... compose=... flip=... ms`。

验证：板上 pure tests 全部通过；无 RGA 的 CPU 路径编译通过；
抓取同一帧输入 JPEG 与面板 BGRX，比较旋转 90°、双线性缩放参考结果，
RGB 平均绝对差约 0.47/255，确认方向、颜色和位置。输入 JPEG 量化表
确认实际编码质量为 80。测量期间 JPEG 解码错误为零，温度约 55～58℃。

Windows 工作目录的 `setup/build-jpeg-neon.ps1` 重建专用静态 JPEG 库，
`setup/build-perf.ps1` 只编译，添加 `-Deploy` 才上传并让守护程序重建 USB。
依赖本机 ARM GNU 13.3 工具链、CMake/Ninja、`setup/stretch` 的板端运行库，
以及 `setup/librga/include` 中的 Rockchip 官方头文件。
源代码库：libjpeg-turbo tag `2.1.5.1`；RGA 头文件来自 airockchip/librga。
完整测量日志保存在工作目录 `setup/perf-results.log`。

板端回退备份：`/usr/bin/tspi-usbmon.before-perf`、
`/etc/tspi-usbmon.conf.before-perf`、`/etc/S51usbdisplay.before-perf`。
更新使用临时文件后原子替换程序，不覆盖仍运行的可执行文件。
Windows 驱动重连后若没有活动扩展桌面，需要重新应用扩展模式。


## 8. MPP 硬解和三缓冲（2026-11-18，后续部署）

当前部署在上一节基础上继续启用了 `WITH_MPP=1 WITH_RGA=1`：
- 完整 JPEG 用 MPP task API 解码到 DRM DMA-BUF，NV12/NV16 直接交给 RGA。
- RGA 使用 JPEG full-range BT.601 转换、旋转、缩放并写显示缓冲。
- 三个显示缓冲分别用于扫描、待翻转、合成；等待前一次翻转后才提交下一次。
  解码/合成与上一帧 vblank 等待重叠，不写入仍在扫描或待翻转的缓冲。
- MPP poll 有 500 ms 超时；不支持的 JPEG/硬解错误回退到软件解码。
- 启动画面仍用软件解码。配置为 `R1920x1080_Ejpg8_Fps60_Bl4096`。

独立重复解码 120 帧（不含传输/RGA/显示）：
- 桌面 JPEG 1920×1080：自编接口约 5.22 ms/帧，191.6 fps。
- 3840×2160 高细节合成 JPEG，质量 80、约 2.42 MB/帧：29.28 ms/帧，34.15 fps。
- 提高 DDR 到设备支持的 performance 档没有改善测试结果，已恢复原 dmc_ondemand。

动态桌面端到端观察：约 30～36 fps；解码 7～8 ms，RGA 8～11 ms，
显示提交/等待约 0.5 ms，合计约 16～19 ms。约 3800 帧期间只有 1 次丢帧，
JPEG 解码失败为 0。host 接收计数与处理计数接近，当前主要限制在主机送帧侧；
未单独测得 Windows 取帧、JPEG 编码、USB 发送各自耗时，不能再细分归因。
统计是处理/提交速率，异步翻转可能有一帧尚待显示，不代表独立测量的扫描刷新率。
用户确认颜色方向正常、没有可见闪屏。内核仍可见此前就出现的偶发
POST_BUF_EMPTY 记录；不宣称已消除所有显示驱动问题。

验证：`setup/mpp-test.c` 的 1080p/4K 反复切换、无效输入、渐进式 JPEG
软件回退测试通过；pure tests 通过；不启用 MPP/RGA 的源码编译通过。
日志：工作目录 `setup/mpp-performance.log`。上一版备份：
`/usr/bin/tspi-usbmon.before-mpp`、`/etc/tspi-usbmon.conf.before-mpp`、
`/etc/S51usbdisplay.before-mpp`。

**未实现 4K60 副屏。** 物理面板为 1080×2160；当前 Windows 驱动公开模式
列表最高 1920×1080；链路枚举为 USB 2.0 high-speed。
官方 4K60 视频硬解指标不等于逐帧 JPEG 的性能保证。
上述 2.42 MB 的 JPEG 若按 60 fps 发送，需要约 145 MB/s，已经超过
USB 2.0 的 60 MB/s 理论总线字节速率，尚未计协议开销。
要做真正原生 4K60，还需要 4K 显示硬件、支持 4K 的主机显示驱动，以及
H.264/H.265 等主机硬编码到板端硬解的低带宽视频链路；这些尚未实现。


## 9. 保持 1080p 的帧率取舍与 USB 分析（2026-11-18）

用户明确要求保持 1080p。最新部署质量 70、请求帧率 60，仍使用 MPP、
RGA 和三缓冲。固定文字加移动矩形的最后一轮采样为 35.7、34.5、35.3、
31.5 fps；质量 80 的对照约 30～32 fps。并非稳定 60 fps。
质量 60 曾测到 33～38 fps，部分试验峰值超过 40，但不能作为持续性能承诺。

试验过 4×64KB Linux AIO 异步 USB 接收，在相同质量 80 下未见稳定收益，
已移除该试验代码，继续使用原同步接收。DDR governor 已恢复 dmc_ondemand。
各次测试都保留了正常的桌面内容变化/调度波动，因此这些是短时观察，
不是针对所有画面的严格吞吐上限。

板端 current_speed 与 maximum_speed 均报告 high-speed，表示当前配置为
USB 2.0。副屏 FunctionFS 描述符只有 FS/HS，无 SS；单换 USB3 主机接口
不会自动获得 SuperSpeed。该报告不能证明芯片所有 USB 控制器均不支持 USB3。

本次1080p接收吞吐通常6～12 MB/s，JPEG约0.2～0.35MB/帧；按同样大小
60fps约需12～21MB/s。USB2的480Mb/s（60MB/s）是理论总线速率，不能
当作可用payload速率。但现有证据也不足以证明总线物理带宽已经用满。
USB驱动、请求调度、主机取帧/编码仍可能造成等待，尚未逐段测出Windows耗时。
高细节4K JPEG测试帧2.42MB若60fps则约145MB/s，确实超过USB2理论上限；
H.264/H.265视频码流因压缩方式不同，不能套用这个JPEG带宽结论。

最新对照日志：工作目录 setup/fps-comparison.log。

## 10. 解码与 RGA 并行（2026-11-18）

已部署双解码器加单任务渲染线程：主线程用另一套 MPP context / DMA-BUF
解码下一帧，渲染线程旋转缩放上一帧并提交 KMS。解码完成之后才等待上一
渲染任务退出，再交换两套解码器。RGA 返回之前不会复用或释放其输入缓冲。
USB 接收仍为原有独立线程；没有增加无界队列或更改 JPEG 质量。
保持 1920×1080、质量 70、请求 60 fps、旋转 90°。

`src/render.c` 统一管理任务所有权、结果同步、线程退出与串行回退；线程
创建失败自动串行。`UM_SERIAL_RENDER=1` 可用于同一程序的串行对照。
DMA 合成失败会跳过该帧并将相应解码器切换到软件路径，避免显示未写入的
缓冲。退出时先完成渲染、加入线程，再销毁显示和解码资源。

同一 `setup/fps-pattern.py` 55 秒动态画面，开启相同 Windows monitor
级日志的两次短时运行（统计动态窗口，排除静止部分）：

| 指标 | 原串行版 | 并行版 |
| --- | ---: | ---: |
| 板端处理/提交帧率，5 秒窗口平均 | 35.26 fps | 35.30 fps |
| JPEG 解码 | 6.39 ms | 6.32 ms |
| RGA 旋转缩放 | 8.76 ms | 8.51 ms |
| 显示提交 | 0.53 ms | 0.60 ms |
| Windows 取帧，帧平均 | 4.85 ms | 4.54 ms |
| Windows JPEG 编码 | 6.25 ms | 5.92 ms |
| Windows USB 请求提交 | 0.098 ms | 0.098 ms |

约 +0.04 fps / +0.1% 属于运行波动，**没有证明端到端帧率提高**。
这些是短时采样，5 秒窗口包括动画开始/结束边界影响，不是严格硬件上限。
Windows 分别统计 1969 / 1929 个约 200～240 kB 的帧日志，提交时间不代表
USB 完成时间；驱动原有括号内等待值因源码局部变量初始化问题不用于归因。

另一次 debug 级诊断，用 URB id 配对发起与完成回调日志，1912 对有效
请求平均间隔 7.13 ms，P95 7.57 ms；连续发起间隔平均 29.02 ms
（排除 >=100 ms 的非连续区间）。日志所见最多一个请求在途。此值由
Windows 日志接收端的单调时钟估算，包含日志/调度开销，不是 USB 分析仪
测得的纯总线时间。该轮不与 monitor 级帧率直接比较。

同轮板端实际解码/渲染重叠平均 0.32 ms，等待渲染完成约 0.005 ms；
表明当前输入稀疏，上一帧通常已完成，新增并行余量尚未转化为更多新帧。
日志 `avg` 是各阶段工作时间之和，并行后不能用其倒数当作 FPS 或延迟；
`overlap` 是当前解码与上一渲染任务的时间交集，`wait` 是等待渲染完成时间。
证据指向主机出帧/调度间隔仍需优化，不能断言 USB2 物理带宽已用满。

验证：板端 pure tests、render mock 测试通过，包括异步提交不阻塞、拒绝
覆盖未完成任务、1000 次交接、DMA 失败回退、带未完成任务的退出和串行
模式。CPU/stub 编译通过（原显示 stub 声明与路径长度警告仍存在）。
SIGTERM 退出后 watchdog 成功恢复副屏，用户确认画面正常、无新增异常。
测量中 failed=0。Windows debug 注册表值已恢复，随后重连使其生效。
同时修复 Makefile 中损坏的 PURE_SRCS 声明，并把 render 测试加入 test。

分析脚本 `setup/analyze-pipeline.py`，汇总 `setup/pipeline-results.json`；
原始数据 `setup/pipeline-{before,after}-{board.log,windows.jsonl}`、
`setup/pipeline-overlap-board.log`、`setup/pipeline-usb-completion.jsonl`。
板端上一版可执行文件备份 `/usr/bin/tspi-usbmon.before-pipeline`。

恢复 Windows 日志设置并重连后的最终动态复测，17 个有效 5 秒窗口平均
32.34 fps，累计处理 2830 帧且 failed=0，用户测试窗口已自动关闭。
该轮场景重启/运行状态与首轮存在差异，不能据此单独归因回退；它进一步
说明没有稳定帧率提升。最终程序仍是并行版，原始日志
`setup/pipeline-final-board.log`。当前应报告约 32～35 fps，而不是 60 fps。

## 11. Windows 提前通知与实际翻转（2026-11-19）

使用 host/windows/early-notify.patch 编译的驱动（实测包版本 1.2.25.5），
配合 deploy/1080p60 下的配置，Windows 以 1920×1080、JPEG 质量 70 发送。
测试动画以 60 fps 运行 70 秒。板端在日常并行版上临时应用
tools/trace-kms.patch，在 KMS 翻转事件中输出协议帧号、刷新序号和时间戳。
按传输顺序处理 10 位帧号回绕，再关联 Windows 帧号并排除重复内容。

中间连续 60 秒内，Windows 提交 3325 帧，副屏实际翻转 3323 张不同画面。
平均 55.38 fps，逐秒 46～60 帧，36 个一秒窗口达到 55 帧；
翻转间隔 P95 为 33.67 ms。平均达标，但不能稳定保持每秒 55 帧。
测试结束后恢复常规板端程序并保留提前通知驱动。诊断补丁仅用于复测。
