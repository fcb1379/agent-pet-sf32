# Momo 回忆日历累计集成与门禁记录

## 集成范围

- 集成分支：`codex/pr-momo-memory-calendar`
- 累计基线：`codex/pr-pet-behavior-engine@e1a5a69549b3576307a14ebc47fbad3b47665899`
- 原功能分支：`codex/2026-08-18-momo-memory-calendar@9974c91e98e5c1dd870380337161d3c43264cf1f`
- 产品提交：`e8a4c3d`（PRD 与扫描记录）
- 方案提交：`a21afde`（实现方案）
- 开发提交：`2a67579`（实现、主机测试和累计集成适配）

重放开发提交时，文本冲突集中在 `Kconfig.proj`、宠物应用 `SConscript` 和
`app_pet.c`。解决结果同时保留自主行为引擎、回忆日历和当前主分支的语音、
图片传输、多 GIF、Agent 状态、木鱼及任务花园路径。

## 统一显示仲裁

回忆面板与宠物主画面共用同一个 LVGL 页面，必须由 GUI 线程统一仲裁：

1. 面板打开期间，行为模型仍推进并按既有节拍持久化，但不得更新状态标签、
   根背景、行为 GIF 或状态动画，避免隐藏层消耗解码资源或抢占面板。
2. 图片正在接收、打字态、远端 `PLAY` 请求非基础表情槽，或聚合 Agent 状态为
   `RUNNING`、`NEEDS_INPUT`、`COMPLETED`、`ERROR` 时，禁止新开面板；若面板已开，
   立即关闭，当前高优先级状态在同一个刷新周期接管主画面。
3. 面板关闭后将已渲染 Agent 状态、行为状态和行为素材槽标记为无效；下一次刷新
   必须读取最新状态恢复动画，不补播面板期间的历史状态，也不复用过期素材。
4. 普通自主状态变化不会关闭面板；模型只在后台累计，用户退出面板后看到最新
   自主状态。
5. 页面退出时先停止运动、状态、木鱼和每日定时器，再关闭面板；随后按行为、任务
   花园、回忆日历的顺序保存并关闭持久化句柄，最后由页面框架删除根对象。所有
   LVGL 创建、隐藏和删除操作仍只发生在 GUI 定时器或 GUI 事件回调中。

## 可关闭门禁与组合

`AGENT_PET_BEHAVIOR_ENGINE` 和 `AGENT_PET_USING_MEMORY_CALENDAR` 是相互独立的
Kconfig 开关，均默认开启。`SConscript` 会在对应开关关闭时从构建图移除
`pet_behavior.c`/`pet_state_assets.c` 或 `momo_memory_calendar.c`。

本次重点验证了两种累计配置：

| 配置 | `rtconfig.h` | 编译数据库 | 完整构建 |
| --- | --- | --- | --- |
| 行为 ON、日历 OFF | 仅定义 `AGENT_PET_BEHAVIOR_ENGINE` | 有行为与素材模块，无日历模块 | 通过 |
| 行为 ON、日历 ON | 两个宏均定义 | 三个模块均存在 | 通过 |

日历 OFF 使用临时 `CONFIG_AGENT_PET_USING_MEMORY_CALENDAR=n` 验证；完成后已移除
临时配置并重新 clean build，分支最终保持默认双功能 ON。

## 自动验证结果

| 门禁 | 实际命令 | 结果 |
| --- | --- | --- |
| 回忆日历主机测试 | `powershell -ExecutionPolicy Bypass -File tests/run_momo_memory_calendar_host_test.ps1` | 通过，GCC `-Wall -Wextra -Werror` |
| 行为引擎主机测试 | `powershell -ExecutionPolicy Bypass -File tests/run_pet_behavior_host_test.ps1` | 通过，MSVC `/W4 /WX` |
| 宠物协议回归 | `powershell -ExecutionPolicy Bypass -File tests/run_agent_pet_protocol_host_test.ps1` | 通过 |
| 宠物音频协议回归 | `powershell -ExecutionPolicy Bypass -File tests/run_agent_pet_audio_protocol_host_test.ps1` | 通过 |
| 任务花园回归 | `powershell -ExecutionPolicy Bypass -File tests/run_agent_quest_garden_host_test.ps1` | 通过 |
| 日历 OFF 目标固件 | `powershell -ExecutionPolicy Bypass -File .\\build.ps1 -c` 后执行 `.\\build.ps1` | 通过，退出码 0 |
| 双功能 ON 目标固件 | 恢复默认后再次执行上述 clean 与完整构建 | 通过，退出码 0 |
| 静态门禁 | `git diff --check e1a5a69..HEAD` 与冲突标记扫描 | 通过 |
| PC 模拟器 | `work/watch_bt_audio_template/simulator/build.bat` | 基线阻塞，未到达宠物功能编译 |

PC 模拟器使用当前 MSVC 后仍在 SDK 媒体构建图失败：
`sdk/middleware/media/media_dec.h` 找不到 `audio_server.h`；并且 Opus 编译参数
`/Wno-error` 不被 MSVC 接受。构建开始时还会报告固定 VS2017/Windows SDK 路径不存在。
这些均位于本功能代码之前，不能据此声称模拟器通过。

目标构建会继续输出 SDK/第三方库既有告警，包括 LVGL/FlashDB/音频空指针诊断、
蓝牙字符串长度、FFmpeg 宏重定义、Opus 数组读取以及链接器 RWX/系统调用告警；
本次新增行为、日历和仲裁代码没有产生新的编译错误或告警。

## 资源结果

`arm-none-eabi-size main.elf` 的累计结果如下：

| 配置 | text | data | bss | dec |
| --- | ---: | ---: | ---: | ---: |
| 行为 ON、日历 OFF | 5,501,334 | 16,620 | 4,864,720 | 10,382,674 |
| 行为 ON、日历 ON | 5,506,174 | 16,620 | 4,864,920 | 10,387,714 |
| 日历累计增量 | +4,840 | +0 | +200 | +5,040 |

该结果只代表链接后静态占用；LVGL 面板对象、GIF 解码峰值和实际运行帧率仍需真机测量。

## 合并后必须回归

以下项目标记为“待硬件验收”：

1. 打开回忆面板后持续触发自主状态变化，确认面板不闪烁、不被行为 GIF 抢占；退出
   后一次恢复最新状态，无历史动画补播。
2. 分别在面板打开时触发远端 `PLAY`、打字、图片传输和四种非空闲 Agent 状态，
   确认面板立即关闭、对应动画正确显示，状态结束后恢复最新自主状态。
3. 在面板创建/关闭、页面退出和快速重复进出期间检查 LVGL 对象生命周期，确认无
   use-after-free、双删、空指针、卡死或持续内存增长。
4. 跨日、RTC 未同步、断电重启、连续记录超过环形容量时检查日期归档、持久化恢复
   和过期记录覆盖；同时确认行为与任务花园持久化互不污染。
5. 在录音/上传、音频播放和图片接收并发场景测量峰值 RAM、CPU、UI 帧率与功耗，
   确认新增 5,040 字节静态占用及面板运行开销在硬件预算内。
