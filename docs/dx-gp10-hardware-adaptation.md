# DX-GP10 与 SF32LB52 硬件适配说明

日期：2026-09-17

数据手册：DX-GP10 GPS 模块技术手册 V2.0，2024-06-18；DX-GP10-A 载板规格以厂家 2025 年资料及用户转接板网络为准。

板载资源依据：[黄山派开发板官方 Wiki](https://wiki.sifli.com/board/sf32lb52x/SF32LB52-%E9%BB%84%E5%B1%B1%E6%B4%BE.html)、[SiFli 传感器适配说明](https://docs.sifli.com/projects/solution/7.FAQ/peripheral/sensor.html) 与 [MEMSIC MMC5603NJ Rev.B 数据手册](https://www.memsic.com/Public/Uploads/uploadfile/files/20220119/MMC5603NJDatasheetRev.B.pdf)。

## 1. 模组与载板接口

用户转接板安装的是 `DX-GP10-A` 载板，不是直接焊接 18 引脚 LCC 裸模组。下表用于说明载板内部 DX-GP10 裸模组信号；开发板实际连接应以 DX-GP10-A 的 `VDD/RXD/TXD/GND/1PPS/WAKE` 接口和转接板网络为准。

| DX-GP10 引脚 | 名称 | 方向 | 首版用途 |
|---:|---|---|---|
| 1, 10, 12 | GND | 电源 | 与开发板共地 |
| 2 | TXD | 输出 | 经 DX-GP10-A `TXD` 接 SF32 UART3_RX |
| 3 | RXD | 输入 | 经 DX-GP10-A `RXD` 接 SF32 UART3_TX，用于可选 PCAS 配置命令 |
| 4 | 1PPS | 输出 | 首版不接入中断，后续用于 RTC/时间精度验证 |
| 5 | WAKE_UP | 输入 | 默认内部上拉；首版悬空，后续接 GPIO 做待机控制 |
| 6 | VCC_BACKUP | 电源 | 可悬空；需要热启动时接 1.4~3.6 V 备电 |
| 8 | VCC_MAIN | 电源 | 裸模组内部主电源，推荐 3.3 V；由 DX-GP10-A 载板供电电路处理 |
| 9 | RESET | 输入 | 低有效，低电平至少 100 ms；首版悬空 |
| 11 | GNSS_ANT | RF | 50 ohm 天线通道 |
| 13 | ANT_ON | 输出 | 不作 GPIO 使用，首版悬空 |
| 14 | VCC_RF | 输出 | 有源天线供电/检测；不用时悬空 |
| 18 | SET | 输入/输出 | 默认悬空，选择 GPS+BeiDou |

注意：数据手册表 3 的 RESET 行误写为 8 脚；引脚图和 2.6 节均为 9 脚，且 8 脚已明确为 VCC_MAIN。

用户 EasyEDA 工程 `黄山派_GPS_转接板_V1.0_双层下单版` 已确认以下实际网络：

- `PA35_GPS_TX`：SF32 PA35/UART3_TX 接 DX-GP10-A `RXD`。
- `PA36_GPS_RX`：DX-GP10-A `TXD` 接 SF32 PA36/UART3_RX。
- `GPS_PPS`、`GPS_WAKE` 已在转接板上引出；当前固件尚未为它们分配 MCU GPIO。
- 供电网络包含 `5V_RAW`、`GPS_5V`、`VSYS`、`VCC_3V3` 和 `GND`；DX-GP10-A 的 `VDD` 使用载板允许的 3.6~6 V 输入，设计目标为 5 V。

## 2. 串口参数

- NMEA 0183，默认 9600 bps。
- 1 起始位、8 数据位、无校验、1 停止位。
- 默认 1 Hz；首版不发送波特率或更新率修改命令。
- 首版解析 `RMC` 和 `GGA`，接受 `GP`、`GN`、`BD`、`GB` talker ID。
- 每条语句先验证 `*XX` 异或校验，再更新共享快照；超长、断句、坏校验和未知语句均丢弃。

## 3. SF32LB52 资源分配

| 功能 | 当前分配 | 依据/冲突 |
|---|---|---|
| 调试日志 | UART1，PA18/PA19 | 现有板级配置，保留 |
| GNSS | UART3，PA36(RX)/PA35(TX) | 用户转接板 `PA36_GPS_RX`/`PA35_GPS_TX` 网络；与 USB DP/DM 复用，GNSS 使用期间必须保持 USB Device/Host 关闭 |
| TF 卡 | SPI1，PA24/PA25/PA28/PA29 | 不与 GNSS 复用 |
| 板载 IMU | I2C2，PA39(SDA)/PA40(SCL) | 当前 QADSPI 屏幕不占用；LSM6DS3TR-C 地址 0x6A |
| 板载磁力计 | I2C2，PA39(SDA)/PA40(SCL) | 与 IMU 共总线；MMC5603NJ 地址 0x30、产品 ID 0x10 |
| 电池检测 | GPADC1 内部 VBAT channel 7，设备 `bat1` | SF32LB52 内部连接，不占用外部 GPIO |
| 充电状态 | 片上 PMU charger，设备 `charge` | 读取 VBUS ready 与 EOC，不修改充电参数 |
| 1PPS | 转接板已引出 `GPS_PPS`，MCU GPIO 未分配 | 后续选择空闲 GPIO/中断并补充固件支持 |
| WAKE_UP | 转接板已引出 `GPS_WAKE`，MCU GPIO 未分配 | 后续选择空闲 GPIO；悬空时使用载板默认状态 |
| RESET | 未接入 MCU | DX-GP10-A 对外接口未提供 RESET，若需控制须修改硬件 |

TF 卡软件侧已启用 SDK SPI-MSD：PA24 为 DIO/MOSI、PA25 为 DI/MISO、PA28 为 CLK、PA29 为 CS，块设备名为 `sd0`。应用只尝试把已有 FAT 文件系统挂载到 `/sd`，失败时回退内部存储，不会自动格式化卡片；真实卡座焊接、供电和异常拔卡仍需最终实机阶段确认。

板载 LSM6DS3TR-C 软件侧使用 SiFli SDK 的 LSM6DSL 兼容驱动，仅注册 step 设备。初始化前校验 0x0F WHO_AM_I 为 0x6A，开启后回读 0x10 ODR 与 0x19 功能/计步使能位；应用再以独立、可检查返回值的 I2C 事务读取 0x4B/0x4C 步数。PA39/PA40 只在当前 QADSPI LCD 配置下复用，若改用 8080 DBI 屏，计步初始化会直接报错以避免引脚冲突。

板载 MMC5603NJ 使用同一 I2C2 总线。项目驱动先读取 0x39 产品 ID，再执行软件复位，以 5 Hz 单次测量方式启用自动 SET/RESET，轮询数据就绪后完整读取 0x00~0x08 的 9 字节 20 位三轴数据。SDK 自带 MMC56x3 封装分配 8 字节缓冲却访问第 9 字节，并在 I2C 失败时断言，因此本项目不启用该封装，改用所有事务均检查返回值的固定缓冲实现。航向只在 X/Y 两轴运行时校准跨度和样本数达标后发布；安装朝向、磁偏角、硬铁/软铁干扰和倾斜补偿留待最终实机标定。

电池检测使用 SF32LB52 固定内部 VBAT channel 7。板级配置显式启用 GPADC1 并把 charger 的检测配置指向 `bat1`/channel 7；应用读取 SDK 校准后的 0.1 mV 数值，每 30 秒短时使能 ADC，校验读取长度后立即关闭。电量百分比沿用 X-TRACK 的 3.3~4.1 V 线性映射，并增加低通和 2% 回差；它只用于界面估算，真实电池型号、负载压降和温度曲线仍需实机标定。外部供电与充满状态只读片上 `charge` 设备，不改变板载充电电流或目标电压。

软件把 UART 设备名、波特率和 PA35/PA36 映射集中在 GNSS 端口层。板级配置启用 UART3 RX DMA，并保持 USB Device/Host 关闭，避免 PA35/PA36 的 USB DP/DM 复用冲突。

## 4. 电源与功耗边界

- DX-GP10-A 载板 `VDD` 允许 3.6~6 V 输入，用户转接板按 `GPS_5V`/`5V_RAW` 网络供电；上电前需实测极性和电压。
- DX-GP10 裸模组 `VCC_MAIN` 仅允许 2.7~3.4 V、推荐 3.3 V，不能绕过载板把 5 V 直接送入裸模组。
- UART 信号侧按 3.3 V IO 连接 SF32；实机前仍需测量空闲电平，确认载板没有输出 5 V 逻辑。
- GPS/BeiDou 连续追踪典型约 24.3 mA；GPS 连续追踪典型约 22.5 mA。
- Standby 典型约 20 uA，备份域典型约 9 uA；这些是模组数据手册值，不代表整机功耗。
- 天线和模块应远离 LCD、DC/DC、高速 SPI 和金属遮挡；最终需要在整机形态下验证冷启动、重捕获和弱信号性能。

## 5. 尚未闭环的硬件验收

- 实物连接器方向、PA35/PA36 交叉连接和 GND 连通性。
- `GPS_5V` 上电电压、DX-GP10-A 载板稳压输出及 UART 空闲电平。
- `GPS_PPS`、`GPS_WAKE` 是否需要接入 MCU，以及对应 GPIO/中断资源分配。
- 天线类型、馈电方式和安装方向。
- 上电后 30 分钟连续 NMEA、断开重连、冷/热启动和实际整机电流。
- MMC5603NJ 产品 ID、三轴方向、板级安装朝向、水平转动校准、磁干扰、倾斜误差和采样功耗。
- `bat1` channel 7 的空载/负载电压、ADC 校准精度、电池百分比曲线、低电压行为、USB/VBUS 和 EOC 状态以及 30 秒采样功耗。
