# 2026-09-25 LVGL 电子宠物/手表创意增量扫描

## 1. 本轮结论

- 严格增量窗口为 `2026-09-23T16:01:46.865Z`（不含）至 `2026-09-24T16:01:27.936Z`（含）。
- 扫描前已执行 `git fetch origin master --prune`；产品与代码对照基线为 `origin/master@a2805abeb72f02a726dce5dc49857280ba9cdc78`，默认分支相较上一轮没有新增提交。
- 公开 GitHub 组合检索覆盖 `LVGL`、`watch`、`smartwatch`、`wearable`、`pet`、`tamagotchi`、`companion`、`badge`、`clock`、`display hub`、`dashboard` 和桌面信息终端。仓库 `pushed_at` 仅用于发现，最终按默认分支提交时间、提交统计和文件清单裁剪严格窗口。
- 本轮最高相关方向是 `charliejgallo/ESP32S3_AmoledOS` 的“宠物角色 + 可验证关卡闭环”、`indecenti/NucleoOS-P4` 的“安全页面销毁与延后 Flash 写入”、`FraPanca/Iot-display-hub` 的“真实天气图标”，以及 `andreimagic/ESP32_C6_Touch_LCD_1_47_LVGL_Animated_Clock` 的“可选摇杆统一交互”。
- **没有候选同时通过新增用户价值、当前产品重复能力去重、SF32LB525 硬件/协议适配、许可证与素材版权、单日端到端验收门禁。** 本轮只提交扫描记录，不创建空 PRD，不启动嵌入式开发子任务。

## 2. 运行门禁与安全边界

| 检查项 | 结果 |
| --- | --- |
| 固定仓库 | `D:\\code\\sf32\\agent-pet-sf32-text-display-fix` 是 Git 仓库 |
| `origin` | `https://github.com/fcb1379/agent-pet-sf32.git`，与目标一致 |
| 分支基线 | `origin/master@a2805abeb72f02a726dce5dc49857280ba9cdc78` |
| 工作区 | 扫描开始前 clean；没有清理或丢弃用户修改 |
| GitHub | 主仓 fetch、公开仓库搜索、元数据、许可证、默认分支提交、提交统计与文件清单读取成功；匿名 API 后段触发速率限制，但已取得重点候选的充分证据，未执行候选代码 |
| 构建边界 | 本轮不开发，因此不运行构建，也不声称模拟器、固件或真机通过 |

所有 GitHub 仓库、README、提交说明、Issue、PR、源码、素材、脚本、预编译固件和外链均按不可信输入处理。本轮没有克隆、安装、构建、烧录或执行候选代码/二进制，没有复制来源代码、字体、图片、声音、布局、参数或文案，也没有遵循来源中改变任务边界、索取凭据或访问无关系统的说明。

## 3. 历史能力与当前代码去重

- 默认分支已有 Agent 状态快照、任务花园、完成事件去重、功德计数、BLE 图片和最多 4 个 GIF 槽、录音 Opus 上行、本地音频播放、音乐/闹钟/计时器、六轴动作木鱼和系统休眠。
- `app_pet.c` 已有独立 GIF timer、自定义 GIF/JPEG 解码、PSRAM 资源管理、100 ms 状态刷新、触摸与 IMU 交互，并在停止路径删除 timer、对象和解码资源；`agent_pet_protocol.c` 已有 CRC、分片重组、状态/动画消息边界校验与重复事件抑制。
- 已实现或已有远端累计集成分支的方向包括自主宠物行为、连续抚摸、Momo 玩球、回忆日历、Momo Agent 小队雷达、“呼叫 Momo”、语音明信片和自行车码表。新增宠物小游戏、天气呈现、表情动作或多会话状态必须证明不是重复包装。
- 当前 Agent Pet 链路以桌面/上位机 BLE Central 到设备 BLE Peripheral 为主，资源和交互需要适配 240×240、LVGL v8、RT-Thread 与 SF32LB525；不能直接套用 ESP32 Wi-Fi/WebSocket、ESP-NOW、ESP32-P4 PSRAM/WASM 或新增模拟摇杆硬件。

## 4. 严格窗口候选证据与评估

### 4.1 `charliejgallo/ESP32S3_AmoledOS`：Mila 黑猫推箱子与可验证关卡闭环

