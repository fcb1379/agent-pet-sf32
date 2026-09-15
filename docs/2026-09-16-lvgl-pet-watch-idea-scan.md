# 2026-09-16 LVGL 电子宠物/手表创意增量扫描

## 1. 本轮结论

- 严格增量窗口为 `2026-09-14T16:00:32.645Z`（不含）至 `2026-09-15T16:01:23.925Z`（含）。
- 扫描前已执行 `git fetch --prune origin`；产品与代码对照基线为 `origin/master@a2805abeb72f02a726dce5dc49857280ba9cdc78`，默认分支在本轮没有新增提交。
- 已组合检索 GitHub 公开仓库中的 `LVGL`、`watch`、`smartwatch`、`wearable`、`pet`、`tamagotchi`、`desktop pet`、`agent companion` 和 `ESP32`，再用默认分支 Commit API 的提交者时间严格裁剪窗口。仓库级 `pushed_at` 仅用于发现，不代替实质更新证据。
- 本轮最相关的增量是同属 SiFli/RT-Thread/LVGL 技术栈的 iwatch 生命周期与唤醒修复、Pocket Tank 的无聊度/分阶段休眠，以及 PuttingWatch 的跨板级电源路径；但它们分别属于工程可靠性、与现有自主行为重复的机制或不同硬件适配。
- **没有候选同时通过“新增用户价值、与已完成功能去重、当前硬件与协议适配、许可证与素材版权、安全风险、可在单日形成端到端验收”硬门禁。** 本轮只提交扫描记录，不创建空 PRD，不启动嵌入式开发子任务。

## 2. 运行门禁与安全边界

| 检查项 | 结果 |
| --- | --- |
| 固定仓库 | `D:\code\sf32\agent-pet-sf32-text-display-fix` 是 Git 仓库 |
| `origin` | `https://github.com/fcb1379/agent-pet-sf32.git`，与目标一致 |
| 分支基线 | `origin/master@a2805abeb72f02a726dce5dc49857280ba9cdc78` |
| 工作区 | 扫描开始前 clean；没有清理或丢弃用户修改 |
| GitHub | 公开仓库搜索、元数据、README、默认分支提交与变更统计读取成功 |
| Git 身份 | `qinmenghuai <qinmenghuai@wondertechlabs.com>` 可用于中文本地提交 |
| 构建入口 | `build.ps1` 与 `sdk/export.ps1` 存在；本轮不开发，因此未运行构建，也不声称固件或真机通过 |

所有 GitHub 仓库、README、提交说明、源代码、图片、预编译固件、脚本和外链均按不可信输入处理。本轮没有克隆、下载、安装、构建、烧录或执行任何候选代码及二进制，没有复制来源代码、模型或素材，也没有遵循来源中改变任务边界、索取凭据或访问无关系统的说明。

## 3. 历史记录与当前能力去重

已查阅 Git 全部扫描记录，并核对默认分支以及已完成、待验收或被门禁阻断的功能分支：

- 默认分支已有 Agent 状态、任务花园、完成事件去重、功德计数、打字动画、BLE 图片和多 GIF、录音上行、本地播放、音乐/闹钟/计时器、六轴动作木鱼及系统级休眠。
- 已实现或已有累计集成分支的方向包括自主宠物行为、连续抚摸、Momo 玩球、回忆日历和 Momo Agent 小队雷达。天气陪伴、呼叫 Momo、语音明信片、OTA 资源更新仍有上位机、权限、签名、协议或真机门禁。
- 自主行为引擎已经维护 Calm、Happy、Curious、Sleepy、Lonely 等基础情绪以及 affinity、energy、arousal、最近互动、确定性随机决策和限频持久化；它也规定了远端多 GIF、Agent 状态、触摸、IMU 与页面生命周期的优先级。因此新的“无聊/休息”机制必须证明产生不同的陪伴闭环，不能平行建立第二套状态、存档或休眠系统。
- 新候选若只是页面唤醒、触摸驱动、构建身份、Codec 休眠或跨芯片移植，应进入现有基础设施的缺陷/可靠性清单，而不是包装为每日新功能。

