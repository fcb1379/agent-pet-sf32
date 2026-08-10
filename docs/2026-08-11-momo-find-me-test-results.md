# 呼叫 Momo 实现与测试记录

## 1. 结论

端到端代码链路已实现，自动构建通过，真机端到端待验收：Android 原生入口、HWS1 能力探测与
START/STOP/STATUS、固件固定内存状态机、60 秒单调超时、GUI 顶层覆盖层、
设备点击/HOME 停止、闹钟抢占和异步 END 事件均已接通。目标固件完整构建
通过；纯 C、HWS1、既有回归及纯 Java reducer 测试通过。

本版本不播放声音，不修改音量或音乐队列。HWS1 当前未认证，因此本功能仅限
内部视觉验证，不能按量产安全能力发布。

## 2. 实际构建与测试

| 命令 | 退出码 | 结果 |
| --- | ---: | --- |
| `tests\\run_momo_find_me_host_test.ps1` | 0 | MSVC `/utf-8 /W4 /WX`；`momo_find_me_host_test: PASS` |
| `wsl sh -lc "... && sh tests/run_watch_protocol_host_test.sh"` | 0 | `watch protocol host tests passed` |
| `tests\\run_agent_pet_protocol_host_test.ps1` | 0 | 既有 Agent Pet 协议回归通过 |
| `tests\\run_agent_quest_garden_host_test.ps1` | 0 | 既有任务花园回归通过 |
| `tests\\build-flash-utils.test.ps1` | 0 | 9 组构建/烧录辅助测试通过 |
| `tests\\run_android_find_momo_state_host_test.ps1` | 0 | `FindMomoStateHostTest: PASS` |
| JDK `javac -source 17 -target 17 -cp android-35/android.jar ...` | 0 | Android 四个原生 Java 源文件编译通过；仅既有 deprecated API 提示 |
| 固定 SiFli 环境后执行 `build.ps1` | 0 | 目标 main.elf/bin/hex 构建成功 |

目标构建使用：

- Python/SCons：`C:\\Users\\woan\\.sifli\\envs\\default\\...\\python\\Scripts`
- GCC：`C:\\Users\\woan\\.sifli\\tools\\arm-none-eabi-gcc\\14.2.1\\bin`
- `SIFLI_SDK=<repo>\\sdk`
- `PYTHONPATH=<repo>\\sdk\\tools\\build`
- `RTT_EXEC_PATH=<gcc-bin>`、`RTT_CC=gcc`

目标构建只有 SDK/基线既有警告：Python Kconfig escape、ELF RWX segment、
newlib `_close/_lseek/_read/_write` 桩和 ftab entry；新增源码没有编译警告。

## 3. 覆盖矩阵

- 同 session START 重复 100 次：deadline、generation、accepted count 不变。
- STOP 重复 100 次：状态保持 STOPPING，结束事件只生成一次。
- active 不同 session、错误 session STOP、结束后迟到 START、65535 到 1 回绕。
- 32 位 tick 回绕、59999/60000 ms 边界、remaining ceil 溢出边界。
- 闹钟忙、badge 图传忙、Agent Pet ingress busy 协议源；生产实现用 pending
  packet count 封闭 BEGIN 入队到 worker 发布状态的窗口。
- ARMING 窗口闹钟抢占、ACTIVE 闹钟抢占、mailbox 投递失败回滚。
- 24 组 alarm/transfer/user-stop 优先级交错。
- Android 旧固件隐藏、点击防抖、重连 STATUS、陈旧 END 丢弃、忙响应复位。

## 4. 资源 A/B 实测

同一 SDK/GCC 对 `origin/master@75bcc5720a00c46efa924ba41b5b8c5c53a52622`
和功能分支构建：

| 版本 | text | data | bss | dec |
| --- | ---: | ---: | ---: | ---: |
| baseline | 4,116,465 | 12,052 | 3,972,764 | 8,101,281 |
| Find Momo | 4,118,033 | 12,052 | 3,972,844 | 8,102,929 |
| 净增 | **1,568** | **0** | **80** | **1,648** |

满足方案的 `text + rodata <= 20 KiB`、`data + bss <= 1 KiB` 预算。

## 5. 未完成/受环境阻塞项

- Android Gradle `:app:testDebugUnitTest :app:assembleDebug`：未通过。JDK 21
  运行时包含 `java/javac` 但缺 `jlink.exe`，AGP 在
  `JdkImageTransform` 失败；因此 JUnit 6 组未由 Gradle 执行，APK 未生成。
- PC `simulator\\build.bat`：baseline 同命令在 CmBacktrace PC 配置即失败；
  临时禁用该基线项后，本功能两个新增 `.obj` 均编译成功，但 SDK PC link
  命令又传入空 `.obj` 并 LNK1181。未把基线构建问题混入功能提交，故 PC
  overlay 50 轮烟测未执行。

## 6. 待硬件验收

1. Pet、时钟、音乐三页分别呼叫并点击停止，原页面/Agent/GIF/天气/自主状态不变。
2. 连续 50 次呼叫/停止，观察 LVGL 对象、堆和手势是否稳定。
3. 真机验证 60 秒超时、断连后继续计时、重连 STATUS、异步 END。
4. 闹钟与计时器在 ARMING/ACTIVE 时触发，覆盖层让位且提醒正常。
5. badge 与五槽 GIF/image BEGIN 入队窗口均返回 BUSY，不写坏文件。
6. 当前目标未启用 `BSP_USING_PM`，watch_demo 的睡屏路径未编译，目标常亮，
   无需主动唤醒。未来启用低功耗时必须同时启用 `GUI_APP_PM`，否则只能亮屏后
   看见覆盖层，并应由 Kconfig 禁止不完整组合。
7. 验证本功能全程无提示音、音量 0 行为一致、音乐/蓝牙媒体队列不变。