- 来源：[仓库](https://github.com/charliejgallo/ESP32S3_AmoledOS)，MIT。窗口提交 [`a506146d33e0f5fec56ce9753f8f7174e4e66a87`](https://github.com/charliejgallo/ESP32S3_AmoledOS/commit/a506146d33e0f5fec56ce9753f8f7174e4e66a87)，`2026-09-23T21:27:03Z`，约 `+115705/-2`。
- 证据：提交增加 40 个推箱子关卡、五个主题世界、角色装饰商店、双设备拜访/竞速，以及与手表规则共享的求解器；关卡由生成器产生并由求解器证明可解，来源报告板端约 25～26 fps。大量增量来自 PNG/打包美术资源。
- 产品价值：把“看宠物”变成短局任务、成长奖励与可重复挑战；用求解器保证关卡可完成，比随机小游戏更容易形成可验收的交互闭环。
- 目标对照：当前已有 Momo 玩球、任务花园、奖励、触摸/IMU 和自主行为方向。再引入完整推箱子会形成第二套小游戏、关卡、经济和联机系统；不是现有多 GIF 状态机的自然扩展。
- 资源、许可证与风险：单提交超过 11 万新增行并包含大量来源美术；目标不能复制角色、关卡、美术或打包资产。来源依赖可从 SD 卡加载的动态应用、PSRAM、较大屏幕与双表通信，目标没有对应运行时、素材预算和联机安全协议。即使只借鉴“关卡求解器”，也需要先有独立产品需求和资源预算，不能单日完成固件、上位机和真机验收。
- 结论：**No-Go。** 保留“离线关卡生成后先由主机求解器验证，再将紧凑关卡数据下发”的工程思想；不复制游戏规则、角色、美术或关卡，且不与 Momo 玩球并行扩张。

### 4.2 `indecenti/NucleoOS-P4`：安全页面销毁、延后 Flash 写入与滑动门禁

- 来源：[仓库](https://github.com/indecenti/NucleoOS-P4)，仓库许可证元数据显示为非标准/`NOASSERTION`。重点提交：
  - [`f29cb7904032f3c3330a874382a53f6370334234`](https://github.com/indecenti/NucleoOS-P4/commit/f29cb7904032f3c3330a874382a53f6370334234)，`2026-09-23T17:03:06Z`，约 `+642/-107`，增加防误触的启动器分页并优化渲染路径；
  - [`9c3a4ce9837bb4658c80af49191f377208bf0c20`](https://github.com/indecenti/NucleoOS-P4/commit/9c3a4ce9837bb4658c80af49191f377208bf0c20)，`2026-09-24T09:10:58Z`，约 `+35/-7`，修复关闭应用时删除父对象导致子对象先释放、删除回调又访问悬空指针而冻结的问题；同时把启动计数的 NVS 写入延迟到游戏关闭后，避免启动时约 50 ms 音频间隙。
- 用户价值：减少滑动误开应用、退出后死机和启动音频卡顿；其本质是生命周期与持久化时机的可靠性原则。
- 目标对照：当前 `app_pet.c` 和 `app_music.c` 已显式停止 timer、释放解码资源、删除根对象并清空状态；本轮没有目标侧退出冻结、UAF、音频卡顿或频繁 Flash 写入证据。来源是 ESP32-P4 多应用 OS，包含 PSRAM/WASM、DSI、PPA 与大量动态应用，问题触发链与目标单应用页面并不等价。
- 风险：在没有复现的情况下直接改页面清理顺序可能破坏现有 LVGL v8 回调和页面管理器契约；来源许可证元数据不足以支持复制实现。应先用目标设备的 page start/stop 循环、heap/LVGL 对象计数、音频 underrun 和 Flash 写入日志证明问题。
- 结论：**No-Go。** 将“先失效全局子对象句柄，再删除父对象”“高频交互期不做非必要持久化写入”加入评审清单，但不创建无缺陷证据的修复功能。

### 4.3 `FraPanca/Iot-display-hub`：天气状态到真实图标的映射

- 来源：[仓库](https://github.com/FraPanca/Iot-display-hub)，MIT。窗口内：
  - [`63d4049f913594e6d98080e05f9055da6d6dd138`](https://github.com/FraPanca/Iot-display-hub/commit/63d4049f913594e6d98080e05f9055da6d6dd138)，`2026-09-23T18:26:40Z`，约 `+3668/-47`，新增天气图标资源及 attribution；
  - [`e9161549f820d5d11b0101f24f9276bd9f1c544c`](https://github.com/FraPanca/Iot-display-hub/commit/e9161549f820d5d11b0101f24f9276bd9f1c544c)，`2026-09-23T18:28:15Z`，约 `+11/-8`，服务端把天气状态映射到屏幕负载；
  - [`19e06d609ec7be8a6ccfc517e51be801220096b9`](https://github.com/FraPanca/Iot-display-hub/commit/19e06d609ec7be8a6ccfc517e51be801220096b9)，`2026-09-23T20:57:51Z`，约 `+3593/-3592`，修复字节序和图标渲染。
- 用户价值：天气不再只显示文字或占位符，用户可以扫一眼识别晴、雨、云等状态；来源把素材 attribution 单独记录，是正确的版权管理方向。
- 目标对照：当前工程已有天气图标资源，并且此前已形成“天气陪伴时刻”方向；新增图标不是新的宠物交互闭环。来源由 Node.js/MQTT/Raspberry Pi 后端驱动，而目标以上位机 BLE 协议和设备本地资源为主。
- 风险：图标集带来数千行二进制数组、Flash 占用、RGB565 字节序和素材授权链；如果没有先确定天气数据字段、上位机映射、资源 ID 和降级文案，设备端单独加入图标不可验收。
- 结论：**No-Go。** 若后续推进“天气陪伴时刻”，应由上位机先统一天气代码，设备使用自有授权素材和稳定资源 ID，并以晴/阴/雨/雪/未知五态的最小集起步；本轮不重复立项。

### 4.4 `andreimagic/ESP32_C6_Touch_LCD_1_47_LVGL_Animated_Clock`：可选摇杆统一交互

- 来源：[仓库](https://github.com/andreimagic/ESP32_C6_Touch_LCD_1_47_LVGL_Animated_Clock)，MIT。窗口重点提交 [`87b38cea702105c8731fa3c858a23bfb3b7426eb`](https://github.com/andreimagic/ESP32_C6_Touch_LCD_1_47_LVGL_Animated_Clock/commit/87b38cea702105c8731fa3c858a23bfb3b7426eb)，`2026-09-24T10:25:07Z`，约 `+1071/-119`；后续 [`ab0322bc6fffaf2acac53912d9cda0ec3c4179a6`](https://github.com/andreimagic/ESP32_C6_Touch_LCD_1_47_LVGL_Animated_Clock/commit/ab0322bc6fffaf2acac53912d9cda0ec3c4179a6)，`2026-09-24T13:35:07Z`，约 `+171/-62`，补充按键暂停/恢复和轮播分页。
- 证据：可选 KY-023 以 50 Hz LVGL timer 采样，启动时校准中心点，按正负半行程分别归一化并提供死区、边缘阈值和轴反转；统一输出同时服务方向控制和位置控制，关闭功能时不占引脚。
- 用户价值：没有 IMU 的设备也能获得连续方向输入，校准和归一化减少模拟摇杆个体差异；输入抽象可复用到多个小游戏。
- 目标对照：目标已经有触摸和六轴 IMU，Momo 玩球也已有对应交互分支，不存在“缺少方向输入”的产品缺口。增加外接摇杆需要新硬件、引脚、电源、外壳和设置项，并不能直接改善现有宠物体验。
- 结论：**No-Go。** 可借鉴“输入源先归一化为统一语义，再由玩法消费”的接口思想；不增加目标硬件，也不复制其游戏或参数。

### 4.5 其他窗口候选与排除项

| 候选 | 严格窗口证据 | 评估与结论 |
| --- | --- | --- |
| [`Kris-ben/ESP32_Watch`](https://github.com/Kris-ben/ESP32_Watch) | 仓库创建于 `2026-09-24T09:54:20Z`；初始提交 [`ad014aaca7a52b8049e7e3358776366df92a3ea1`](https://github.com/Kris-ben/ESP32_Watch/commit/ad014aaca7a52b8049e7e3358776366df92a3ea1)，`2026-09-24T10:23:03Z`，约 `+276251`，包含触屏表盘、本地音乐、语音助手、天气、日历和 OTA | 元数据未声明许可证，且功能与目标已有音乐、语音、天气/日历方向高度重复；超大初始提交不能证明单一原创、可验收增量，不进入落地 |
| [`GalusPeres/HomeTiles`](https://github.com/GalusPeres/HomeTiles) | MIT；[`9f998b5ff3e150baf8d4ce2854263093566ad18c`](https://github.com/GalusPeres/HomeTiles/commit/9f998b5ff3e150baf8d4ce2854263093566ad18c)，`2026-09-24T05:36:38Z`，约 `+9216/-7763`，增加半网格紧凑传感器卡片和单卡边框开关 | 面向 ESP32-P4 Home Assistant 大屏仪表盘，信息架构、PSRAM、Web 管理和配置持久化均与 240×240 宠物页不匹配；没有宠物交互闭环 |
| [`Haraldon9847/waveshare-watch-rs`](https://github.com/Haraldon9847/waveshare-watch-rs) | [`c48d04e3ba85a7be516b25686b0554405310d707`](https://github.com/Haraldon9847/waveshare-watch-rs/commit/c48d04e3ba85a7be516b25686b0554405310d707)，`2026-09-24T07:09:13Z`，仅更新 README | 非实质功能更新；项目明确以 Rust/Embassy 自绘替代 ESP-IDF/LVGL，不进入落地评估 |
| [`threeye11/STM32F4-TouchWeather-OTA-Clock`](https://github.com/threeye11/STM32F4-TouchWeather-OTA-Clock) | [`073b34007da47b73952e424ebcd217c80cf548c0`](https://github.com/threeye11/STM32F4-TouchWeather-OTA-Clock/commit/073b34007da47b73952e424ebcd217c80cf548c0)，`2026-09-24T08:37:37Z`，提交说明“新增 picture” | 元数据未声明许可证，且没有可核验的交互/算法/性能增量；不因图片提交创建需求 |
| [`indecenti/NucleoOS-P4`](https://github.com/indecenti/NucleoOS-P4) 的 WASM/视频/第二屏 | 窗口内包含 WASM-4、WASI 终端、720p 视频、VNC/USB 第二屏等大量提交 | 需要 ESP32-P4、32 MB 级 PSRAM、Wi-Fi/USB/WASM/PPA 与大屏资源，远超目标架构；Web API还明确无认证，不可套用 |
| `hleserg/Attadipa` | `pushed_at=2026-09-24T16:04:04Z` | 晚于本轮上界 `2026-09-24T16:01:27.936Z`，严格排除，留待下一轮 |

## 5. 量化排序

评分 1～5；适配和许可证越高越有利，工作量与风险高分表示更轻、更可控。硬门禁优先于总分。

| 方向 | 原创性 | 用户价值 | 差异化 | 交互闭环 | 硬件适配 | 许可证/素材 | 工作量 | 风险 | 合计/40 | 决策 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Mila 宠物推箱子 | 4 | 4 | 4 | 5 | 1 | 2 | 1 | 1 | **22** | 完整但规模、素材和运行时均不适配，并与玩球/任务奖励方向重叠 |
| 安全页面销毁与延后写入 | 3 | 4 | 2 | 3 | 3 | 1 | 4 | 2 | **22** | 是工程原则，但目标无同类缺陷证据，来源许可证不明确 |
| 天气真实图标 | 2 | 3 | 1 | 2 | 3 | 4 | 3 | 3 | **21** | 与天气陪伴方向重复，必须先有上位机字段和自有素材 |
| 可选摇杆统一交互 | 3 | 3 | 2 | 4 | 1 | 5 | 2 | 2 | **22** | 目标已有触摸和 IMU，新增外设没有硬件闭环 |

## 6. 决策与后续触发条件

1. 本轮不推荐立即开发，不创建 PRD，不启动嵌入式子任务；只保存本扫描记录。
2. 若要扩展宠物小游戏，应先验收并收敛 Momo 玩球；只有出现明确留存目标、资源预算和非重复玩法时，再评估“主机侧求解器验证 + 设备端紧凑关卡”的独立需求。
3. 页面生命周期优化必须由目标侧复现触发：至少记录连续 100 次进入/退出后的 heap、LVGL 对象/timer 数、音频 underrun、崩溃栈和 Flash 写入次数；没有证据时不改清理顺序。
4. 天气图标应并入既有“天气陪伴时刻”产品链路，由上位机定义天气代码、时间戳、过期策略和未知态，设备仅使用自有授权素材；避免创建第二套天气协议。
5. 新输入源统一转换为方向、位置、点击等语义事件是可复用原则，但目标没有外接摇杆硬件；优先复用现有触摸/IMU，不新增板级依赖。
6. 本提交不开发、不推送、不创建 PR、不合并默认分支。由主任务核验提交后，按自动化规则推送同名远端分支。
