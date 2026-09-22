# 2026-09-23 LVGL 电子宠物/手表创意增量扫描

## 1. 本轮结论

- 严格增量窗口为 `2026-09-21T16:01:21.873Z`（不含）至 `2026-09-22T16:00:53.616Z`（含）。
- 扫描前已执行 `git fetch --prune origin`；产品与代码对照基线为 `origin/master@a2805abeb72f02a726dce5dc49857280ba9cdc78`，默认分支相较上一轮没有新增提交。
- 公开 GitHub 组合检索覆盖 `LVGL`、`watch`、`smartwatch`、`wearable`、`pet`、`tamagotchi`、`badge`、`clock`、`companion`、`ESP32` 和桌面信息终端。仓库 `pushed_at` 仅用于发现，最终使用默认分支 Commit API 的提交时间、提交统计与文件清单裁剪严格窗口。
- 本轮最高相关方向是 `curisama/The-Badge` 的“关屏后仍保持闹钟调度，并用模拟器按行为验证”；另有祈念提醒手表、低功耗手表设置持久化、双表联机赛车和圆屏 Home Assistant 控制台等候选。
- **没有候选同时通过新增用户价值、目标缺陷或需求证据、历史能力去重、当前硬件/协议适配、许可证与素材版权、单日端到端验收门禁。** 本轮只提交扫描记录，不创建空 PRD，不启动嵌入式开发子任务。

## 2. 运行门禁与安全边界

| 检查项 | 结果 |
| --- | --- |
| 固定仓库 | `D:\\code\\sf32\\agent-pet-sf32-text-display-fix` 是 Git 仓库 |
| `origin` | `https://github.com/fcb1379/agent-pet-sf32.git`，与目标一致 |
| 分支基线 | `origin/master@a2805abeb72f02a726dce5dc49857280ba9cdc78` |
| 工作区 | 扫描开始前 clean；没有清理或丢弃用户修改 |
| GitHub | 主仓 fetch、公开仓库搜索、元数据、许可证、默认分支提交、提交统计和文件清单读取成功 |
| 构建边界 | 本轮不开发，因此不运行构建，也不声称模拟器、固件或真机通过 |

所有 GitHub 仓库、README、提交说明、Issue、PR、源码、素材、脚本、预编译固件和外链均按不可信输入处理。本轮没有克隆、安装、构建、烧录或执行候选代码/二进制，没有复制来源代码、字体、图片、声音、布局、参数或文案，也没有遵循来源中改变任务边界、索取凭据或访问无关系统的说明。

## 3. 历史能力与当前代码去重

- 默认分支已有 Agent 状态、任务花园、完成事件去重、功德计数、打字动画、BLE 图片与多 GIF、录音 Opus 上行、本地音频播放、音乐/闹钟/计时器、六轴动作木鱼和系统休眠。
- 已实现或已有远端累计集成分支的方向包括：自主宠物行为、连续抚摸、Momo 玩球、回忆日历、Momo Agent 小队雷达和“呼叫 Momo”。新增宠物养成、表情状态、触摸反馈、日历或 Agent 状态方向必须证明不是重复包装。
- 当前 `watch_alarm_service.c` 已使用 RT-Thread alarm、独立事件线程、持久化闹钟设置、音频提示与 snooze；`app_alarm.c` 已有响铃页面。候选的“关屏仍调度”更适合作为目标现有闹钟的专项真机回归项，而不是新功能。
- 当前设备仍以桌面/上位机 BLE Central 连接设备 BLE Peripheral 为主要链路。双表联机、离线社交卡和 Wi-Fi/MQTT 控制台依赖目标默认配置并不具备的协议、身份与安全链路。

## 4. 严格窗口候选证据与评估

### 4.1 `curisama/The-Badge`：关屏后仍保持闹钟调度

