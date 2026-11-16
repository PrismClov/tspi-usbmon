# 01 架构与协议

## 1. 角色分工

| 角色 | 硬件 | 软件 |
| --- | --- | --- |
| 主机（Host） | 你的 PC（Windows 10/11 或 Linux） | Windows IDD 驱动 / `udisp_drm.ko`：把桌面合成成 JPEG，通过 USB bulk 下发；把收到的 HID 触摸当作触摸屏 |
| 设备（Device） | 泰山派 RK3566 + 天马屏 | 本项目 `tspi-usbmon`：收帧、解码、旋转/缩放、DRM 上屏；读触摸、转 HID 上报 |

主机端把这块屏当成一台“普通显示器”，扩展/复制/主屏/缩放全部由桌面环境完成，
设备端只负责“把收到的 JPEG 原样显示出来”。

## 2. 为什么能直接复用 ESP32-P4 的驱动

乐鑫的 `usb_extend_screen` 例程定义了一套**与芯片无关**的私有协议：

* USB 复合设备 `VID:PID = 303a:2986`
* 接口 0：vendor specific，两个 bulk 端点（OUT 收视频、IN 未用）
* 接口 1：HID digitizer，中断 IN 上报 5 点触摸
* 视频帧 = **16 字节帧头 + JPEG 数据**
* 设备通过**接口字符串描述符**告诉主机分辨率/编码/帧率/缓冲上限

主机驱动只依赖这些描述符和帧格式，所以设备端换成 RK3566 + Linux 完全不影响它。
本项目的 `src/gadget.c` 就是按这个 ABI 一比一实现的。

## 3. 帧头格式

16 字节，小端：

| 偏移 | 长度 | 字段 | 说明 |
| --- | --- | --- | --- |
| 0 | 2 | `crc16` | 两个主机驱动都填 0，仅占位 |
| 2 | 1 | `type` | `3` = JPEG；`0xff` = 帧结束标记（ZLP workaround） |
| 3 | 1 | `cmd` | 预留 |
| 4 | 2 | `x` | 预留（0） |
| 6 | 2 | `y` | 预留（0） |
| 8 | 2 | `width` | 逻辑画面宽（设备声明的尺寸，非 JPEG 实际宽） |
| 10 | 2 | `height` | 逻辑画面高 |
| 12 | 4 | `packed` | `frame_id = packed & 0x3ff`；`payload_len = packed >> 10` |

设备侧校验规则（`src/proto.c`）：

* `type == 3`：宽高 8..8192、`x < width`、`y < height`、`128 <= payload_len <= max_frame`
* `type == 0xff`：`payload_len == 0`，直接忽略
* 任何不合法 → 丢弃 1 字节后重新同步，避免主机中途拔线导致永久错位

> **注意 JPEG 尺寸与逻辑尺寸可能不一致。**
> 主机的 JPEG 编码器会把宽高按 16 对齐：逻辑 1080×2160 实际编码成 **1088×2160**，
> 多出的 8 列填充黑色。设备端按 `width/height` 裁掉多余列（`src/geom.c`）。

## 4. 接口字符串（设备能力上报）

主机从 **product 字符串** 和 **接口字符串** 里解析 `_` 分隔的 token：

| token | 例子 | 含义 |
| --- | --- | --- |
| `R<w>x<h>` | `R1080x2160` | 设备原生（本方案即面板方向）分辨率 |
| `Ejpg<n>` | `Ejpg6` | JPEG 质量 1..10 |
| `Fps<n>` | `Fps30` | 期望刷新率 |
| `Bl<n>` | `Bl4194304` | 帧缓冲上限 |

本项目的默认字符串（`stream = 1080x2160` 时）：

```
product   : tspi_R1080x2160_Ejpg6_Fps30_Bl4194304
interface : tspiudisp0_R1080x2160_Ejpg6_Fps30_Bl4194304
```

`Bl` 在两个主机驱动里的单位不同：

* Linux 驱动：**字节**，`4194304` → 4 MiB 上限（正好等于 `max_frame_kb = 4096`）；
* Windows 驱动：**KB**，`4194304` → 4 GiB，等于不限制（Windows 默认 8 MiB）。

所以 `max_frame_kb` 取 4096 对两端都安全：Linux 端有保护，Windows 端不会因为
上限太小而反复降质量。

## 5. 三个坐标系（触摸为什么不会错位）

| 坐标系 | 尺寸 | 谁用 |
| --- | --- | --- |
| host | `host_w × host_h` | 主机桌面的帧缓冲，HID 触摸报告用它 |
| stream | `stream_w × stream_h` | 设备字符串里的 `R`，也是收到的 JPEG 逻辑尺寸 |
| panel | `1080 × 2160` | 面板物理像素 |

变换链：

