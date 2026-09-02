# Momo 玩球四功能累计集成与门禁记录

## 集成范围

- 集成分支：`codex/pr-momo-play-ball-stack`
- 累计基线：`codex/pr-pet-stroking@bc960a75d32d210ba4d5e2b3e1dfaf58c3e3ce40`
- 原功能分支：`codex/2026-08-28-momo-play-ball@1ce6ad3`
- 产品提交：`6041782`（PRD 与扫描记录）
- 方案提交：`2163c37`（嵌入式实现方案）
- 开发提交：`8a6e60f`（玩球实现与主机测试）
- 审查修复提交：`1afe5d8`（热区、事件注册、定时器与生命周期修复）

重放时解决了宠物应用 `SConscript` 和 `app_pet.c` 的冲突，并保留当前主分支的
语音、图片传输、多 GIF、Agent 状态、木鱼、任务花园，以及累计的自主行为、
回忆日历和连续抚摸路径。Kconfig 四个功能开关保持相互独立且默认开启。

## 四方互动与远端状态仲裁

1. 回忆面板打开前先禁止并取消连续抚摸和玩球；面板打开期间拒绝二者新输入，
   自主行为模型可后台推进，但不能改写当前视觉。
2. 玩球进入 `DRAGGING`、`MOVING` 或 `FEEDBACK` 后取得视觉所有权，拒绝抚摸启动，
   并冻结自主行为对根背景、状态标签和舞台动画的更新。
3. 连续抚摸已取得触摸所有权时拒绝球启动或拖动；一次物理触摸不会同时触发
   抚摸、玩球和木鱼。所有仲裁与 LVGL 对象操作仍在 GUI 线程执行。
4. 远端 `PLAY`/GIF、打字、图片传输、非空闲 Agent 状态和状态读取失败会同步
   取消抚摸与玩球并关闭回忆面板；同一刷新周期由高优先级视觉接管。远端状态
   结束后重新读取当前最新状态，不补播被抢占的本地动画。
5. 统一仲裁器为无副作用的纯状态决策模块，并用可执行主机测试覆盖默认、面板、
   玩球、抚摸、远端抢占及空指针分支，避免只依赖静态代码检查。
6. 页面退出严格执行：先禁止抚摸和玩球输入，再取消两类互动，删除玩球定时器，
   然后停止其他状态更新、关闭回忆面板与持久层，最后释放图片/GIF并删除根对象。

审查修复保持不变：球体视觉直径为 26 px，外扩点击区为 11 px，因此从球心计算
的实际触摸半径为 24 px；玩球对象只注册 `PRESSED`、`PRESSING`、`RELEASED`、
`PRESS_LOST` 四类输入事件，不使用全事件回调。

## 自动验证结果

| 门禁 | 实际命令 | 结果 |
| --- | --- | --- |
| 玩球状态机 | `powershell -ExecutionPolicy Bypass -File tests/run_momo_play_ball_host_test.ps1` | 通过，状态 84 字节，5 轮共 200 次确定性检查 |
| 四方互动仲裁 | `powershell -ExecutionPolicy Bypass -File tests/run_pet_interaction_arbiter_host_test.ps1` | 通过，`-Wall -Wextra -Werror -pedantic` |
| 连续抚摸 | `powershell -ExecutionPolicy Bypass -File tests/run_momo_stroking_host_test.ps1` | 通过 |
| 回忆日历 | `powershell -ExecutionPolicy Bypass -File tests/run_momo_memory_calendar_host_test.ps1` | 通过 |
| 自主行为 | `powershell -ExecutionPolicy Bypass -File tests/run_pet_behavior_host_test.ps1` | 通过 |
| 宠物协议 | `powershell -ExecutionPolicy Bypass -File tests/run_agent_pet_protocol_host_test.ps1` | 通过 |
| 宠物音频协议 | `powershell -ExecutionPolicy Bypass -File tests/run_agent_pet_audio_protocol_host_test.ps1` | 通过 |
| 任务花园 | `powershell -ExecutionPolicy Bypass -File tests/run_agent_quest_garden_host_test.ps1` | 通过 |
| 四功能全 ON 目标固件 | `powershell -ExecutionPolicy Bypass -File .\build.ps1 -c` 后执行 `.\build.ps1` | 通过，退出码 0 |
| 玩球 OFF、前三项 ON 目标固件 | 临时关闭玩球后执行上述 clean 与完整构建 | 通过，退出码 0 |
| PC 模拟器 | `work/watch_bt_audio_template/simulator/build.bat` | 环境基线阻塞，未到达宠物功能编译 |

