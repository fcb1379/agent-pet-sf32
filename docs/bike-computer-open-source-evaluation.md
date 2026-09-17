# SF32 码表开源方案评估

日期：2026-09-17

## 1. 结论

可以移植，但不建议整体移植任何一个现有工程。推荐保留当前 SiFli/RT-Thread/LVGL 工程作为产品底座，按模块吸收开源工程中的成熟设计：

1. 以 `FASTSHIFT/X-TRACK` 为主要 UI、骑行数据和轨迹算法参考。
2. 以 `RaemondBW/OpenTrailPaper` 为 FIT 写入、骑行会话、异常中断恢复和 BLE 传感器行为参考。
3. 优先使用 SiFli-SDK 已有的 GNSS、BLE Heart Rate Client 和 Cycling Speed/Cadence Client，不移植 ESP32 NimBLE 或 Nordic SoftDevice 适配层。
4. 所有引入模块必须改造成固定容量、无动态内存、可在 RT-Thread 下测试的 C/C++ 边界，不能直接带入 Arduino `String`、`new/delete`、SdFat 或 ESP32 API。

首个可落地版本应是 GPS 码表 MVP：实时速度、里程、骑行/移动时间、平均/最大速度、方向、海拔、卫星与定位状态、RTC 校时、开始/暂停/结束、轨迹记录。离线地图、FIT、BLE 心率/踏频/功率和手机导航分阶段加入。

## 2. 当前 SF32 基线

当前代码基于 `work/watch_bt_audio_template`，目标板为 `sf32lb52-lchspi-ulp`。

已确认的软件和资源条件：

- SoC：SF32LB52X，RT-Thread。
- GUI：LVGL v8 和现有 GUI app framework。
- 当前生成配置的显示分辨率：390 x 450，RGB565。
- 存储布局：主程序区最大 8 MiB，内部文件系统分区 4 MiB，PSRAM 分区 8 MiB。
- 输入：触摸和两个按键。
- 外设：UART、SPI、I2C、RTC、LCD、触摸、充电管理。
- 蓝牙：BLE Central 已启用；SiFli-SDK 已包含 HRPC 和 CSCPC 客户端实现及多连接示例，但当前产品配置尚未启用这两个 profile。
- GNSS：SiFli-SDK 已包含 UC6226 NMEA 驱动和 AG3335M 驱动，可复用其 RT-Thread 串口、线程和回调结构。具体能否直接用于新转接板，取决于 GPS 模组型号和电气/引脚定义。
- TF 卡：板级 pinmux 中存在 SPI1 TF 卡引脚定义，但当前生成配置未启用 SPI1/SDIO。不能仅凭 pinmux 判断实物一定装有或可用 TF 卡。

当前基线还承载 BLE/经典蓝牙音频和手机服务，历史构建的 RAM 使用率已经较高。码表分支应先裁剪不需要的音频功能，再添加 GNSS 和传感器，避免直接叠加全部功能。

## 3. 候选工程对比

| 工程 | 许可证/活跃度 | 匹配点 | 主要阻碍 | 建议 |
| --- | --- | --- | --- | --- |
| `FASTSHIFT/X-TRACK` | MIT；最近提交 2025-11-08 | LVGL v8、MCU、GPS、轨迹、GPX、离线地图、页面/数据分层 | 240 x 240；AT32 HAL；Arduino API、`String`、`new/delete`、SdFat | 最高优先级，移植算法和界面结构，不移植底层和框架 |
| `RaemondBW/OpenTrailPaper` | Apache-2.0；最近提交 2026-09-13 | BLE HR/CPS/CSC、FIT、GPX、骑行恢复、状态模型、导航 | ESP32-S3、NimBLE、Arduino、SD、e-paper、H3 地图，代码规模较大 | 第二优先级，拆出 FIT/会话/恢复设计 |
| `euphi/TRGB-BikeComputer` | 仓库未见明确许可证；最近提交 2026-09-16 | LVGL、圆屏码表、BLE CSC/HR、Komoot 提示、统计 | ESP32 API 和动态对象较多；无明确许可不能复制 | 只作行为和 UI 参考，不复制代码 |
| `vincent290587/stravaV10` | CC BY-NC 4.0；最近提交 2022-05-12 | nRF52、GPS、ANT+、GPX、Komoot、训练指标 | 非商业限制；旧 Nordic SDK/SoftDevice；移植成本高 | 不进入产品代码，仅参考功能定义 |
| `hishizuka/pizero_bikecomputer` | Linux/Python 应用 | 功能覆盖完整，数据页、地图、导航、统计成熟 | Raspberry Pi、Python、Qt，运行环境完全不同 | 用于产品需求和数据页定义 |
| `lucas-barbosa/spin-bridge` | MIT | 纯 C++ 协议 codec、状态机、host test、FTMS/CPS | 室内骑行桥接器，不是 GPS 码表 | 后续需要 FTMS/CPS 时单独评估 |

