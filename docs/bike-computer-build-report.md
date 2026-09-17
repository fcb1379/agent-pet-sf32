# SF32 码表基础移植构建与验证报告

日期：2026-09-17

分支：`codex/feature-bike-computer`

目标：在 `sf32lb52-lchspi-ulp` 开发板上建立 X-TRACK 功能移植的首个可运行基线，包括 DX-GP10 NMEA 输入、骑行数据模型和 LVGL 骑行主页。

## 1. 已实现范围

- DX-GP10 使用 UART2，默认 9600 bps、8N1；首版引脚为 PA20/RX、PA27/TX。
- 固定长度 NMEA 接收缓冲，支持 GGA/RMC、异或校验、超长语句丢弃和错误计数。
- 使用定点数保存经纬度、速度、航向、海拔和 UTC 时间，避免在串口接收路径中使用浮点运算。
- 骑行模型支持开始、暂停、停止、里程、当前/平均/最大速度、移动/总时间和卡路里估算。
- LVGL 页面显示速度、里程、平均速度、移动时间、海拔和 GNSS 状态，并提供开始/暂停/停止操作。
- GNSS 接收线程只持有静态栈和静态缓冲；串口回调只释放信号量，共享快照使用互斥锁保护。

## 2. 主机测试

测试对象：NMEA 解析器和骑行数据模型。

```text
gcc -std=c11 -Wall -Wextra -Werror ... -lm -o /tmp/bike_core_test
bike_core_test: PASS
```

覆盖内容：

- 合法 GGA/RMC 解析及单位转换。
- 坏校验语句拒绝。
- NMEA 分段输入。
- 骑行状态切换、移动时间、里程、平均速度和异常跳点过滤。

## 3. SF32 整机编译

构建目标：`sf32lb52-lchspi-ulp` HCPU。

```text
scons --board=sf32lb52-lchspi-ulp --board_search_path=../boards -j8
scons: done building targets.
```

主要产物：

| 产物 | 大小 |
|---|---:|
| `main.bin` | 3,380,480 bytes |
| `fs_root.bin` | 4,194,304 bytes |

编译仍输出 SDK/原模板已有的 FlashDB、LVGL、蓝牙音频和汇编兼容性警告；新增 `bike_*` 模块在 `-Werror` 主机测试中无告警，且目标编译成功。

## 4. 资源占用

HCPU ELF 链接结果：

| 区域 | 当前占用 | 链接容量 | 余量 |
|---|---:|---:|---:|
| 片上 SRAM 地址范围 | 335,756 bytes | 523,264 bytes | 187,508 bytes |
| PSRAM 可写段 | 2,649,000 bytes | 8,388,608 bytes | 5,739,608 bytes |

码表新增对象文件在链接前的直接占用：

| 模块 | `.text` | `.bss` |
|---|---:|---:|
| `bike_nmea.o` | 1,488 bytes | 0 bytes |
| `bike_ride_model.o` | 800 bytes | 0 bytes |
| `bike_service.o` | 1,236 bytes | 3,553 bytes |
| `app_bike.o` | 1,875 bytes | 36 bytes |
| 合计 | 5,399 bytes | 3,589 bytes |

`bike_service.o` 的 `.bss` 包含 3,072 bytes GNSS 线程静态栈。对象文件合计只用于描述新增模块的直接体积，不等同于最终镜像增量；最终镜像还受链接消除、库引用和资源打包影响。

## 5. 验证边界

本轮已确认：源码静态检查、核心算法主机测试、SF32 HCPU 完整链接和镜像生成。

本轮未确认：

- 未烧录开发板，不能声明页面、触摸、UART2 或休眠唤醒已通过实机验证。
- 未取得转接板导出网表，PA20/PA27、3V3、GND 以及 PPS/WAKE/RESET 的实际网络仍需硬件复核。
- 未接 DX-GP10，尚未验证真实 NMEA 连续输入、首次定位时间、丢星恢复、天线性能和整机功耗。
- X-TRACK 的 GPX 轨迹记录、离线地图、BLE 传感器和导航等后续功能尚未移植。