玩球 OFF 组合的编译数据库中 `momo_play_ball.c` 为 0 项，而 `momo_stroking.c`、
`momo_memory_calendar.c`、`pet_state_assets.c`、`pet_interaction_arbiter.c` 均存在；
说明关闭玩球不会移除前三项功能或统一仲裁器。验证结束后 `proj.conf` 已恢复
`CONFIG_AGENT_PET_MOMO_PLAY_BALL=y`，版本库最终保持四功能默认 ON。

PC 模拟器仍受既有环境阻塞：固定 VS2017 14.16 和 Windows SDK 10.0.17763 路径
不存在，SDK 媒体构建图缺少 `audio_server.h`，Opus 又使用 MSVC 不接受的
`/Wno-error` 参数。失败发生在宠物源文件之前，不能声称模拟器通过，也不能归因于
本次集成。目标固件构建中的 FlashDB/LVGL、蓝牙字符串、FFmpeg/CMSIS、Opus 和
链接器告警均来自既有 SDK/第三方代码；宠物分组以 `-Wall -Wextra -Werror` 编译，
本次新增源文件没有产生新的编译告警或错误。

## 资源结果

`arm-none-eabi-size main.elf` 的累计结果如下：

| 配置 | text | data | bss | dec |
| --- | ---: | ---: | ---: | ---: |
| 行为/日历/抚摸 ON、玩球 OFF | 5,509,086 | 16,620 | 4,865,012 | 10,390,718 |
| 四功能全 ON | 5,511,454 | 16,620 | 4,865,116 | 10,393,190 |
| 玩球累计增量 | +2,368 | +0 | +104 | +2,472 |

该结果是链接后静态占用。球对象、反馈标签、LVGL 定时器、GIF 解码峰值、触摸延迟、
实际帧率及音频/图传并发峰值仍需在目标硬件测量。

## 合并后必须回归

以下项目标记为“待硬件验收”：

1. 分别完成拖拽、投掷、接住、碰壁与反馈，确认 24 px 实际热区、四类事件回调和
   不同屏幕方向下坐标裁剪正确；连续 200 次操作无球跳变、丢失或误触木鱼。
2. 玩球各阶段尝试连续抚摸，并在抚摸已识别时尝试拖球；确认只有当前所有者响应，
   松手或反馈结束后能恢复最新自主状态。
3. 玩球或抚摸期间打开回忆日历，确认互动立即取消、面板稳定；面板打开期间尝试
   两类输入均不响应，关闭后下一次新触摸可正常开始互动。
4. 在三类本地互动期间分别触发远端 `PLAY`、多 GIF、打字、图片传输和四种非空闲
   Agent 状态，确认本地状态同步清理、高优先级视觉正确接管，结束后无历史补播。
5. 连续 1000 次进入页面、开始/抢占互动、退出页面，检查玩球定时器、LVGL 对象、
   动画、持久化句柄和系统堆均回到稳态，无 use-after-free、双删或持续内存增长。
6. 在录音/上传、音频播放、图传和 GIF 解码并发时测量 RAM/CPU 峰值、UI 帧率、
   触摸响应与功耗，确认新增 2,472 字节静态占用和运行时对象开销符合硬件预算。
