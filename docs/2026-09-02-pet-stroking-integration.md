# Momo 连续抚摸累计集成与门禁记录

## 集成范围

- 集成分支：`codex/pr-pet-stroking`
- 累计基线：`codex/pr-momo-memory-calendar@709087c6a6dbc9e6f3d9eb45cdeb4d7c24344450`
- 原功能分支：`codex/2026-08-14-pet-stroking@a748503581dbf566c9814fef4014301a4c1bece3`
- 产品提交：`e78f4fbeb3496dd10a2dc27f09693a597d8215d9`
- 方案提交：`d02d6ae1026cb189a7d5796b144f25124693d552`
- 开发与累计仲裁提交：`6e628fed05758bab6f4adfb2632274946f25d6d8`

重放开发提交时，文本冲突集中在 `Kconfig.proj` 和宠物应用 `app_pet.c`；
`SConscript` 自动合并。解决结果同时保留自主行为引擎、回忆日历、连续抚摸，
以及当前主分支的语音、图片传输、多 GIF、Agent 状态、木鱼和任务花园路径。

## 统一输入与显示仲裁

1. 连续抚摸只有在开关开启、页面输入有效、回忆面板关闭、基础表情槽、无图传、
   无远端表达覆盖、无打字、Agent 聚合状态为空闲且木鱼隐藏时才允许开始或继续。
2. 打开回忆面板前先禁止并抢占当前抚摸序列；面板打开期间拒绝新抚摸。被抢占
   的物理触摸序列保持“已消费”，不会在同一次抬手后误触发木鱼短击。
3. 远端 `PLAY`/GIF 表达、打字、图片传输、非空闲 Agent 状态或状态读取失败时，
   在行为/状态动画刷新之前取消抚摸并收起光环和缩放；随后同一刷新周期应用当前
   高优先级视觉，不补播被抢占的抚摸反馈。
4. 抚摸识别成功并持有视觉所有权时，行为模型仍可推进和持久化，但不得改写根背景、
   行为标签、行为素材或共享舞台动画。抚摸结束会使已渲染状态失效，下一次刷新恢复
   最新自主状态；不会删除远端状态使用的动画回调。
5. 远端木鱼事件在播放前同样取消抚摸。普通未识别的候选手势不消费短击；识别成功或
   被高优先级抢占的手势才消费后续短击/长按事件，避免一次触摸触发两种互动。
6. 页面退出顺序为：先关闭抚摸输入并取消当前序列，再删除运动、状态、木鱼和每日
   定时器，随后关闭回忆面板、保存并关闭各持久化句柄，最后释放 GIF/图片并删除根对象。
   所有 LVGL 对象、动画和定时器操作仍只发生在 GUI 线程。

## 可关闭门禁与组合

`AGENT_PET_BEHAVIOR_ENGINE`、`AGENT_PET_USING_MEMORY_CALENDAR` 和
`AGENT_PET_USING_STROKE` 是三个相互独立、默认开启的 Kconfig 开关。
`SConscript` 会在对应开关关闭时，从构建图移除行为模块、回忆日历或抚摸状态机。

| 配置 | 编译数据库 | 完整构建 |
| --- | --- | --- |
| 行为 ON、日历 ON、抚摸 ON | 行为素材、日历、抚摸模块均存在 | 通过 |
| 行为 ON、日历 ON、抚摸 OFF | 行为素材和日历存在，无抚摸模块 | 通过 |

抚摸 OFF 使用临时 `CONFIG_AGENT_PET_USING_STROKE=n` 验证；验证完成后已从
`proj.conf` 移除临时配置，版本库最终保持三个功能默认 ON。

## 自动验证结果

