# 2026-09-22 LVGL 电子宠物/手表创意增量扫描

## 1. 本轮结论

- 严格增量窗口为 `2026-09-20T16:00:29.228Z`（不含）至 `2026-09-21T16:01:21.873Z`（含）。
- 扫描前已执行 `git fetch --prune origin`；产品与代码对照基线为 `origin/master@a2805abeb72f02a726dce5dc49857280ba9cdc78`，默认分支相较上一轮没有新增提交。
- 公开 GitHub 组合检索覆盖 `LVGL`、`watch`、`smartwatch`、`wearable`、`pet`、`tamagotchi`、`ESP32`、`clock`、`badge`、`companion` 和 `desktop companion`。仓库 `pushed_at` 只用于发现，最终使用默认分支 Commit API 的提交时间、提交统计和文件清单裁剪严格窗口。
- 本轮最高价值方向是 `man612/nara-firmware` 的“IMU/触摸触发本地表情与声音反射”；另有双手表联机贪吃蛇、离线 BLE 社交卡、OTA 中断恢复提示和滑动误触防护等候选。
- **没有候选同时通过新增价值、历史功能去重、当前硬件/协议适配、许可证与素材版权、风险以及单日端到端验收门禁。** 本轮只提交扫描记录，不创建空 PRD，不启动嵌入式开发子任务。

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
- 已实现或已有远端累计集成分支的方向包括：自主宠物行为 `origin/codex/pr-pet-behavior-engine`、连续抚摸 `origin/codex/pr-pet-stroking`、Momo 玩球、回忆日历、Momo Agent 小队雷达和“呼叫 Momo”。新的传感器表情、滑动互动、离线日历或 Agent 状态方向必须证明不是重复包装。
- `app_pet.c` 已具备 IMU 采样、三阶段动作检测、冷却、木鱼动画/声音、状态动画和自定义 GIF 双缓冲；已有自主行为分支还完成了多 GIF 与本地行为状态兼容。候选只有在补齐新的、可验收的交互闭环时才应再开分支。
- 当前设备仍以桌面/上位机 BLE Central 连接设备 BLE Peripheral 为主要链路。默认配置没有 Wi-Fi、ESP-NOW 或设备到设备可靠通道；BLE 社交与双表联机不能把 ESP32 项目的协议能力视为 SF32LB525 已具备能力。

## 4. 严格窗口候选证据与评估

### 4.1 `man612/nara-firmware`：传感器驱动的本地表情与声音反射

