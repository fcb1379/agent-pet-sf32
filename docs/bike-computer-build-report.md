# SF32 码表基础移植构建与验证报告

日期：2026-09-17

分支：`codex/feature-bike-computer`

目标：在 `sf32lb52-lchspi-ulp` 开发板上建立 X-TRACK 功能移植的可构建基线，包括 DX-GP10 NMEA 输入、骑行数据模型、GPX/BLE 传感器、TF 存储、板载计步/电子罗盘、电池状态、事件提示音和 LVGL 四页骑行界面。

## 1. 已实现范围

- DX-GP10-A 使用 UART3，默认 9600 bps、8N1；转接板网络为 PA36/RX、PA35/TX，且 PA35/PA36 与 USB DP/DM 复用，因此当前配置保持 USB Device/Host 关闭。
- 固定长度 NMEA 接收缓冲，支持 GGA/RMC/VTG、异或校验、超长语句丢弃和错误计数；VTG 可使用 km/h 或节字段，`mode=N` 时清零速度且不会延长坐标定位有效期。
- 使用定点数保存经纬度、速度、航向、海拔和 UTC 时间，避免在串口接收路径中使用浮点运算。
- 骑行模型支持开始、暂停、停止、里程、当前/平均/最大速度、移动/总时间和卡路里估算。
- LVGL 使用横向 TileView 提供主数据、GNSS 详情、离线地图和骑行总结四页；主数据和总结页均可开始/暂停/停止骑行。
- 依据历史实机修复 `d18e3e4`，码表页与时钟状态页不再显式绑定 `lv_font_montserrat_*` 字体对象，统一继承项目已验证的默认字体；未启用曾导致黑屏、花屏或卡死的 legacy flush、RGB565 byte swap、PSRAM framebuffer 或 UNSCII 诊断配置。
- GNSS 详情页显示经纬度、海拔、航向、UTC、卫星数、定位质量、RTC 同步状态和 NMEA 统计；未定位时不伪造零坐标。
- GNSS 接收线程只持有静态栈和静态缓冲；串口回调只释放信号量，共享快照使用互斥锁保护。
- `bikedemo on/off/status` 提供无 GPS 模组演示模式，每秒生成单调 UTC、坐标、海拔、航向和速度；UART3 不存在或初始化失败时，工作线程仍启动以保留演示能力，真实串口数据在演示期间只解析不提交。
- GPX 使用独立静态线程和十二消息队列异步落盘，正常结束后原子发布 `.gpx` 文件。
- 主数据页可保存结束轨迹；总结页分别提供 `SAVE` 和 `DISCARD`，丢弃路径关闭文件并删除 `.part` 与 `.active`，不会发布最终 GPX。
- 每五个轨迹点执行一次 `fsync`；暂停和结束时强制同步，启动前保留 64 KiB 文件系统余量。
- 使用 `.active` 状态标记和 `.part` 临时文件恢复异常中断；恢复时只复制完整 XML 行。
- 单点异常跳变不会画出跨区连线；连续两个新位置点确认后建立新的 `<trkseg>`。
- 首次有效 RMC 按持久化时区（默认值来自 `CONFIG_BIKE_TIMEZONE_MINUTES`）校准片上 RTC，已移除模板启动时写死的 2022 年时间。
- 黄山派 KEY2 已适配为骑行控制键：短按开始/暂停/继续，长按结束并闭合 GPX。
- 时区、轮周、自动暂停阈值、亮度和熄屏参数使用独立 `share_prefs` 命名空间保存；RTC 校时读取运行期时区设置。
- 骑手体重按 X-TRACK 上游默认 65 kg 保存到独立运行参数，`bikeset weight <kg>` 支持 30~250 kg；服务启动和每次新骑行开始时恢复最新值，暂停继续不会在骑行中途改变卡路里基准。
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
- 按 X-TRACK 目录规则增加 `/MAP/<zoom>/<x>/<y>.bin` 离线瓦片层，保持 GNSS/GPX 原始坐标为 WGS-84，地图可按持久化设置选择 WGS-84 或 GCJ-02 后执行 Web Mercator 投影；启动时扫描当前介质的 0~19 级纯数字目录并以实际最小/最大目录约束缩放，未发现合法目录时回退 0~19 级，支持 3 x 3 瓦片窗口、当前位置和固定 128 点实时轨迹。
- 3 x 3 地图窗口按缩放级别和瓦片坐标复用九块固定 PSRAM 缓冲，平移跨过一条瓦片边界时只同步读取新进入的三张瓦片，不再整窗重读九张；缺失瓦片的失败结果也会缓存到地图源或缩放改变，避免 500 ms 刷新持续访问同一缺失文件。
- 无 GNSS 定位的手势浏览将 TF 文件读取移出 `LV_EVENT_PRESSING` 实时路径：按住拖动期间只按全局像素差移动现有 3 x 3 缓存，500 ms 页面刷新也不会抢先触发读卡；松手或触摸丢失后才补齐新进入的瓦片，因此拖动帧不再被 FAT/SPI 阻塞。
- 主码表底部地图、骑行控制和 GNSS/系统信息按钮不再依赖当前字体缺失的 `LV_SYMBOL_*` 私有区字形，也不经过目标显示链路不稳定的 Alpha 图片解码；改用 LVGL 原生线条、边框和圆形图元绘制，中间按钮通过显隐切换播放/暂停图案。
- 对齐 X-TRACK `mapDirPath` 和 `mapExtName`，`bikeset mapdir </path>` 可在当前介质内选择并持久化多个地图源，`bikeset mapext <ext>` 可选择 1~7 位字母数字文件扩展名；TF 卡挂载时自动增加 `/sd` 前缀，切换后即时重扫缩放范围和重载瓦片。路径校验拒绝相对路径、点号、空段、尾随斜杠、非法扩展名和超长输入；扩展名只改变文件名选择，瓦片内容仍须为当前加载器支持的 LVGL v8 RGB565 二进制格式。
- 对齐 X-TRACK `arrowTheme`，`bikeset arrow <default|light|dark>` 可持久化并即时切换定位箭头颜色；40 bytes 只读 1-bit Alpha 蒙版按 GNSS course 以 0.1 度旋转，定位失效时隐藏，不建立运行期箭头像素缓冲。
- SDK 自带文件图片解码器只对 MTD NAND/NOR 提供打开路径，无法直接显示 SPI-MSD/FAT 上的 X-TRACK `.bin` 文件；现改为严格校验 LVGL v8 头、256 x 256 RGB565 格式和精确文件长度，再读入九块固定 PSRAM 非缓存缓冲并以 LVGL 变量图像显示。截断、尾随数据、错误尺寸或错误格式的瓦片会被拒绝并隐藏，不使用动态内存。
- 地图业务缓冲全部静态分配；轨迹以 X-TRACK 默认 16 级坐标存储，缩放时用定点移位重投影而不清空，128 点缓存从完整投影结构缩减为 1,024 bytes；轨迹满时二分抽稀，瓦片缺失时仅隐藏图片并保留定位/轨迹叠加。
- 历史总里程、移动/总时间、卡路里、骑行次数和最高速度使用带版本/序号/FNV-1a 校验和的 FlashDB A/B 双槽记录。
- 新骑行捕获累计基线，每 60 秒以“基线 + 本次绝对值”异步落盘，重复检查点不重复累加；正常结束保存最终值，主动丢弃回滚到开始基线。
- FlashDB 写入由 1,536 bytes 静态工作线程和固定消息队列顺序执行，GNSS 线程和 LVGL 回调仅非阻塞投递快照。
- 已启用黄山派 SPI1 与 SDK SPI-MSD 驱动，使用板级既有 PA24/DIO、PA25/DI、PA28/CLK、PA29/CS 映射探测 `sd0`。
- TF 卡仅挂载到独立 `/sd`，不替换内部根文件系统且不自动格式化；挂载成功时轨迹与地图分别使用 `/sd/tracks`、`/sd/MAP`，无卡、无文件系统或挂载失败时回退 `/tracks`、`/MAP`。
- SPI-MSD 启动探测失败后，进入码表应用会再执行一次块设备探测和 `/sd` 挂载；因此开机后插卡可通过退出并重新进入码表应用触发识别，无需自动格式化卡片。
- 黄山派板载 LSM6DS3TR-C 使用 SDK 的 LSM6DSL 兼容计步驱动；QADSPI 屏幕配置下将空闲 PA39/PA40 复用为 I2C2 SDA/SCL，先校验 WHO_AM_I，再注册并开启仅计步传感器。
- 计步线程使用 2,048 bytes 静态栈，每秒以带返回值检查的 I2C 事务读取 16 位步数；启动后回读 ODR、功能引擎和计步使能位，扩展处理硬件回绕/复位并过滤异常跳变。
- GNSS 详情和总结页显示累计步数、IMU 状态与 I2C 错误计数；若未来切换到占用 PA39/PA40 的 8080 LCD 接口，代码会拒绝计步初始化而不是破坏显示总线。
- 板载 MMC5603NJ 与 IMU 共享 I2C2；初始化校验 0x39 产品 ID 为 0x10，软件复位后以 5 Hz 单次测量和自动 SET/RESET 读取完整 9 字节 20 位三轴数据。
- 罗盘线程使用 2,048 bytes 静态栈，所有 I2C 事务均检查返回值并带数据就绪超时；水平面硬铁中心校准达标前只发布进度，GNSS 详情页显示航向、三轴 mG 和错误计数。
- 已补齐黄山派 GPADC1 板级配置，使用 `bat1` 的 SF32LB52 内部 VBAT channel 7 读取 SDK 校准后的 0.1 mV 电压；每次采样均执行使能、返回长度检查和关闭，避免 ADC 常开。
- 电池线程使用 1,536 bytes 静态栈并以 30 秒周期采样；电压采用四分之一权重低通和 2% 显示回差，百分比沿用 X-TRACK 的 3.3~4.1 V 线性估算模型，不将其描述为库仑计 SOC。
- 片上 `charge` 设备用于区分电池、USB/外部供电和充满状态；充电状态失败不丢弃有效电压，ADC 与充电状态分别累计错误。主页面显示轨迹/电池状态，GNSS 详情页显示电压和诊断计数。
- X-TRACK `MusicCode` 的启动、错误、连接、断开、信号不稳、充电开始/结束和无操作提示序列已转换为 16 kHz/16-bit/单声道方波 PCM，通过黄山派 `AUDIO_TYPE_NOTIFY` 固定输出到片上 Codec/扬声器。
- 提示音使用 2,048 bytes 静态线程栈、八消息静态队列和 20 ms 静态 PCM 缓冲；码表页面启动、GNSS 状态边沿、骑行开始/暂停/停止/丢弃及外部供电边沿只做非阻塞投递。`sound_en` 通过 FlashDB 持久化，`bikeset sound <0|1>` 和 `bikesound play <event>` 提供配置与实机诊断入口。