```
host ──host_rotate──▶ stream ──rotate + 等比缩放──▶ panel
        (主机驱动做的)          (设备端做的)
```

触摸是这条链的逆向：

```
panel ──逆变换──▶ stream ──逆 host_rotate──▶ host
```

* `rotate`：设备端配置，默认 0；
* `host_rotate`：主机驱动发送前的旋转，Linux `udisp_drm` 默认 `rotate=90`，
  Windows IDD 不旋转（=0），Linux 用 `host_native=1` 时也是 0。

HID 描述符里的 logical maximum 用的是 **host** 尺寸，触点报告也用 host 坐标。
所以只要 `rotate` / `host_rotate` 与主机实际配置一致，触摸就和画面完全对齐，
不需要任何校准。

## 6. 设备端线程模型

```
            ┌──────────────┐   ready slots   ┌──────────────────────┐
 USB bulk ─▶│ rx 线程       │ ──────────────▶ │ decode/display 线程   │
 (ep1 OUT)  │ 帧头解析+重组 │   (3 槽环形)     │ turbojpeg → 合成 → flip│
            └──────────────┘                 └──────────┬───────────┘
                                                         │
 touch evdev ─▶ touch 线程 ── HID 报告(52B) ──▶ ep3 IN    │ DRM page flip
                                                         ▼
                                                   MIPI-DSI 面板
```

* **rx 线程**：`poll` + `read` 64 KiB 分块，按帧头把 payload 直接读进槽位（零额外拷贝）。
  3 个槽位都占用时丢弃当前帧（继续消费掉 payload 保持流对齐），保证不反压 USB。
* **decode/display 线程**（主线程）：取**最新**一帧，丢弃积压的旧帧以降低延迟；
  `tjDecompress2(..., TJPF_BGRX)` 解码，`um_compose()` 做裁剪/旋转/缩放写入 DRM
  back buffer，`drmModePageFlip` + 等待 flip 完成。
* **touch 线程**：读 `/dev/input/event*`，按 slot 跟踪触点，`SYN_REPORT` 时把面板坐标
  经几何逆变换映射回主机帧缓冲坐标，组装 52 字节 HID 报告写入 ep3。
* **ep0 线程**：处理 FunctionFS 事件（ENABLE/DISABLE/SETUP），应答 HID 描述符与
  Feature Report 2。

### 3 槽环形缓冲

| 状态 | 含义 |
| --- | --- |
| free | 可写 |
| ready | 有完整 JPEG，等待解码 |
| 解码中 | 已取出，`um_gadget_release_frame()` 归还 |

解码端永远取序号最大的 ready 槽，其余立即释放——副屏场景下“最新一帧”比“不丢帧”重要。

### DRM 双缓冲

两个 dumb buffer 交替：`um_display_flip()` 先提交 page flip，再等 flip 完成事件
（最多 100 ms），然后交换 back/cur。这样写 back buffer 时它一定不在扫描输出中，
不会撕裂；代价是每帧最多等一个垂直消隐（60 Hz 下 ≤16.7 ms）。

## 7. 内存预算

| 项目 | 大小 |
| --- | --- |
| 2 × DRM dumb buffer 1080×2160×4 | 18.7 MB |
| 解码缓冲 1088×2160×4 | 9.4 MB |
| 收帧 3 × max_frame（4 MiB） | 12 MB |
| 待机图解码 | 9.3 MB |
| 合计 | ≈ 50 MB |

若内核里 `rockchip-drm` 的 dumb buffer 走 CMA，建议 `cma=128M`（见 03 内核章节）。

## 8. 启动与切换流程

1. systemd 启动 `tspi-usbmon`（root）。
2. 打开 DRM，找到 DSI 连接器，`SetCrtc` 点亮面板，显示待机图。
3. 创建 configfs gadget、挂载 FunctionFS、写入描述符、绑定 UDC。
4. 主机插入 → 枚举 → 主机驱动加载 → 开始 bulk 下发。
5. 设备收到第一帧，`FUNCTIONFS_ENABLE` 之后 `usb=1`，待机图被替换。
6. 拔线 → `DISABLE` → 重新显示待机图，gadget 保持绑定等待下次枚举。

## 9. 与 LVGL 方案的取舍

参考的 RK3506 项目在设备端跑 LVGL，是因为它还要显示待机页/设置页/背景视频。
本项目目标单一（USB 副屏），所以：

* 不做 UI 框架，少一层缓冲和一次格式转换；
* 待机画面用一张 JPEG，省掉字体/视频解码依赖；
* 旋转/缩放用 CPU 的定点实现（`src/geom.c`），不引入 RGA/Mali 依赖。

如果后续要加 OSD（音量、亮度条），可以在 `main.c` 的合成阶段叠加，或引入 LVGL
渲染到同一块 back buffer。