| 门禁 | 实际命令 | 结果 |
| --- | --- | --- |
| 连续抚摸主机测试 | `powershell -ExecutionPolicy Bypass -File tests/run_momo_stroking_host_test.ps1` | 通过，GCC `-Wall -Wextra -Werror` |
| 回忆日历主机测试 | `powershell -ExecutionPolicy Bypass -File tests/run_momo_memory_calendar_host_test.ps1` | 通过，GCC `-Wall -Wextra -Werror` |
| 行为引擎主机测试 | `powershell -ExecutionPolicy Bypass -File tests/run_pet_behavior_host_test.ps1` | 通过，MSVC `/W4 /WX` |
| 宠物协议回归 | `powershell -ExecutionPolicy Bypass -File tests/run_agent_pet_protocol_host_test.ps1` | 通过 |
| 宠物音频协议回归 | `powershell -ExecutionPolicy Bypass -File tests/run_agent_pet_audio_protocol_host_test.ps1` | 通过 |
| 任务花园回归 | `powershell -ExecutionPolicy Bypass -File tests/run_agent_quest_garden_host_test.ps1` | 通过 |
| 三功能全 ON 目标固件 | `powershell -ExecutionPolicy Bypass -File .\build.ps1 -c` 后执行 `.\build.ps1` | 通过，退出码 0 |
| 抚摸 OFF 组合目标固件 | 临时关闭抚摸后执行上述 clean 与完整构建 | 通过，退出码 0 |
| 静态门禁 | `git diff --check 709087c..HEAD`、冲突标记与禁用函数扫描 | 通过 |
| PC 模拟器 | `work/watch_bt_audio_template/simulator/build.bat` | 基线阻塞，未到达宠物功能编译 |

目标构建继续输出 SDK/第三方库既有告警，包括 LVGL/音频空指针诊断、蓝牙字符串
长度、FFmpeg 宏重定义、Opus 数组读取以及链接器 RWX/系统调用告警；本次新增抚摸、
累计仲裁代码没有产生新的编译错误或告警。

PC 模拟器先报告固定 VS2017 14.16 和 Windows SDK 10.0.17763 路径不存在，随后
在 SDK 媒体构建图因 `audio_server.h` 缺失及 Opus 的 `/Wno-error` 参数不兼容失败，
退出码 1（内部 SCons 退出码 2）。失败位于宠物源文件之前，因此不能声称模拟器通过，
也未扩大范围修复该基线环境问题。

## 资源结果

`arm-none-eabi-size main.elf` 的累计结果如下：

| 配置 | text | data | bss | dec |
| --- | ---: | ---: | ---: | ---: |
| 行为 ON、日历 ON、抚摸 OFF | 5,506,174 | 16,620 | 4,864,920 | 10,387,714 |
| 行为 ON、日历 ON、抚摸 ON | 5,508,862 | 16,620 | 4,865,012 | 10,390,494 |
| 抚摸累计增量 | +2,688 | +0 | +92 | +2,780 |

状态机使用固定内存，没有新增线程、RTOS 定时器、动态轨迹数组或素材；上述结果仅代表
链接后静态占用，LVGL 光环/提示对象、触摸事件峰值、GIF 解码和实际帧率仍需真机测量。

## 合并后必须回归

以下项目标记为“待硬件验收”：

1. 100 次真实温和抚摸的识别率，以及点击、滑动、长按和页面手势各 100 次的误触率；
   同时覆盖屏幕旋转、不同 GIF 尺寸和触摸采样抖动。
2. 抚摸过程中分别打开回忆面板，或触发远端 `PLAY`、多 GIF、打字、图片传输、四种
   非空闲 Agent 状态和木鱼，确认抚摸立即收起、目标视觉正确接管且结束后恢复最新状态。
3. 在抚摸视觉持续时触发自主状态变化，确认行为模型后台推进但不抢占舞台；抬手后只恢复
   最新状态，无历史动画补播、位置跳变或 GIF 解码异常。
4. 连续 1000 次进入页面、识别抚摸、被抢占、退出页面，检查 LVGL 对象数、系统堆、
   动画和定时器均回到稳态，无 use-after-free、双删、卡死或持续内存增长。
5. 开关掉电保持、持久化失败日志、RTC 未同步和断连状态读取失败路径；确认回忆、行为、
   任务花园与抚摸设置互不污染。
6. 在录音/上传、音频播放、图传和 GIF 并发时测量 RAM/CPU 峰值、触摸延迟、UI 帧率与
   功耗，确认新增 2,780 字节静态占用及运行时对象开销在目标硬件预算内。
