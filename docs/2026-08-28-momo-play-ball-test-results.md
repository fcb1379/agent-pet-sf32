# Momo 玩球测试记录

- 执行日期：2026-08-31
- 目标分支：`codex/2026-08-28-momo-play-ball`
- 产品提交：`5ce9b245213b77c2b2a2fff1860368edd9f2b657`
- 设计提交：`db2b91d8257f28850a6354a2f75ca95ad82bee5e`
- 固件基线：`origin/master@a2805abeb72f02a726dce5dc49857280ba9cdc78`
- 目标板：`sf32lb52-lchspi-ulp`，实际编译配置 `390 x 450`

## 结论

MVP 已通过纯状态机主机测试、现有宠物相关主机回归、Kconfig OFF/ON 完整目标编译和最终 ON 增量复编。新状态机与宠物页面均使用宠物模块现有的 `-Wall -Wextra -Werror` 门禁编译，未产生新警告。功能只增加本地静态状态、两个 LVGL 对象和一个初始暂停的 50 ms LVGL 定时器；不增加线程、协议、上位机依赖、持久化或外部素材。

PC 全应用模拟器进入真实 MSVC 编译后，被 SDK 既有 CmBacktrace PC CPU 平台配置门禁阻断，尚未形成可运行程序。触摸手感、系统边缘返回手势、真机帧率和退出重入仍需硬件验收。

## 构建环境

- Arm 编译器：`arm-none-eabi-gcc 14.2.1 20241119`
- SCons：`C:\Users\woan\.sifli\python_env\sifli-sdk2.4_py3.10_env\Scripts\scons.exe`
- 主机测试：WSL Ubuntu `cc 11.4.0`
- SDK：仓库内 `sdk`
- 最终配置：`CONFIG_AGENT_PET_MOMO_PLAY_BALL=y`

生成的 `rtconfig.h` 已核验：

```text
#define LCD_HOR_RES_MAX 390
#define LCD_VER_RES_MAX 450
#define AGENT_PET_MOMO_PLAY_BALL 1
```

## 目标固件完整构建

实际执行命令（工作目录 `work/watch_bt_audio_template/project`）：

```powershell
$env:SIFLI_SDK='<repo>\sdk'
$env:RTT_CC='gcc'
$env:RTT_EXEC_PATH='C:\Users\woan\.sifli\tools\arm-none-eabi-gcc\14.2.1\bin'
$env:PYTHONPATH='<repo>\sdk\tools\build;<repo>\sdk\tools\build\default'
& 'C:\Users\woan\.sifli\python_env\sifli-sdk2.4_py3.10_env\Scripts\scons.exe' `
  --board=sf32lb52-lchspi-ulp --board_search_path=../boards -j8
```

结果：

- Kconfig OFF 完整构建：通过；确认 `rtconfig.h` 不包含 `AGENT_PET_MOMO_PLAY_BALL`。
- Kconfig ON 完整构建：通过；`app_pet.c` 与 `momo_play_ball.c` 均通过宠物模块严格警告门禁。
- 恢复 ON 后最终增量构建：通过；重新编译 `momo_play_ball.o` 并链接 `main.elf/main.bin/main.hex`。
- 最终功能配置保持 ON，便于分支验收；回滚只需移除 `proj.conf` 中的一行配置。

完整工程仍输出基线 SDK/第三方警告，包括 FlashDB 空指针诊断、蓝牙路径参数越界诊断、部分音频/编解码器诊断，以及链接阶段 RWX、未实现 syscall、ftab entry/dfu 提示。OFF 与 ON 构建均存在这些警告；最终增量输出中与本功能相关的唯一编译单元 `momo_play_ball.c` 为零警告。未把基线警告误报为本功能通过项。

## OFF/ON 资源对比

实际命令：

```powershell
& 'C:\Users\woan\.sifli\tools\arm-none-eabi-gcc\14.2.1\bin\arm-none-eabi-size.exe' `
  'build_sf32lb52-lchspi-ulp_hcpu\main.elf'
```

| 配置 | text | data | bss | dec |
|---|---:|---:|---:|---:|
| OFF | 5,497,138 B | 16,620 B | 4,864,640 B | 10,378,398 B |
| ON | 5,499,474 B | 16,620 B | 4,864,744 B | 10,380,838 B |
| 增量 | **+2,336 B** | **+0 B** | **+104 B** | **+2,440 B** |

推送前安全修复后使用相同工具链重新完成 OFF/ON 全量构建。纯状态结构在主机 ABI 下为 84 B，并有 `_Static_assert(sizeof(MOMO_PLAY_BALL) < 256U)` 编译期上限。其余 20 B BSS 增量来自页面中的指针、实际 timer tick、布尔成员及对齐；LVGL 对象内部内存由既有 LVGL 对象系统管理，不计入静态 BSS 差值。

## 纯状态机主机测试

实际命令：

```powershell
.\tests\run_momo_play_ball_host_test.ps1
```

脚本实际使用：

```text
cc -std=c11 -Wall -Wextra -Werror -pedantic \
  tests/momo_play_ball_host_test.c \
  work/watch_bt_audio_template/src/gui_apps/pet/momo_play_ball.c
```

