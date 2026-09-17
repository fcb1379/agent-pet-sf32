# SF32 码表基础移植构建与验证报告

日期：2026-09-17

分支：`codex/feature-bike-computer`

目标：在 `sf32lb52-lchspi-ulp` 开发板上建立 X-TRACK 功能移植的可构建基线，包括 DX-GP10 NMEA 输入、骑行数据模型、GPX/BLE 传感器和 LVGL 四页骑行界面。

## 1. 已实现范围

- DX-GP10 使用 UART2，默认 9600 bps、8N1；首版引脚为 PA20/RX、PA27/TX。
- 固定长度 NMEA 接收缓冲，支持 GGA/RMC/VTG、异或校验、超长语句丢弃和错误计数；VTG 可使用 km/h 或节字段，`mode=N` 时清零速度且不会延长坐标定位有效期。
- 使用定点数保存经纬度、速度、航向、海拔和 UTC 时间，避免在串口接收路径中使用浮点运算。
- 骑行模型支持开始、暂停、停止、里程、当前/平均/最大速度、移动/总时间和卡路里估算。
- LVGL 使用横向 TileView 提供主数据、GNSS 详情和骑行总结三页；主数据和总结页均可开始/暂停/停止骑行。
- GNSS 详情页显示经纬度、海拔、航向、UTC、卫星数、定位质量、RTC 同步状态和 NMEA 统计；未定位时不伪造零坐标。
- GNSS 接收线程只持有静态栈和静态缓冲；串口回调只释放信号量，共享快照使用互斥锁保护。
- `bikedemo on/off/status` 提供无 GPS 模组演示模式，每秒生成单调 UTC、坐标、海拔、航向和速度；UART2 不存在或初始化失败时，工作线程仍启动以保留演示能力，真实串口数据在演示期间只解析不提交。
- GPX 使用独立静态线程和十二消息队列异步落盘，正常结束后原子发布 `.gpx` 文件。
- 主数据页可保存结束轨迹；总结页分别提供 `SAVE` 和 `DISCARD`，丢弃路径关闭文件并删除 `.part` 与 `.active`，不会发布最终 GPX。
- 每五个轨迹点执行一次 `fsync`；暂停和结束时强制同步，启动前保留 64 KiB 文件系统余量。
- 使用 `.active` 状态标记和 `.part` 临时文件恢复异常中断；恢复时只复制完整 XML 行。
- 单点异常跳变不会画出跨区连线；连续两个新位置点确认后建立新的 `<trkseg>`。
- 首次有效 RMC 按持久化时区（默认值来自 `CONFIG_BIKE_TIMEZONE_MINUTES`）校准片上 RTC，已移除模板启动时写死的 2022 年时间。
- 黄山派 KEY2 已适配为骑行控制键：短按开始/暂停/继续，长按结束并闭合 GPX。
- 时区、轮周、自动暂停阈值、亮度和熄屏参数使用独立 `share_prefs` 命名空间保存；RTC 校时读取运行期时区设置。
- 自动暂停使用独立防抖状态机：低于阈值持续 5 秒暂停，高于阈值 1 km/h 持续 2 秒恢复；手动暂停不会被速度变化自动恢复。
- GUI 启动和设置变化后应用持久化 LCD 亮度，并以持久化秒数替代模板固定 10 秒熄屏；0 秒可关闭自动熄屏。
- 屏幕休眠时 KEY2 第一次短按或长按只唤醒界面，不会在不可见状态下改变骑行或轨迹记录状态。
- 使用项目自有的固定槽 GATT client 分别维护 HR、CSC 和 Cycling Power 服务，避免 SDK HRPC/CSCPC/CPPC 单例环境覆盖不同连接；支持三类独立传感器以及同地址组合传感器，并在 5 秒无新数据后使结果失效。
- 在 HR/CSC/Power 三个必选服务槽之外，为最多三条传感器连接各保留一个可选 Battery Service 槽；支持 Battery Level 初始读取和通知订阅，缺少 BAS 不影响测量服务就绪。
- Heart Rate、CSC 和 Cycling Power Measurement 均由有界纯解析器校验 flags、字段长度及尾随数据；协议回调只做校验、固定快照更新或非阻塞投递。
- CSC 派生算法按持久化轮周计算轮速，处理 32 位轮转累计值、16 位曲柄累计值和 16 位事件时间回绕，并过滤超过 200 km/h 或 300 rpm 的异常结果。
- BLE 传感器管理使用 2,048 bytes 静态线程栈和固定深度消息队列；协议回调仅解析、复制并非阻塞投递，扫描、连接、配对和重连在工作线程执行。
- 广播解析支持 16-bit UUID 列表和 Service Data，可识别 Heart Rate(0x180D)、CSC(0x1816) 与 Cycling Power(0x1818)，并拒绝越界或截断的 AD 结构。
- `bikesensor scan` 启动一次 10 秒主动扫描；无已知设备时线程无限期阻塞，不做常驻扫描或周期唤醒。已连接首个传感器后仍继续本轮扫描，以发现另一类传感器。
- 已配对传感器地址使用独立 `share_prefs` 命名空间和版本/校验和保存；断连后按 15 秒退避重连。`bikesensor clear` 仅清除传感器连接和记录，不影响手机绑定记录。
- 手机外设链路和骑行传感器主机链路按 GAP role/conn_idx 隔离，传感器连接不会覆盖手机连接状态。
- 骑行总结页已接入心率、踏频、CSC 轮速、瞬时功率和三类传感器电量显示，区分未连接、已连接但无 BAS/待首帧和有效数据三种状态。
- 统一速度源采用 CSC 优先、GNSS 回退策略；CSC 有效时参与当前/平均/最大速度、移动时间、里程、卡路里和自动暂停，5 秒数据超时后自动回退到 3 秒有效期内的 GNSS 地速。
- CSC 里程按定点轮速和单调时间积分，速度来源切换时断开 GNSS 坐标段，避免从旧定位点重复累计；主页面速度单位旁显示 `CSC`、`GPS` 或 `--`。
- 按 X-TRACK 目录规则增加 `/MAP/<zoom>/<x>/<y>.bin` 离线瓦片层，使用 Web Mercator 投影，支持 3~19 级缩放、3 x 3 瓦片窗口、当前位置和固定 128 点实时轨迹。
- 地图业务缓冲全部静态分配；轨迹满时二分抽稀，瓦片缺失时仅隐藏图片并保留定位/轨迹叠加。

