# 自主宠物行为引擎集成与门禁记录

## 集成范围

- 集成分支：`codex/pr-pet-behavior-engine`
- 基线：`origin/master@a2805abeb72f02a726dce5dc49857280ba9cdc78`
- 原功能分支：`codex/2026-08-07-pet-behavior-engine@0345d06b5434e0a43da6dd03fe54d7094d216a9e`
- 合并提交：`b52a528`（保留原 PRD、实现方案、测试记录、主机测试和功能代码）

唯一的文本冲突位于 `app_pet.c` 的页面状态结构体。解决时同时保留当前主分支的图片传输进度节拍 `ulImageProgressTick` 与行为持久化节拍 `ulLastBehaviorSaveTick`，未覆盖主分支的新路径。

## 当前主分支兼容性

本次集成保留以下现有能力：

- Agent 远端状态与 0～4 号多 GIF 表情槽位；
- 远端 `PLAY` 表情覆盖、打字态及表情动画结束事件；
- 图片传输、校验、进度和槽位状态显示；
- 语音转写、音频上传、本地录音及播放；
- 木鱼点击和 IMU 撞击计数。

行为引擎只在没有远端表情覆盖、没有打字态时刷新自主状态素材。所有 LVGL 对象更新仍由宠物页面的 GUI 定时器或 GUI 事件回调执行。模型事件从 IMU/页面回调注入；调试命令访问模型时使用 RT-Thread 中断锁保护，不在非 GUI 线程操作 LVGL 对象。

## 可关闭门禁

新增 `AGENT_PET_BEHAVIOR_ENGINE` Kconfig，默认开启：

- ON：编译行为模型、素材映射、持久化、状态标签和 `petmood` 调试命令；
- OFF：从构建图移除 `pet_behavior.c` 与 `pet_state_assets.c`，关闭行为模型、持久化和调试命令；保留原 Agent 状态、多 GIF、打字态、图片传输、音频和木鱼行为。

OFF 构建的 `rtconfig.h` 不含该宏；`compile_commands.json`、`main.map` 和 `main.elf` 符号表均不含行为模型或素材映射。构建目录中可能保留上一次 ON 构建产生的旧 `.o`，但不会参与 OFF 编译或链接。

## 自动验证结果

| 门禁 | 命令 | 结果 |
| --- | --- | --- |
| 行为模型主机测试 | `powershell -ExecutionPolicy Bypass -File tests/run_pet_behavior_host_test.ps1` | 通过，MSVC `/W4 /WX` |
| 宠物协议回归 | WSL GCC，`-Wall -Wextra -Werror` 编译并运行 `agent_pet_protocol_test.c` | 通过 |
| 宠物音频协议回归 | WSL GCC，`-Wall -Wextra -Werror` 编译并运行 `agent_pet_audio_protocol_test.c` | 通过 |
| 任务花园回归 | WSL GCC，`-Wall -Wextra -Werror` 编译并运行 `agent_quest_garden_test.c` | 通过 |
| 目标固件 ON | 仓库根目录 `powershell -ExecutionPolicy Bypass -File .\\build.ps1` | 通过 |
| 目标固件 OFF | 临时设置 `CONFIG_AGENT_PET_BEHAVIOR_ENGINE=n` 后执行同一完整构建 | 通过 |
| PC 模拟器 | `work/watch_bt_audio_template/simulator/build.bat` | 未通过：SDK `middleware/media/media_dec.h` 无法找到 `audio_server.h`，在编译到宠物功能前由既有 PC 构建图中止 |

目标固件资源对比：

| 配置 | text | data | bss | dec |
| --- | ---: | ---: | ---: | ---: |
| OFF | 5,497,170 | 16,620 | 4,864,648 | 10,378,438 |
| ON | 5,501,334 | 16,620 | 4,864,720 | 10,382,674 |
| 增量 | +4,164 | +0 | +72 | +4,236 |

完整构建仍会输出 SDK/第三方库的既有告警，例如 FlashDB/LVGL 空指针诊断、蓝牙字符串长度、FFmpeg 宏重定义、Opus 数组读取诊断以及链接器 RWX/系统调用告警；本次功能文件没有产生新的编译错误或告警。PC 模拟器失败点位于 SDK 媒体模块的包含路径，不能据此声称完成模拟器自测，也不影响目标固件 ON/OFF 已通过的结论。

## 真机验收与风险

以下项目必须标记为“待硬件验收”，不能由主机测试或编译结果替代：

1. 连续运行 24 小时，确认行为状态切换、GIF 播放和 LVGL 定时器无卡死或内存持续增长；
2. 在图片传输、远端表情 `PLAY`、打字态、录音/上传期间触发 IMU 事件，确认远端内容优先且结束后恢复自主状态；
3. 断电重启验证心情、精力和亲密度持久化，确认 Flash 写入节拍不会影响 UI 响应；
4. 测量 ON/OFF 的峰值 RAM、UI 帧率、CPU 占用和续航差异；
5. 验证每个状态 GIF 素材的授权、尺寸、帧率和解码稳定性。

固件采用定时合并保存和页面关闭保存以降低 Flash 写放大，但真实 Flash 延迟、掉电一致性与寿命仍需真机长期测试。