- 来源：[仓库](https://github.com/man612/nara-firmware)。窗口提交 [`e306135517be948949efc54d9b480c0a7aed10ae`](https://github.com/man612/nara-firmware/commit/e306135517be948949efc54d9b480c0a7aed10ae)，`2026-09-21T13:43:26Z`，约 `+485/-3`，新增独立 gesture classifier、单元测试，并把 IMU、本地电量与视线输入接到表情层；提交 [`37d392ab6e1f89f8adfd6df06d659349071bf0e3`](https://github.com/man612/nara-firmware/commit/37d392ab6e1f89f8adfd6df06d659349071bf0e3)，`2026-09-21T13:56:56Z`，约 `+177/-29`，增加持久化本地声音反应。
- 产品价值：设备不需要网络或上位机即可对拿起、摇动、转向、触摸等现实动作即时改变眼神/表情并播放短反馈；输入、反应和用户再次动作形成低延迟陪伴闭环，符合 Momo 的常驻宠物定位。
- 原创性与差异化：把分类器、表情状态和可替换声音反应拆成独立模块，工程证据完整；但“动作触发表情/声音”本身与目标仓已有 IMU 木鱼、自主行为、多 GIF 状态和连续抚摸高度重叠，新增价值主要在触发器组合与配置层，不是新的核心体验。
- 许可证硬门禁：来源根 `LICENSE` 明确写明 **NARA FIRMWARE PROPRIETARY SOURCE-AVAILABLE TERMS**，对 Nara 原创代码、文档、产品行为、项目结构和设备集成不授予使用、复制、修改、合并或派生许可；只有明确标注的第三方部分继续受各自许可证约束。因此不得复制或移植候选实现、结构、参数、声音包或产品行为设计。
- 适配与风险：目标硬件具备 IMU、触摸、音频与 GIF 基础，独立重做在能力上可行；但必须先在已有自主行为分支上统一触发优先级、冷却、音频仲裁、页面生命周期和低功耗策略，并由自有素材构建反馈包。当前再开平行 PRD 会制造重复状态机和合并冲突。
- 结论：**No-Go。** 保留“本地多模态反射”作为既有自主行为需求的后续验收项；在自主行为与连续抚摸合并、许可证完全隔离并获得自有声音/表情素材后再增量评估，不新建功能分支。

### 4.2 `charliejgallo/ESP32S3_AmoledOS`：双手表联机 Neon Snakes

- 来源：[仓库](https://github.com/charliejgallo/ESP32S3_AmoledOS)，MIT。窗口提交 [`2dba26862144a013ddbb111f829abe05b721fd79`](https://github.com/charliejgallo/ESP32S3_AmoledOS/commit/2dba26862144a013ddbb111f829abe05b721fd79)，`2026-09-21T14:33:23Z`，约 `+3778/-279`；新增代码绘制的霓虹贪吃蛇、28 fps 局部刷新、单表三机器人模式，以及两表可靠通道锁步、START/HELLO 竞态修复、状态哈希和 20,000 步 harness。
- 用户价值：两只设备见面即可进入对战，具有明确的邀请、游玩、胜负和复玩闭环；代码绘制素材降低外部美术依赖。
- 当前不落地：目标仓没有来源所依赖的动态 `.so` 应用框架、双表可靠通道、配对身份、密钥、重放保护或双角色共存证据。此前双表对讲、雷达和像素画已因同一底层协议门禁判为 No-Go；新游戏不能绕过该门禁。单表版本又是大规模独立游戏，偏离“宠物陪伴”且单日难以完成真机性能和触摸验收。
- 结论：**No-Go。** 等设备到设备安全链路与两台真机共存门禁闭环后，才考虑轻量合作玩法；本轮不移植游戏实现或图片。

### 4.3 `VictorTran1023/ai-passport-streetpass`：离线 BLE 社交卡交换

- 来源：[仓库](https://github.com/VictorTran1023/ai-passport-streetpass)，MIT。窗口提交 [`ccb05ddd20d49eace0ed95f2261974fea8119ea7`](https://github.com/VictorTran1023/ai-passport-streetpass/commit/ccb05ddd20d49eace0ed95f2261974fea8119ea7)，`2026-09-21T14:41:25Z`，约 `+153819/-231`、69 个文件；实现离线卡片存储、BLE 相遇交换、本地 Wi-Fi 编辑和 LVGL 界面，并带大量生成字体与视觉素材。
- 用户价值：可抽象为“两个 Momo 相遇后交换昵称/心情/纪念章”，社交传播价值高，且无需云端账户。
- 当前不落地：目标的 BLE Peripheral 正由桌面端 Agent 链路使用；设备互扫互连需要 Central/Peripheral 并发、身份确认、隐私可见性、速率限制、去重、解绑和重放保护。来源的 Wi-Fi 编辑器也不适配当前默认配置。提交包含约 15 万行生成字体和多张 UI/概念素材，即使仓库为 MIT，也必须独立核对字体 NOTICE、图片来源和商标，不能整包复制。
- 结论：**No-Go。** 产品概念可在安全的双设备协议完成后独立设计；当前不能为社交卡另开协议支线。

### 4.4 小型候选与其他窗口结果

| 候选 | 严格窗口证据 | 评估与结论 |
| --- | --- | --- |
| [`curisama/The-Badge`](https://github.com/curisama/The-Badge) | [`a8c78bb2b4e3ec270bb2ca75430e1ee5dd349d2c`](https://github.com/curisama/The-Badge/commit/a8c78bb2b4e3ec270bb2ca75430e1ee5dd349d2c)，`2026-09-21T05:02:38Z`，`+72/-2`；通过按下点与释放点距离阈值防止滑出秒表时误启动 | 修复思路清楚，但目标仓没有相同全屏秒表按钮缺陷证据；现有 alarm/timer 使用明确控件与独立 gesture 回调。没有真实缺陷，不为通用防护新建功能分支 |
| [`hzn29/01watch`](https://github.com/hzn29/01watch) | [`de6e3d68480beeca35be53bed82bf221c90792a4`](https://github.com/hzn29/01watch/commit/de6e3d68480beeca35be53bed82bf221c90792a4)，`2026-09-21T05:27:00Z`，`+124/-12`；STM32F411/Ymodem bootloader 增加 OTA 中断恢复提示 | 属于可靠性修复而非新陪伴创意；目标 OTA 架构、分区和 bootloader 不同，且已有独立 OTA/BLE 资源更新分支，不可直接移植 |
| [`suijunqiang/lvgl9.3_watch_ui`](https://github.com/suijunqiang/lvgl9.3_watch_ui) | [`25da6379970325582f900233d7923a6e464a01db`](https://github.com/suijunqiang/lvgl9.3_watch_ui/commit/25da6379970325582f900233d7923a6e464a01db)，`2026-09-21T09:42:43Z`，`+2155/-8`；新增页面管理、瀑布流、轮播和 Android 风格示例 | 主要是通用 UI 框架/示例，目标已有应用管理与页面生命周期；没有宠物交互闭环、目标硬件测量或可证明的新用户问题 |
| `JerrettDavis/AgentScreen`、`newbie130/familybox`、`Haraldon9847/waveshare-watch-rs` | 严格窗口仅依赖升级或 README/展示更新 | 没有实质宠物/手表代码增量，不重复评估历史能力 |
| `niclasvestlund-YT/vibepulse` | [`3ce0e733155276b539c483b6851db244747c1679`](https://github.com/niclasvestlund-YT/vibepulse/commit/3ce0e733155276b539c483b6851db244747c1679)，`2026-09-20T16:40:46Z`；降低 Agent 状态 relay 上传频率 | 目标没有相同 relay；且已有 Momo Agent 小队雷达，不以云中继优化为新功能 |
| `DriftingBoats/lunar-travel-tag`、`chaitanyaasati/mapnavigation` | 窗口内分别新增像素旅行日计数应用、mDNS 地图服务切换 | 前者与回忆日历/自有 GIF 重叠并带新素材，后者依赖 Wi-Fi 与地图服务；均不进入开发 |

## 5. 量化排序

评分 1～5；适配和许可证越高越有利，工作量与风险高分表示更轻、更可控。硬门禁优先于总分。

| 方向 | 原创性 | 用户价值 | 差异化 | 交互闭环 | 硬件适配 | 许可证/素材 | 工作量 | 风险 | 合计/40 | 决策 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 本地 IMU/触摸表情与声音反射 | 3 | 5 | 3 | 5 | 4 | 1 | 3 | 2 | **26** | 价值最高，但来源 proprietary，且与现有自主行为/抚摸/IMU 链路重复 |
| 双手表 Neon Snakes | 4 | 4 | 5 | 5 | 1 | 5 | 1 | 1 | **26** | 依赖不存在的双设备安全可靠通道与动态应用框架 |
| 离线 BLE 社交卡 | 4 | 4 | 5 | 4 | 1 | 3 | 1 | 1 | **23** | 需要双角色 BLE、身份隐私与素材审计，不能单日闭环 |
| 滑动误触防护 | 2 | 3 | 2 | 4 | 4 | 5 | 5 | 4 | **29** | 分数高但目标没有同类缺陷证据，不应制造无需求改动 |

## 6. 决策与后续触发条件

1. 本轮不推荐立即开发，不创建 PRD，不启动嵌入式子任务；只保存本扫描记录。
2. “本地多模态反射”应进入现有自主行为与连续抚摸分支的合并后验收清单：动作分类必须独立实现，使用自有表情/GIF/声音；明确触发优先级、冷却、误触阈值、音频仲裁、页面退出和睡眠策略，并在真机统计误触率、响应时延和功耗。
3. 只有当默认分支已合入自主行为、多 GIF 与连续抚摸，且仍能证明缺少一个具体传感器闭环时，才为新触发器写增量 PRD；不得从 Nara 的 proprietary 实现或产品行为设计派生。
4. 双设备游戏或社交先完成 SF32LB525 在维持桌面 BLE 连接时的 Central/Peripheral 并发、RAM、功耗、时延和断线恢复测量，并完成身份、确认、密钥、重放、解绑和版本协商设计。
5. 小型交互防护只在目标代码能复现同类缺陷时修复；来源项目通过的模拟器或板端测试不能替代目标真机证据。
6. 本提交不开发、不推送、不创建 PR、不合并默认分支。由主任务核验提交后，按自动化规则推送同名远端分支。
