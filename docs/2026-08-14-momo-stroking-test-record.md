# 摸摸 Momo 连续抚摸互动自测记录

## 1. 测试对象

- 分支：`codex/2026-08-14-pet-stroking`
- 产品提交：`34be90107b63bbd3c9524844d0449c922274c105`
- 实现方案提交：`f1c7644d9fb800e57f74074766e611d52ed59999`
- 基线：`origin/master@a2805abeb72f02a726dce5dc49857280ba9cdc78`
- SDK：`90e575f7ec253b6a879d816460a16546ebd2582f`
- 蓝牙子模块：`b0a2cb714c383a407d08c43e7fbecf39f732185c`
- 日期：2026-08-14

## 2. 主机状态机与四组回归

以下命令均在仓库根目录执行。主机编译器使用 `-std=c11 -Wall -Wextra -Werror`，本机未发现 clang/gcc 时由脚本使用 WSL `cc`。

| 命令 | 退出码 | 结果 |
| --- | ---: | --- |
| `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/run_momo_stroking_host_test.ps1` | 0 | `PASS momo_stroking_host_test` |
| `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/run_agent_pet_protocol_host_test.ps1` | 0 | `PASS agent_pet_protocol_host_test` |
| `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/run_agent_pet_audio_protocol_host_test.ps1` | 0 | `agent_pet_audio_protocol_host_test: PASS` |
| `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/run_agent_quest_garden_host_test.ps1` | 0 | `PASS agent_quest_garden_host_test` |

新增状态机用例覆盖：650 ms 停留、350 ms 温和轨迹、12/160/161 px 总路径、80/81 px 单步、69%/70% 热区占比、热区与舞台边界、空指针与非法配置、普通候选取消和高优先级抢占、消费标志跨同一物理序列保持且下一次 `PRESSED` 清除、32 位 tick 回绕、100 次正向识别、100 组负向序列和 1000 次生命周期压力循环。

## 3. 目标固件构建

仓库的常规 SDK 激活链在当前 Windows 环境执行 `sdk/tools/activate.py --export` 时返回 `Support for platform Windows- hasn't been added`，退出码 1。未绕过依赖版本；改用本机已安装且与 SDK 配套的 SiFli Python/SCons 和 Arm GCC 路径，设置 `SIFLI_SDK`、`SIFLI_SDK_PATH`、`RTT_CC=gcc`、`RTT_EXEC_PATH`、`PATH`、`PYTHONPATH` 后执行：

```powershell
C:\Users\woan\.sifli\python_env\sifli-sdk2.4_py3.10_env\Scripts\scons.exe `
  --board=sf32lb52-lchspi-ulp --board_search_path=../boards -j8
```

工作目录：`work/watch_bt_audio_template/project`。

- 完整首次 ON 构建：目标文件、`main.elf`、`main.bin` 均生成；工具调用超过 120 秒返回窗口，但后续同配置增量构建退出码 0。
- 最终 ON 构建（包含 `PRESS_LOST` 即时取消修正）：退出码 0，重新编译 `app_pet.o` 并链接生成 `main.elf/main.bin/main.hex`。
- 新增 `app_pet.c` 和 `momo_stroking.c` 的编译命令包含 `-Wall -Wextra -Werror`，未报告功能源文件警告。
- 构建仍报告基线/SDK 既有信息：ftab 中未定义 `dfu` 镜像，以及链接产物存在 RWX LOAD segment。首次完整构建还可见 Opus/Newlib 等依赖告警；本功能没有扩大修复范围。
- 最终生成配置确认：`.config` 为 `CONFIG_AGENT_PET_USING_STROKE=y`，`rtconfig.h` 为 `#define AGENT_PET_USING_STROKE 1`。

## 4. 功能开关 A/B 资源测量

在同一分支、同一工具链、同一 board 下，仅临时切换 `AGENT_PET_USING_STROKE`，每次均执行上述 SCons 命令；临时配置通过 `finally` 恢复，版本库中的 `proj.conf` 无差异。`arm-none-eabi-size main.elf` 结果：

| 配置 | text | data | bss | 构建退出码 |
| --- | ---: | ---: | ---: | ---: |
| OFF | 5,497,138 B | 16,620 B | 4,864,640 B | 0 |
| ON | 5,499,730 B | 16,620 B | 4,864,732 B | 0 |
| ON - OFF | +2,592 B | 0 B | +92 B | - |

`arm-none-eabi-nm` 复核 OFF 固件不包含 `MOMOSTROKE_*` 符号，ON 固件包含状态机接口。新增功能未创建线程、RTOS timer、动态轨迹数组或素材；LVGL 侧只创建一个 halo 对象和一个提示 label，LVGL 对象堆占用需在真机用运行时统计确认。

## 5. PC 模拟器结论

执行命令：

```bat
cd work\watch_bt_audio_template\simulator
cmd.exe /d /c build.bat
```

退出码 2，**PC 模拟器未通过**。失败发生在进入 Pet 功能源文件编译之前：脚本硬编码的 VS2017 14.16 与 Windows SDK 10.0.17763 路径在当前主机不存在，随后 CmBacktrace 报 `#error You must select a CPU platform`。本分支相对 `origin/master` 未修改 `simulator/build.bat`、`msvc_setup.bat`、`SConstruct` 或 `SConscript`；因此记录为基线工具链/平台配置阻塞，不能声称模拟器通过，也未扩大修复。

## 6. 静态审查

- 状态机固定内存，`_Static_assert` 限制上下文不超过 256 B；没有保存外部指针，没有动态分配。
- 所有距离、计数均做饱和处理，时间差使用无符号减法支持 32 位 tick 回绕。
- LVGL 事件、对象、动画和持久化开关均只在 Pet 页面 GUI 回调/timer 中访问，没有跨线程调用 LVGL。
- 连续抚摸只删除本功能唯一 `PET_SetStrokeZoom` 动画并清除独立样式状态，不删除/复位 Agent、PLAY、typing、图传使用的共享舞台动画或坐标。
- 高优先级抢占会消费当前物理序列，后续同序列 `SHORT_CLICKED`/长按事件被停止；普通候选失败不消费，也不手工补发短击，因此原木鱼短击最多触发一次。
- `PRESS_LOST`、舞台外坐标即时取消；正常 `RELEASED` 保留 150 ms 释放尾迹。
- 数组/坐标转换有边界检查；新增生产代码未引入 `malloc/free/gets/strcpy/sprintf/scanf`。

## 7. 待硬件验收

以下项目没有真实触摸屏、蓝牙链路和目标设备，均明确为“待硬件验收”，不得视为已通过：

1. 100 次真实温和抚摸的识别率，以及 100 次点击/滑动/页面手势的误触率。
2. 连续 1000 次进入、抚摸、抢占、离开页面后的 LVGL 堆、系统堆和对象数是否回到稳态。
3. Agent 状态、远端 PLAY、多 GIF/图传、typing、任务花园与木鱼同时到来时的优先级和恢复视觉。
4. 触摸坐标在屏幕旋转、缩放、不同 GIF 尺寸下与头部热区的实际对齐。
5. 开关掉电保持、持久化写失败日志，以及异常退出后的默认回退。
6. 真机刷新率、触摸采样抖动、CPU 占用和 LVGL 堆峰值。