- 来源：[仓库](https://github.com/curisama/The-Badge)，MIT。窗口提交 [`1189c309f319d72a5eb780a3b56cdc51593f7a92`](https://github.com/curisama/The-Badge/commit/1189c309f319d72a5eb780a3b56cdc51593f7a92)，`2026-09-22T09:23:46Z`，约 `+125/-3`，修改 alarm、launcher、模拟器构建和行为检查脚本。
- 证据：来源项目为了省电在关屏时停止 idle timer，意外同时停止闹钟 tick；修复仅在有已启用闹钟时保留秒级调度，并用模拟器统计关屏期间 tick，从修复前 `0` 次变为修复后 `8` 次，同时恢复应用源代码编译警告门禁。
- 用户价值：闹钟在设备放入口袋、屏幕关闭时仍能准时响铃，是基础可靠性闭环；“有闹钟才保活”也比无条件轮询更符合低功耗目标。
- 目标对照：目标已有 RT-Thread alarm、独立事件线程、音频提示和持久化设置，不依赖 LVGL idle timer 计时；目前没有“关屏后不响”或 RTC/PM 唤醒失败的目标日志、真机复现与功耗数据。直接移植来源的定时器策略既不适配目标架构，也可能增加无必要唤醒。
- 结论：**No-Go。** 不作为新功能立项；把“亮屏、关屏、GUI suspend、轻睡/深睡边界下的闹钟与 snooze 真机验收”加入既有闹钟回归清单。只有目标真机复现失败并定位到 PM/唤醒链路后，才以缺陷修复方式单独处理。

### 4.2 `abda-s/tasbeeh-watch`：离线提醒与睡眠可见性

- 来源：[仓库](https://github.com/abda-s/tasbeeh-watch)，仓库元数据未声明 SPDX 许可证。窗口内有多次实质更新：[`c494953653eb39ca2308ae20ae4734b73f5b824d`](https://github.com/abda-s/tasbeeh-watch/commit/c494953653eb39ca2308ae20ae4734b73f5b824d) 于 `2026-09-21T22:48:45Z` 增加运行真实 UI 的桌面模拟器；[`fea56c4678a9c7c86850dc4f3b05f178c61597d6`](https://github.com/abda-s/tasbeeh-watch/commit/fea56c4678a9c7c86850dc4f3b05f178c61597d6) 于 `2026-09-22T14:01:21Z` 增加浏览器 WASM 模拟器；[`d92140403540396da6d17727e65cabd3f9db8c64`](https://github.com/abda-s/tasbeeh-watch/commit/d92140403540396da6d17727e65cabd3f9db8c64) 于 `2026-09-22T15:55:38Z` 增加轻睡/深睡区分、tap-to-wake 提示和提醒配置。
- 用户价值：离线提醒、振动反馈、睡眠状态可见性与可操作提示形成明确闭环；真实 UI 共用模拟器也有较好的工程验证价值。
- 当前不落地：目标已经有功德计数、木鱼、闹钟/计时器、系统休眠和 PC 模拟器，本轮增量主要是来源自身的模拟器与网页可用性完善；没有新的宠物陪伴核心体验。更重要的是仓库未声明可复用许可证，不能复制其实现、界面、提示文案或素材。
- 结论：**No-Go。** 可吸收“睡眠状态必须可观察、唤醒动作必须可发现”的验收原则，但不创建新功能分支。

### 4.3 `duli07556-sudo/low-power-smartwatch`：设置和每日步数持久化

- 来源：[仓库](https://github.com/duli07556-sudo/low-power-smartwatch)，仓库元数据未声明 SPDX 许可证。窗口提交 [`32f99b3056f4`](https://github.com/duli07556-sudo/low-power-smartwatch/commit/32f99b3056f4)，`2026-09-22T06:25:50Z`，约 `+156/-21`，把设置与每日步数写入外部 EEPROM；随后提交 [`64e769a89262`](https://github.com/duli07556-sudo/low-power-smartwatch/commit/64e769a89262) 更新持久化架构图。
- 用户价值：断电后保留亮度、时间与步数，降低重复配置成本；按日期滚动每日步数可形成轻量长期反馈。
- 当前不落地：目标已经用 `prefdb`/FlashDB 持久化闹钟、时区与产品设置，宠物与资源状态也有独立持久化路径；目标仓没有已确认的步数产品需求与准确性基线。外部 EEPROM 方案不适配当前存储架构，且来源许可证未明确。
- 结论：**No-Go。** 仅在未来明确引入计步闭环时，先定义传感器算法、日期切换、写放大和准确性验收，再复用目标自身持久化服务独立实现。

### 4.4 `charliejgallo/ESP32S3_AmoledOS`：双表联机伪 3D 赛车

- 来源：[仓库](https://github.com/charliejgallo/ESP32S3_AmoledOS)，MIT。窗口提交 [`fcb54fd2185753b0e37443bf90e76f2ae6c306cb`](https://github.com/charliejgallo/ESP32S3_AmoledOS/commit/fcb54fd2185753b0e37443bf90e76f2ae6c306cb)，`2026-09-22T04:36:12Z`，新增多赛道赛车、PSRAM 渲染和双表 ghost 联机；后续 [`b68e154a0de190887b4c6a1d8461fc234e1a971f`](https://github.com/charliejgallo/ESP32S3_AmoledOS/commit/b68e154a0de190887b4c6a1d8461fc234e1a971f) 增加第四帧缓冲并报告板端帧率与 PSRAM 余量，`fa4ac4547ec974c12ce52cebef756ed87b9228a4` 再增加赛道与可变协议。
- 用户价值：两只设备同步比赛，邀请、游玩、成绩和复玩闭环完整；资源按赛道加载和版本化链路有工程参考价值。
- 当前不落地：目标没有来源依赖的动态 `.so` 应用框架、数 MB 卡包、PSRAM 帧缓冲预算与双设备可靠安全链路；赛车偏离宠物陪伴主线，且美术/模型资源需要逐项来源审计。此前双表游戏已经因同一协议门禁判为 No-Go。
- 结论：**No-Go。** 等双设备身份、密钥、重放、版本协商和两台真机共存门禁完成后，再考虑轻量合作玩法。

### 4.5 小型候选与严格窗口排除项

| 候选 | 严格窗口证据 | 评估与结论 |
| --- | --- | --- |
| [`fbiego/chronos-watchy`](https://github.com/fbiego/chronos-watchy) | [`a70021224acd6f4407402e7ff0eb017f18474c66`](https://github.com/fbiego/chronos-watchy/commit/a70021224acd6f4407402e7ff0eb017f18474c66)，`2026-09-22T08:01:18Z`，`+13/-12`；修复日期文本标签并更新预编译固件 | 仅小型修复和二进制发布，没有新的陪伴交互；不执行或导入预编译固件 |
| [`FraPanca/Iot-display-hub`](https://github.com/FraPanca/Iot-display-hub) | [`8ccd81e79c99`](https://github.com/FraPanca/Iot-display-hub/commit/8ccd81e79c99)，`2026-09-22T13:55:44Z`，`+608/-5`；新增 Wi-Fi/MQTT、封面下载和 OTA 管理 | 目标默认配置没有 Wi-Fi/MQTT；天气、音乐、系统状态与 OTA 均已有或有独立路径，不创建平行协议栈 |
| [`baes-cloud/esp-rotary-display-ha-hub`](https://github.com/baes-cloud/esp-rotary-display-ha-hub) | [`af28277cc3e1`](https://github.com/baes-cloud/esp-rotary-display-ha-hub/commit/af28277cc3e1)，`2026-09-22T11:23:25Z`，初始发布约 `+6543`；双圆屏 Home Assistant 控制台、夜间时钟和工作日闹钟 | 依赖 Home Assistant、Wi-Fi 和旋钮硬件；目标已有时钟/闹钟，不是当前宠物交互闭环 |
| [`marcelodias1806/yellowcard`](https://github.com/marcelodias1806/yellowcard) | 严格窗口仅文档、照片和硬件验证更新 | 没有实质功能代码增量；BLE/Wi-Fi 扫描能力此前已评估，不重复立项 |
| `jvduuren/esphome-es3c28p-light-panel` | 严格窗口只有功耗、机械和购买信息 README 更新 | 没有实质代码增量，不进入功能评估 |
| `JerrettDavis/AgentScreen` | 窗口提交仅升级 Playwright 测试依赖 | 没有产品或嵌入式功能增量，且 Agent 状态屏已有实现 |
| `arnoutzw/ESP32-Tamagotchi`、`han0519/esp32-s31-vocat`、`CyberXcyborg/ESP32-TamaPetchi` | 组合搜索发现，但仓库搜索未返回严格窗口内 `pushed_at`/默认分支实质提交 | 养成状态、语音聊天、多表情、天气主题等与已实现或已评估方向高度重叠；不以窗口外 README 能力重新立项 |

## 5. 量化排序

评分 1～5；适配和许可证越高越有利，工作量与风险高分表示更轻、更可控。硬门禁优先于总分。

| 方向 | 原创性 | 用户价值 | 差异化 | 交互闭环 | 硬件适配 | 许可证/素材 | 工作量 | 风险 | 合计/40 | 决策 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 关屏闹钟保活与行为回归 | 2 | 5 | 2 | 5 | 4 | 5 | 4 | 3 | **30** | 有可靠性价值，但目标无缺陷证据且架构不同；转为现有闹钟验收项 |
| 离线提醒与睡眠可见性 | 3 | 4 | 2 | 4 | 4 | 1 | 3 | 3 | **24** | 与现有能力重复且来源许可证未声明 |
| 设置/每日步数持久化 | 2 | 3 | 2 | 3 | 3 | 1 | 3 | 3 | **20** | 设置已持久化，步数尚无明确产品需求和准确性门禁 |
| 双表联机赛车 | 5 | 4 | 5 | 5 | 1 | 3 | 1 | 1 | **25** | 依赖不存在的资源预算、动态应用与双设备安全链路 |

## 6. 决策与后续触发条件

1. 本轮不推荐立即开发，不创建 PRD，不启动嵌入式子任务；只保存本扫描记录。
2. 在既有闹钟功能下补充硬件验收矩阵：亮屏、关屏、GUI suspend、系统 sleep、断开上位机、重复/一次性闹钟和 snooze；记录是否唤醒显示、音频是否响起、触摸能否停止以及额外唤醒功耗。若目标真机复现失败，再按缺陷流程修复。
3. 睡眠状态文案或唤醒提示只有在目标用户测试证明当前不可发现时再做增量需求；不得复制来源项目的提示文案、布局或素材。
4. 计步与长期养成必须先明确传感器精度、状态衰减、写放大、断电一致性和用户闭环；不能仅因其他手表有持久化就增加目标 Flash 写入。
5. 双设备玩法继续等待 SF32LB525 在维持桌面 BLE 连接时的 Central/Peripheral 并发、RAM、功耗、时延、断线恢复与身份安全设计。
6. 本提交不开发、不推送、不创建 PR、不合并默认分支。由主任务核验提交后，按自动化规则推送同名远端分支。