## 2. 主机测试

测试对象：NMEA、骑行模型、GPX、时间/自动暂停、BLE 广播与 HR/CSC/Cycling Power Measurement 解析和 CSC 派生算法。

```text
gcc -std=c11 -Wall -Wextra -Werror ... -lm -o /tmp/bike_core_test
bike_core_test: PASS
gcc -fsanitize=address,undefined -fno-omit-frame-pointer ...
bike_core_test: PASS
```

覆盖内容：

- 合法 GGA/RMC/VTG 解析及单位转换，覆盖 VTG km/h、节回退、无效模式和越界航向。
- RMC 必填时间、公历日期、状态、坐标分值与极端速度溢出路径校验。
- 坏校验语句拒绝。
- NMEA 分段输入。
- 骑行状态切换、移动时间、里程、平均速度和异常跳点过滤。
- GPX 正常闭合、暂停/继续、空间安全路径、异常跳点断段、未发布轨迹丢弃和掉电恢复。
- GPX 恢复发布后、清理 `.part`/`.active` 前再次掉电的中间状态收敛。
- 生成的正常文件与恢复文件均通过 Python `xml.etree.ElementTree` 解析。
- UTC 时区转换覆盖正负偏移、跨年、闰日和非法日期。
- 自动暂停覆盖低速防抖、恢复回差、定位失效、计时回绕和手动暂停隔离。
- CSC 覆盖首帧基线、轮速/踏频计算以及 16/32 位累计值自然回绕。
- CSC 轮转累计值重置/大跳变不会绕过 64 位乘法与 200 km/h 上限。
- BLE 广播解析覆盖完整/短 UUID 列表、Service Data、HR/CSC/Power 组合广播和畸形长度拒绝。
- BLE Measurement 解析覆盖 8/16 位心率、Energy/RR 可选字段、HR 截断/奇数 RR/非法尾随数据，CSC 轮/曲柄组合数据、保留 flags、截断和尾随数据拒绝，Cycling Power 正负功率、全可选字段、保留 flags、截断和尾随数据拒绝，以及 Battery Level 的 0~100 边界、长度和空指针校验。
- 速度源仲裁覆盖 CSC 优先、GNSS 回退、非法 CSC 拒绝、来源切换、CSC 定点里程积分以及 GNSS 失效时保留有效 CSC。
- 地图投影覆盖零点、北京坐标、经纬度/缩放边界、瓦片索引与路径穿越/截断拒绝。

目标编译同时验证了设置写入的事务顺序修正：单字段在持久化成功后才更新运行态，双字段第二次写入失败时回滚首字段。摘要页字号与行距也已按当前 390 px 可用高度收紧，不以此替代最终屏幕实机验收。

## 3. SF32 整机编译

构建目标：`sf32lb52-lchspi-ulp` HCPU。

```text
scons --board=sf32lb52-lchspi-ulp --board_search_path=../boards -j8
scons: done building targets.
```