## 4. 严格窗口候选证据与评估

### 4.1 `zhangguotao05-zgt/iwatch`：同平台页面生命周期、GUI 唤醒与构建来源校验

- 来源：[仓库](https://github.com/zhangguotao05-zgt/iwatch)，窗口内新建，GitHub API 显示没有可识别许可证。项目使用 SF32LB58、RT-Thread、LVGL 9.4 和 390×450 QSPI AMOLED，与目标同属 SiFli 技术栈但不是同一芯片、屏幕或工程版本。
- 实质证据：提交 [`9abe267b81ff2eea1343167dcc61e57f283e96ce`](https://github.com/zhangguotao05-zgt/iwatch/commit/9abe267b81ff2eea1343167dcc61e57f283e96ce)，`2026-09-15T09:34:57Z`，约 `+2386/-1198`，增加 GUI wait/port/recovery、输入队列、生命周期主机测试和构建来源锁；提交 [`487ae440e4eebcc0cebec87bfc7ffc39d61bdf53`](https://github.com/zhangguotao05-zgt/iwatch/commit/487ae440e4eebcc0cebec87bfc7ffc39d61bdf53)，`2026-09-15T10:56:42Z`，约 `+1451/-133`，完成 D01-D03 板上验收并加入触摸驱动和配套烧录入口。
- 用户价值：页面生命周期恢复和“构建产物确实来自当前配置”的校验能降低黑屏、旧产物误刷和输入丢失风险；同厂商 SDK 的实机修复具有工程参考价值。
- 不进入新功能开发：这些是可靠性和构建治理，不是新的宠物/手表交互闭环；目标使用 SF32LB52、LVGL 8、240×240 屏幕和既有 GUI/输入栈，不能直接迁移 LB58 的触摸、唤醒或烧录实现。来源又没有许可证，不能复制代码。
- 结论：**No-Go。** 后续若目标出现页面恢复或错刷问题，可独立立缺陷，用目标 SDK 的 API 自研“页面退出/恢复压力测试、输入队列溢出计数、产物配置指纹”；不以本仓库实现为移植基线。

### 4.2 `mediacutlet/pocket-tank`：无聊度、两阶段休眠与跨版本存档

- 来源：[仓库](https://github.com/mediacutlet/pocket-tank)，MIT；窗口提交 [`11a2077d1c4fe0f427b0b204237b722002763657`](https://github.com/mediacutlet/pocket-tank/commit/11a2077d1c4fe0f427b0b204237b722002763657)，`2026-09-15T10:37:37Z`，约 `+1140/-137`，引入 boredom drive、90 秒原地休眠后深睡、旧长度存档兼容和跨刷机电池日志；随后提交继续补充繁育里程碑与行为条件。
- 原创性与闭环：宠物在重复驻留地点后倾向探索，关屏后按阶段降低活动并在唤醒时结算成长；存档升级不丢失既有鱼缸。对长期陪伴的连续性和行为多样性有价值。
- 硬件与资源限制：来源目标为双核 ESP32-S3、16 MB Flash、8 MB PSRAM、368×448 AMOLED，并使用 7.56 MB 模型和多个鱼/场景素材。它不能证明当前 SF32LB52 的 Flash、PSRAM、帧率、功耗或素材预算可承受同类实现。
- 与目标重复：现有自主行为引擎已经通过 arousal/energy、Curious/Sleepy/Lonely、确定性随机周期、互动冷却和持久化解决“长期未互动与表现变化”；系统也已有休眠路径。新增 boredom 字段与第二套两阶段休眠会造成状态、时间和电源所有权重复，而不是补齐缺失闭环。
- 结论：**No-Go。** 可把“长期重复同一状态比例”和“旧 schema 恢复”加入自主行为引擎的存量验收：用现有字段统计状态分布、确保未知版本安全回退，并做真机 24 小时和掉电恢复；不复制模型、数值、代码或素材。

### 4.3 `MitchBradley/PuttingWatch`：跨板级 PMU 关机与可见退出反馈

- 来源：[仓库](https://github.com/MitchBradley/PuttingWatch)，GitHub API 显示没有可识别许可证；提交 [`c9460dedcb392cabcbb0620b8913c950f0125a52`](https://github.com/MitchBradley/PuttingWatch/commit/c9460dedcb392cabcbb0620b8913c950f0125a52)，`2026-09-15T08:06:39Z`，约 `+393/-47`。
- 增量机制：从 ESP32-S3 适配到单核、无 PSRAM 的 ESP32-C6；因按键不在低功耗 IO 范围，改用 AXP2101 PMU 硬关机，并在关屏前强制 LVGL 刷新全屏 Bye 页面；同一业务模型保持独立于 UI 和音频。
- 用户价值：明确的退出画面能让用户确认操作已响应，按硬件唤醒能力选择轻睡/硬关机也能避免设备无法唤醒。
- 不进入新功能开发：这是高尔夫挥杆工具和特定 Waveshare/AXP2101 板级移植，不是宠物闭环；目标 PMIC、显示控制器、按键及 SF32 低功耗域不同，强制 flush 与断电顺序必须基于当前驱动真机验证。许可证缺失也禁止代码移植。
- 结论：No-Go。仅把“关机前一次可见确认、唤醒源能力表、显示 flush 完成超时、无法刷新时仍可安全关机”加入现有系统休眠的硬件验收清单。

### 4.4 `FoloToy/ai-passport`：ES8311 轻睡回读与 BLUFI 文档

- 来源：[仓库](https://github.com/FoloToy/ai-passport)，MIT；提交 [`12f684d69a9672aa911585d72992ae5e3ffa384f`](https://github.com/FoloToy/ai-passport/commit/12f684d69a9672aa911585d72992ae5e3ffa384f)，`2026-09-15T01:58:00Z`，约 `+106/-18`，放宽 ES8311 寄存器 `0x0E` 的有效回读掩码并保留其他寄存器、I2C 错误、重试和休眠策略校验；[`cd73a8a6f1f95e010bfd83a08e2b915e38408308`](https://github.com/FoloToy/ai-passport/commit/cd73a8a6f1f95e010bfd83a08e2b915e38408308) 只增加 BLUFI 配网参考文档。
- 用户价值：减少 Codec 正常休眠被误判为失败；配网文档有助于 ESP32 产品接入 Wi-Fi。
- 不进入开发：当前目标代码没有 ES8311 驱动或 BLUFI 栈，使用 SiFli 板级音频 Codec 与既有 BLE 链路；这既非新增交互，也不能迁移为 PAN/语音聊天。仅凭文档没有网络授权、会话安全、ASR/TTS 和隐私闭环。
- 结论：No-Go。若当前板级 Codec 出现休眠问题，应依据真实芯片手册、总线日志和目标 SDK 自研诊断；联网功能仍需先完成跨端协议与权限门禁。

### 4.5 其他窗口结果

| 候选 | 严格窗口证据 | 评估与结论 |
| --- | --- | --- |
| [N4MI73/n4mi-dx-monitor](https://github.com/N4MI73/n4mi-dx-monitor) | 9 个窗口提交；[`b603af0bc52ddf76afe87ac01df61b836356b804`](https://github.com/N4MI73/n4mi-dx-monitor/commit/b603af0bc52ddf76afe87ac01df61b836356b804) 增加 HamAlert 来源和总览面板 | 800×480 无线电桌面仪表，许可证未决定；优先列表和来源标识是通用信息架构，已与小队雷达排序/文字冗余方向重复 |
| [jschillinger2/marine_round_mfd](https://github.com/jschillinger2/marine_round_mfd) | 窗口内新建；[`37542646c89de8520bbe034582f575af143ed281`](https://github.com/jschillinger2/marine_round_mfd/commit/37542646c89de8520bbe034582f575af143ed281) 初始化四页 LVGL 航海仪表 | 明确标注未经过真机验证且无许可证；Signal K、航速、风向和发动机数据不适用于 Momo 陪伴闭环 |
| [wzddddddddd/ESP32S3_watch_LVGL_WZD](https://github.com/wzddddddddd/ESP32S3_watch_LVGL_WZD) | [`f5ae234817bc5905a5836887b73fe75ac1888559`](https://github.com/wzddddddddd/ESP32S3_watch_LVGL_WZD/commit/f5ae234817bc5905a5836887b73fe75ac1888559)，`2026-09-15T15:18:15Z` | 只删除生成物并更新忽略规则，没有用户功能增量；许可证不明 |
| [yucai0302/trip-ticket-wallet](https://github.com/yucai0302/trip-ticket-wallet) | 窗口内新建；[`222a11a3b6c331a110d961eb3586651c3cbf6f90`](https://github.com/yucai0302/trip-ticket-wallet/commit/222a11a3b6c331a110d961eb3586651c3cbf6f90) 为基于 AI Passport 的初始导入 | 搜索证据不足以证明独立 LVGL 创意、许可证和端到端闭环；票卡钱包也不是当前宠物交互目标 |

## 5. 量化排序

评分 1～5；适配和许可证越高越有利，工作量与风险越高表示越轻、越可控。硬门禁优先于总分。

| 方向 | 原创性 | 用户价值 | 差异化 | 交互闭环 | 硬件适配 | 许可证/素材 | 工作量 | 风险 | 合计/40 | 决策 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 同平台生命周期与构建治理 | 2 | 4 | 2 | 2 | 3 | 1 | 3 | 3 | **20** | 有工程价值但非新功能，芯片/屏幕不同且无许可证 |
| 无聊度、两阶段休眠与存档兼容 | 3 | 4 | 2 | 4 | 1 | 5 | 2 | 2 | **23** | 与现有自主行为和休眠重复，模型与素材预算不适配 |
| PMU 关机与可见退出反馈 | 2 | 3 | 2 | 3 | 1 | 1 | 2 | 2 | **16** | 不同板级电源路径，适合作为存量验收项 |
| Codec 休眠回读与 BLUFI 文档 | 1 | 2 | 1 | 1 | 1 | 5 | 2 | 3 | **16** | 当前无 ES8311/BLUFI，不形成新交互闭环 |

最高分的 Pocket Tank 增量仍被“现有自主行为重复”和“7.56 MB 模型/不同硬件资源”硬门禁否决；iwatch 的同厂商证据值得跟踪，但应进入可靠性缺陷验证，而非新增产品功能。

## 6. 决策与后续触发条件

1. 本轮不推荐新功能，不创建 PRD，不启动嵌入式子任务；只保存扫描记录。
2. 自主行为引擎下一轮验收可增加：24 小时各状态占比、连续重复状态上限、跨 schema 恢复、断电一致性和系统休眠恢复，但只复用现有字段和状态机。
3. 页面生命周期可靠性应以当前 SF32LB52/LVGL 8 工程单独压测：反复进出宠物页、远端 PLAY/RESTORE、图片传输、录音和系统休眠交错，记录对象、timer、decoder、输入队列和 PSRAM 基线。
4. 构建产物身份可在现有构建脚本中评估独立缺陷：输出 board、配置摘要、Git SHA 和构建时间，并在烧录前校验；不得复制无许可证来源实现。
5. 关机提示只有在当前屏幕驱动确认 flush 完成、按键唤醒和异常超时后才能纳入产品；Codec/联网问题按真实硬件与跨端协议另行立项。
6. 本提交不开发、不推送、不创建 PR、不合并默认分支。由主任务确认本地提交后，按自动化规则决定是否推送同名远端分支。