## 4. 建议复用的模块

### 4.1 从 X-TRACK 吸收

- `DP_SportStatus` 的速度、总里程、移动时间、平均/最大速度状态更新思路。
- `Filters` 的低通、中值、迟滞滤波思路。
- `TrackPointFilter` 和 `TrackLineFilter` 的轨迹抽稀思路。
- `MapConv`、`TileConv` 的地理坐标和瓦片坐标转换；只在地图阶段引入。
- `Dialplate`、`LiveMap` 页面信息层级和大数字布局。
- GPX 输出字段和记录生命周期。

需要重写的原因：这些模块与 Arduino `String`、C++ 动态对象、工程自有 DataCenter/PageManager/HAL 存在耦合；当前产品已有 GUI app framework 和 FlashDB，不应再引入第二套框架。

### 4.2 从 OpenTrailPaper 吸收

- `ride_state` 的统一快照思想：GNSS、BLE、记录器只更新数据，不直接操作 UI。
- `fit_writer` 的最小 FIT 编码器和 CRC 设计。
- `ride_recorder` 的开始/暂停/保存/丢弃、断电后修复、移动时间和爬升统计逻辑。
- BLE Heart Rate、Cycling Power、Cycling Speed/Cadence 的数据有效性、超时和重连策略。
- GPX 路线和转向提示的数据模型。

需要重写的原因：其 BLE、文件系统、线程、PSRAM 分配和屏幕渲染均绑定 ESP32/Arduino，且大量使用 SD 卡与动态缓存。

### 4.3 优先使用 SiFli-SDK 的部分

- BLE GATT Central、连接管理、`bf0_ble_hrpc`、`bf0_ble_cscpc` 和 `example/ble/multi_connection`。
- RT-Thread serial/DMA、event、mutex、timer 和设备模型。
- UC6226/AG3335M GNSS 驱动框架；如果转接板使用其他 NMEA 模组，可保留 UART/NMEA 公共层并替换命令层。
- 现有 LVGL、LCD、触摸、RTC、FlashDB、Elm FAT 和 GUI app framework。

## 5. 明确不移植的部分

- X-TRACK 的 AT32 BSP、Keil 工程、Arduino 兼容层、SdFat、PageManager 和全套资源管理器。
- OpenTrailPaper 的 ESP32 NimBLE、Arduino `SD`、e-paper 驱动、H3 地图缓存和 LoRa/Meshtastic。
- TRGB-BikeComputer 的任何源码，除非后续确认许可证。
- stravaV10 的源码；CC BY-NC 4.0 不适合默认进入产品分支。
- ANT+：SF32 当前方案没有已确认的 ANT+ 协议栈和认证路径，MVP 只做 GPS 和 BLE。

## 6. 关键风险

1. GPS 转接板工程已确认 DX-GP10-A、5 V 供电网络、PA35/UART3_TX、PA36/UART3_RX 以及 PPS/WAKE 引出；剩余风险是实物连接方向、载板稳压/IO 电平、天线和可选控制脚实测。
2. UART 资源冲突：UART1 保留调试，GPS 使用 UART3；PA35/PA36 与 USB DP/DM 复用，因此 GNSS 配置下必须保持 USB Device/Host 关闭。
3. 存储容量：4 MiB 内部文件系统按 1 Hz 文本 GPX 记录只能覆盖有限骑行时长，必须做剩余空间检查、文件轮转和异常关闭恢复；长途记录建议启用 TF 卡。
4. RAM：现有音频/蓝牙功能占用较高，码表产品应裁剪 A2DP/WebRTC/本地音乐等非目标功能。
5. BLE 连接预算：手机连接、心率、CSC、功率计同时在线前，必须核对控制器最大连接数和实际 RAM，而不是只修改配置数字。
6. 功耗和户外可读性：构建成功或显示首帧不能证明码表可用，必须做持续定位、常亮/降亮、骑行振动和实际电流测试。

## 7. 推荐技术路线

```text
GPS 模组/UART -> GNSS 端口层 -> 固定容量骑行状态快照 -> LVGL 码表页面
                                      |
                                      +-> 记录器 -> GPX/FIT -> 内部 Flash/TF
BLE HR/CSC/CPS -> SiFli BLE Client ----+
```

模块之间只传结构化快照和事件。串口、BLE 回调不直接调用 LVGL；UI 定时拉取快照；记录器持有独立快照并做原子/互斥保护。所有队列、缓存和文件路径使用固定上限。

## 8. 来源

- https://github.com/FASTSHIFT/X-TRACK
- https://github.com/RaemondBW/OpenTrailPaper
- https://github.com/euphi/TRGB-BikeComputer
- https://github.com/vincent290587/stravaV10
- https://github.com/hishizuka/pizero_bikecomputer
- https://github.com/lucas-barbosa/spin-bridge