## 2. 主机测试

测试对象：NMEA、骑行模型、GPX、时间/自动暂停、计步累计、磁力计校准/航向、电池电压估算/滤波、提示音固定序列、BLE 广播与 HR/CSC/Cycling Power Measurement 解析和 CSC 派生算法。

```text
gcc -std=c11 -Wall -Wextra -Werror ... -lm -o /tmp/bike_core_test
bike_core_test: PASS
gcc -fsanitize=address,undefined -fno-omit-frame-pointer ...
bike_core_test: PASS
gcc -Wall -Wextra -Werror -fanalyzer -c ...
无告警
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
- 地图转换/投影覆盖 WGS-84 原样输出、北京 WGS-84 转 GCJ-02、境外坐标不偏移、非法坐标系、零点、经纬度/缩放边界、16~19 级轨迹像素正反转换、非法像素坐标、瓦片索引与路径穿越/截断拒绝。
- 地图文件加载覆盖正常 RGB565、色键 RGB565、非法源类型字段、错误色彩格式、错误尺寸、截断载荷、尾随数据、容量不足、缺失文件和空指针保护。
- 地图源配置覆盖内部介质/TF 挂载点解析、多级合法路径、路径穿越、连续/尾随斜杠、非法字符、超长输入、输出缓冲截断，以及扩展名空值、非法字符和 1~7 位长度边界。
- 地图箭头主题覆盖 `default/light/dark` 完整匹配、稳定名称、大小写错误、尾随字符、空字符串和空指针拒绝。
- 历史累计模型覆盖空记录、多次骑行合并、最高速度保留、校验和损坏以及 A/B 槽选择/全损坏回退。
- 存储路径选择覆盖 TF 优先和内部文件系统回退，容量快照覆盖介质类型、总/剩余字节关系、重新查询和空指针保护；地图目录扫描覆盖合法最小/最大缩放级别、越界/非数字目录、同名普通文件、空目录、缺失目录和空指针保护；主机测试使用本机文件系统，不执行目标板挂载操作。
- 计步累计覆盖首帧基线、正常递增、异常跳变重同步、传感器计数复位、16 位自然回绕、32 位饱和和空指针保护；主机测试不访问 I2C。
- 磁力计覆盖 X/Y 极值和中心偏移、校准进度、最少样本门限、四象限 0~359.9 度航向、样本计数饱和和空指针保护；主机测试不访问 I2C。
- 电池模型覆盖 3.3/4.1 V 百分比边界、四分之一权重低通、2% 回差、0/100% 边界更新、无效电压和空指针保护；主机测试不访问 ADC/charger。
- 提示音模型覆盖全部已接入 X-TRACK 事件的节点数量、频率、时长、静音节点、稳定名称、非法事件和空指针保护；主机测试不访问 Audio Codec。

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
| `main.bin` | 3,341,852 bytes |
| `fs_root.bin` | 4,194,304 bytes |

完整重构建输出 SDK 既有的 FlashDB/LVGL/音频等静态告警，以及 `dfu` 分区未定义和 ftab 入口符号警告；新增 `bike_*` 模块在 `-Werror` 主机测试中无告警，且目标编译成功。生成配置已确认 `CONFIG_BSP_USING_UART3=y`、`CONFIG_BSP_UART3_RX_USING_DMA=y`，未启用 UART2 或 USB Device/Host；同时启用 `CONFIG_BSP_USING_SPI1=y`、`CONFIG_RT_USING_SPI_MSD=y`、`CONFIG_RT_USING_SENSOR=y`、`CONFIG_ACC_USING_LSM6DSL=y`、仅 `CONFIG_PKG_USING_LSM6DSL_STEP=y`，以及 `CONFIG_RT_USING_ADC=y`、`CONFIG_BSP_USING_ADC1=y`、`CONFIG_BSP_BATTERY_DETECT_ADC="bat1"`、`CONFIG_BSP_BATTERY_DETECT_ADC_CHANNEL=7`；加速度/陀螺仪 RT-Thread 设备未启用。

HCPU ELF 汇总为 `.text=3,334,329 bytes`、`.data=7,492 bytes`、`.bss=4,150,952 bytes`。其中地图的 1,179,648 bytes 固定像素缓冲位于 PSRAM 非缓存段，不占用片上 SRAM。目标编译已实际生成 `lsm6dsl.o`、`lsm6dsl_reg.o`、`st_lsm6dsl_sensor_v1.o`、`sensor.o`、`drv_adc.o`、`bike_pedometer.o`、`bike_compass.o`、`bike_power.o`、`bike_sound.o`、`bike_storage.o` 和 `bike_map_image.o`。

## 4. 资源占用

HCPU ELF 链接结果：

| 区域 | 当前占用 | 链接容量 | 余量 |
|---|---:|---:|---:|
| 片上 SRAM 地址范围 | 356,244 bytes | 523,264 bytes | 167,020 bytes |
| PSRAM 可写段 | 3,828,720 bytes | 8,388,608 bytes | 4,559,888 bytes |

码表新增对象文件在链接前的直接占用：

| 模块 | `.text` | `.bss` |
|---|---:|---:|
| `bike_nmea.o` | 2,062 bytes | 0 bytes |
| `bike_time.o` | 314 bytes | 0 bytes |
| `bike_speed_source.o` | 68 bytes | 0 bytes |
| `bike_ride_model.o` | 982 bytes | 0 bytes |
| `bike_auto_pause.o` | 174 bytes | 0 bytes |
| `bike_ble_advertising.o` | 236 bytes | 0 bytes |
| `bike_ble_measurement.o` | 346 bytes | 0 bytes |
| `bike_ble_gatt_client.o` | 2,493 bytes | 129 bytes |
| `bike_csc.o` | 244 bytes | 0 bytes |
| `bike_gpx.o` | 2,874 bytes | 0 bytes |
| `bike_history.o` | 2,147 bytes | 2,189 bytes |
| `bike_map.o` | 2,625 bytes | 0 bytes |
| `bike_map_image.o` | 212 bytes | 0 bytes |
| `bike_compass.o` | 1,905 bytes | 2,273 bytes |
| `bike_pedometer.o` | 1,299 bytes | 2,253 bytes |
| `bike_sound.o` | 1,427 bytes | 2,937 bytes |
| `bike_power.o` | 1,321 bytes | 1,747 bytes |
| `bike_storage.o` | 1,448 bytes | 71 bytes |
| `bike_recorder.o` | 1,434 bytes | 3,865 bytes |
| `bike_settings.o` | 3,402 bytes | 111 bytes |
| `bike_sensor_ble.o` | 5,925 bytes | 2,545 bytes |
| `bike_service.o` | 4,351 bytes | 3,930 bytes |
| `app_bike.o` | 8,443 bytes | 1,181,496 bytes |
| 合计 | 45,732 bytes | 1,203,546 bytes |

`app_bike.o` 的 `.bss` 包含明确放入 PSRAM 的 1,179,648 bytes 地图像素缓存，不是片上 SRAM 占用。`bike_service.o` 和 `bike_recorder.o` 分别包含 3,072 bytes 静态线程栈，`bike_sensor_ble.o`、`bike_pedometer.o`、`bike_compass.o` 与 `bike_sound.o` 分别包含 2,048 bytes 静态线程栈，`bike_history.o` 与 `bike_power.o` 分别包含 1,536 bytes 静态线程栈。对象文件合计只用于描述新增模块的直接体积，不等同于最终镜像增量；最终镜像还包含本轮启用的 SDK LSM6DSL、RT-Thread sensor、GPADC1 与音频服务，并受链接消除、库引用和资源打包影响。

## 5. 验证边界

本轮以 X-TRACK 上游 `c42b8e5605e50838f4c0c9828385948fa33ab485` 为对照基线，已确认主线功能逐项对照、源码静态检查、核心算法 `-Werror` 主机测试、ASan/UBSan 回归、GCC `-fanalyzer` 零告警、SF32 HCPU 完整链接和镜像生成。文件级复核补齐了此前遗漏的 `mapWGS84` 坐标系选择、地图目录缩放级别扫描、`arrowTheme` 方向箭头和 SystemInfos 存储介质/容量信息；后续验证项需要真实开发板、GPS 转接板、离线瓦片或外部传感器，不再把实机结果与代码完成状态混为一项。

本轮未确认：

- 应用字体回退已完成源码复核和目标构建，但仍需在当前实机上确认主数据、GNSS、地图、总结和时钟状态页的文字不再异常；字体大小变化和 390 x 450 布局仍以实机视觉结果为准。
- 未烧录开发板，不能声明四页滑动、页面布局、触摸、UART3 或休眠唤醒已通过实机验证。
- 转接板工程已确认 PA35/TX、PA36/RX、`GPS_5V`、GND 以及 PPS/WAKE 引出；实物连接器方向、5 V/载板稳压输出、UART 电平和可选 PPS/WAKE 的 MCU GPIO 仍需硬件复核。
- 未接 DX-GP10，尚未验证真实 NMEA 连续输入、首次定位时间、丢星恢复、天线性能和整机功耗。
- RTC 校时和 KEY2 控制已通过代码与目标构建验证，尚未做实板按键电平、长按阈值和 RTC 走时验证。
- 设置项、自动暂停和显示策略已通过代码、主机测试与目标构建验证，尚未验证 FlashDB 掉电保存、自动暂停道路行为、LCD 亮度范围和熄屏/唤醒的实机行为。
- GPX 已完成源码、主机测试和目标构建验证，但尚未在板载 Elm FAT 上进行复位中断和空间耗尽实机测试。
- BLE 广播解析、扫描、连接、配对、地址保存、断线重连、项目自有逐连接 GATT client、HR/CSC/Cycling Power/BAS 读写/通知解析和显示已经通过主机测试或目标构建验证，但尚未接真实传感器验证射频、配对交互、三只独立设备/组合设备订阅、Battery Service 兼容性、长时间重连、功耗和手机/传感器多连接稳定性。
- CSC/GNSS 速度仲裁已经通过主机测试与目标构建验证，但 CSC 轮速积分与 GNSS 坐标里程之间的道路误差、传感器停转通知行为和来源切换手感仍需实车验证。
- X-TRACK 离线地图、WGS-84/GCJ-02 选择、多地图源目录/文件扩展名切换、地图目录缩放边界扫描、缩放轨迹重投影和 FAT 二进制瓦片到 PSRAM/LVGL 变量图像加载已完成源码、主机测试、静态分析与 SF32 目标构建；尚未导入两种坐标系、多个目录和自定义扩展名的实际瓦片并验证页面布局、缩放、坐标对齐、TF 读取延迟和长距离轨迹显示。
- X-TRACK `arrowTheme` 已完成 `default/light/dark` 持久化、运行期重着色、GNSS course 旋转和无效定位隐藏的源码、主机解析测试与目标构建；尚未实机确认三种主题在明暗瓦片上的对比度、旋转方向和视觉锯齿。
- 掉电累计记录已通过纯模型测试和目标构建，但尚未对真实 FlashDB 执行 A/B 槽交替、写入中复位、60 秒检查点精度和丢弃回滚实机测试。
- X-TRACK 的 Micro SD 存储已完成 SPI1 驱动、独立挂载、路径回退、进入应用时的插卡重探测、总/剩余容量快照和 UI 诊断的源码与目标构建；尚未用真实 TF 卡验证 FAT 兼容性、异常拔卡、满盘和断电行为。当前板级无卡检测 GPIO，因此仍未实现插拔中断或异常拔卡后的自动重枚举。
- X-TRACK 计步已完成板载 LSM6DS3TR-C、I2C2 PA39/PA40、SDK LSM6DSL 兼容驱动、寄存器回读和 UI 状态的源码/测试/目标构建；尚未实机确认 WHO_AM_I、走动计数准确率、16 位回绕、长时间稳定性和功耗。
- X-TRACK 磁力信息已适配板载 MMC5603NJ，并完成产品 ID、单次测量、数据就绪超时、三轴解码、运行时水平校准和 UI 状态的源码/主机测试/目标构建；尚未实机确认安装轴向、硬铁/软铁校准、磁偏角、倾斜误差、共享 I2C 稳定性和功耗。
- X-TRACK 电源信息已适配板载 GPADC1 channel 7 与片上充电状态，并完成电压范围、低通/回差、错误传播、UI 状态和目标构建；尚未用电池和 USB 实机标定空载/负载电压曲线、充满状态、低电压阈值、ADC 校准精度、30 秒状态延迟和采样功耗，因此当前百分比只能视为电压估算。
- X-TRACK 事件提示音已适配黄山派 Audio Codec/扬声器并完成纯序列主机回归与目标链接；尚未实机确认扬声器通路、响度、音色、首尾截断、提示音与本地音乐/蓝牙音频混音以及休眠和关机边界行为。
- FIT 与路线导航属于其他参考工程扩展，不是 X-TRACK 主线必备项。