结果：`PASS momo_play_ball_host_test state=84 bytes deterministic=5 rounds=200`

覆盖：

- 无效边界使状态机保持禁用；NULL 更新安全返回。
- 球外按下拒绝；无有效速度采样的释放复位。
- `uint32_t` tick 回绕下仍能取得有效拖动采样。
- 确定性验证四边精确反弹。
- 确定性验证有效速度后静止 8 ms、超过 200 ms 均使旧样本失效；有效样本后小于 8 ms 松手仍可进入运动。
- 确定性验证第 5,000 ms 触发 `RESET`，且此前保持运动。
- 确定性验证 `CAUGHT` 仅触发一次，随后 650 ms 保持反馈，第 700 ms 精确 `RESET`。
- 200 轮固定种子拖拽、释放、运动、反弹、追逐、捕获反馈和停止/超时复位。
- 每一步断言球和 Momo 坐标均未越过配置边界。

## 现有回归

以下脚本均通过：

```text
.\tests\run_agent_pet_protocol_host_test.ps1
PASS agent_pet_protocol_host_test

.\tests\run_agent_pet_audio_protocol_host_test.ps1
agent_pet_audio_protocol_host_test: PASS

.\tests\run_agent_quest_garden_host_test.ps1
PASS agent_quest_garden_host_test
```

## PC 模拟器尝试

先按仓库说明执行 `simulator/build.bat`，系统 Python 可运行但缺少 SCons：

```text
No module named SCons
```

随后使用仓库已安装的 SiFli Python/SCons，并先执行 `simulator/msvc_setup.bat` 建立 MSVC 2022 与 Windows SDK 映射：

```powershell
$env:SIFLI_SDK='<repo>\sdk'
$env:PYTHONPATH='<repo>\sdk\tools\build'
& 'C:\Users\woan\.sifli\python_env\sifli-sdk2.4_py3.10_env\Scripts\python.exe' `
  -m SCons --board=pc -j8
```

构建进入 MSVC 编译后，SDK v2.4 的 PC board 脚本仍尝试旧 VS2017/Windows SDK 路径，并最终在未选择 CmBacktrace CPU platform 时停止：

```text
sdk\external\CmBacktrace-v1.3.0\cmb_cfg.h(52): fatal error C1189:
#error: "You must select a CPU platform on menuconfig"
```

失败发生在 SDK 的 `cm_backtrace.obj/cmb_port.obj`，早于宠物页面编译，属于当前模拟器基线环境限制。未修改 SDK 或绕过配置门禁，因此不声称 PC 模拟器通过。

## 静态安全与并发检查

- 纯 C 状态机无 `malloc/free`、文件、协议或外设访问；所有输入指针先校验。
- 位置、速度、时间和碰撞参数均有边界；乘法在进入除法前提升至 `int64_t`。
- 速度上限 520 px/s，单次更新最大 100 ms，轮次最大 5 s，反馈 700 ms。
- tick 采样使用无符号减法，允许自然回绕；静止达到 8 ms 或异常长采样会主动清除旧释放速度。timer 也按实际无符号 tick 差推进，零差最小为 1 ms，状态机上限仍为 100 ms。
- 仅 LVGL 事件和 LVGL 定时器回调访问 LVGL；没有新增 RTOS 线程、锁、队列或 ISR。
- 页面仅有一个 50 ms 定时器，空闲时暂停；退出时先禁止交互、取消状态，再删除定时器并清空对象指针。
- 球半径 13 px、扩展点击区 11 px，球心左右边界额外计入两者，使实际命中区距离两侧屏幕边界均为 24 px。未增加覆盖全屏的可点击层。
- 球只分别注册 `PRESSED/PRESSING/RELEASED/PRESS_LOST` 四类事件，不注册 `LV_EVENT_ALL`；回调在读取 indev 前再次过滤事件码，避免位置或样式事件误取消游戏。
- Cancel 会清除实际 timer tick、隐藏反馈并恢复橙色球；图片、多 GIF 或 Agent 状态在绿色反馈期抢占时不会泄漏反馈样式。
- Agent 非 IDLE、打字、图片接收、非基础多 GIF 槽或外部动画更新会取消本轮并恢复原动画，避免争用 stage 坐标和 LVGL 动画。

## 待硬件验收

以下项目必须在 390 x 450 真机完成，当前标记为 **待硬件验收**：

1. 从宠物页反复进入/退出 50 次，无残留定时器、悬空对象或异常内存增长。
2. 左右边缘返回手势稳定，球的扩展点击区不吞掉系统手势。
3. 拖拽、快速/慢速抛球、四边反弹、追逐和接球反馈符合手感预期。
4. 连续玩球 10 分钟，观察帧率、触摸延迟、CPU 占用、功耗和温升。
5. 在 PLAY、打字、图片/GIF 传输、多 GIF 切换和外部表情动画中切换，确认本地玩球被正确中止且原动画恢复。
6. 接球、停球和 5 s 超时后均能回到确定性初始位置；睡眠/唤醒后可重新交互。
7. 在后续 240 x 240 兼容板上验证活动区仍有效；本次主验收只以实际 390 x 450 目标板为准。