主要产物：

| 产物 | 大小 |
|---|---:|
| `main.bin` | 3,408,120 bytes |
| `fs_root.bin` | 4,194,304 bytes |

编译仅输出原工程已有的 `dfu` 分区未定义和 ftab 入口符号警告；新增 `bike_*` 模块在 `-Werror` 主机测试中无告警，且目标编译成功。

## 4. 资源占用

HCPU ELF 链接结果：

| 区域 | 当前占用 | 链接容量 | 余量 |
|---|---:|---:|---:|
| 片上 SRAM 地址范围 | 347,136 bytes | 523,264 bytes | 176,128 bytes |
| PSRAM 可写段 | 2,649,032 bytes | 8,388,608 bytes | 5,739,576 bytes |

码表新增对象文件在链接前的直接占用：

| 模块 | `.text` | `.bss` |
|---|---:|---:|
| `bike_nmea.o` | 2,136 bytes | 0 bytes |
| `bike_time.o` | 320 bytes | 0 bytes |
| `bike_speed_source.o` | 68 bytes | 0 bytes |
| `bike_ride_model.o` | 964 bytes | 0 bytes |
| `bike_auto_pause.o` | 174 bytes | 0 bytes |
| `bike_ble_advertising.o` | 200 bytes | 0 bytes |
| `bike_ble_measurement.o` | 352 bytes | 0 bytes |
| `bike_ble_gatt_client.o` | 2,561 bytes | 129 bytes |
| `bike_csc.o` | 248 bytes | 0 bytes |
| `bike_gpx.o` | 2,966 bytes | 0 bytes |
| `bike_map.o` | 708 bytes | 0 bytes |
| `bike_recorder.o` | 1,349 bytes | 3,865 bytes |
| `bike_settings.o` | 1,924 bytes | 53 bytes |
| `bike_sensor_ble.o` | 5,927 bytes | 2,545 bytes |
| `bike_service.o` | 3,188 bytes | 3,769 bytes |
| `app_bike.o` | 6,839 bytes | 4,564 bytes |
| 合计 | 29,924 bytes | 14,925 bytes |

`bike_service.o` 和 `bike_recorder.o` 分别包含 3,072 bytes 静态线程栈，`bike_sensor_ble.o` 包含 2,048 bytes 静态线程栈以及固定消息队列和连接状态。对象文件合计只用于描述新增模块的直接体积，不等同于最终镜像增量；最终镜像还受链接消除、库引用和资源打包影响。

## 5. 验证边界

本轮已确认：源码静态检查、核心算法 `-Werror` 主机测试、ASan/UBSan 回归、SF32 HCPU 完整链接和镜像生成。

本轮未确认：

- 未烧录开发板，不能声明三页滑动、页面布局、触摸、UART2 或休眠唤醒已通过实机验证。
- 未取得转接板导出网表，PA20/PA27、3V3、GND 以及 PPS/WAKE/RESET 的实际网络仍需硬件复核。
- 未接 DX-GP10，尚未验证真实 NMEA 连续输入、首次定位时间、丢星恢复、天线性能和整机功耗。
- RTC 校时和 KEY2 控制已通过代码与目标构建验证，尚未做实板按键电平、长按阈值和 RTC 走时验证。
- 设置项、自动暂停和显示策略已通过代码、主机测试与目标构建验证，尚未验证 FlashDB 掉电保存、自动暂停道路行为、LCD 亮度范围和熄屏/唤醒的实机行为。
- GPX 已完成源码、主机测试和目标构建验证，但尚未在板载 Elm FAT 上进行复位中断和空间耗尽实机测试。
- BLE 广播解析、扫描、连接、配对、地址保存、断线重连、项目自有逐连接 GATT client、HR/CSC/Cycling Power/BAS 读写/通知解析和显示已经通过主机测试或目标构建验证，但尚未接真实传感器验证射频、配对交互、三只独立设备/组合设备订阅、Battery Service 兼容性、长时间重连、功耗和手机/传感器多连接稳定性。
- CSC/GNSS 速度仲裁已经通过主机测试与目标构建验证，但 CSC 轮速积分与 GNSS 坐标里程之间的道路误差、传感器停转通知行为和来源切换手感仍需实车验证。
- X-TRACK 离线地图和实时轨迹已完成源码、主机测试与目标构建，尚未导入实际 `.bin` 瓦片并验证 LVGL 文件解码、页面布局、缩放和长距离轨迹显示。
- X-TRACK 的 Micro SD 存储与计步尚未移植：当前工程未启用 TF 控制器，且尚无板载 IMU 型号/总线证据。FIT 与路线导航属于其他参考工程扩展，不是 X-TRACK 主线必备项。
