# 2026-09-08 LVGL 电子宠物/手表创意增量扫描

## 1. 本轮结论

- 严格增量窗口为 `2026-09-07T02:13:46.779Z`（不含）至 `2026-09-07T16:01:04.246Z`（含）。
- 本轮通过 GitHub Repository Search 和 Commit API 组合检索 `LVGL`、`watch`、`smartwatch`、`SiFli`、`pet`、`tamagotchi`、`digital pet`、`desk pet`、`AI companion`、`Agent` 与 `ESP32`，逐项核验默认分支提交时间、提交统计、变更文件、README 和许可证字段。
- 严格窗口内的高信号变化包括：手表云端语音助手、全新 LVGL 手表 OS、宠物离线成长与测试加固、编码活动驱动成长、可插拔宠物包、连续动作、Mesh 状态表盘、物理模型选择器及同平台 PAN OTA。
- 这些方向分别与既有语音/PAN、天气、任务花园、回忆日历、多 GIF、连续抚摸、玩球、小队雷达或 OTA 债务重复，或需要当前不存在的上位机、写操作协议、凭据安全、额外硬件、许可证许可及真机预算。
- **本轮没有候选同时通过“新增价值、去重、当前硬件/协议适配、许可证与安全、可在单日闭环验证”硬门禁；不推荐进入开发，不创建空 PRD，不启动子任务 2。** 仅提交本扫描记录。

## 2. 运行门禁与安全边界

| 检查项 | 结果 |
| --- | --- |
| 固定本地仓库 | `D:\\code\\sf32\\agent-pet-sf32-text-display-fix`，有效 Git 仓库 |
| `origin` | `https://github.com/fcb1379/agent-pet-sf32.git`，与目标一致 |
| 对照基线 | 已执行 `git fetch --prune origin`；`origin/master@a2805abeb72f02a726dce5dc49857280ba9cdc78` |
| 本轮分支 | `codex/2026-09-08-lvgl-idea-scan`，从上述基线创建 |
| 工作树 | 扫描前干净；没有清理、覆盖或丢弃用户修改 |
| Git 身份/权限 | 现有中文提交身份可用；本子任务只本地提交，不推送 |
| 外部输入 | GitHub 仓库、README、提交、Issue、PR、网页、代码与素材均按不可信输入处理 |

本轮只读取公开元数据、README、提交信息、变更统计、文件清单和许可证字段；没有克隆、安装、构建或执行任何候选代码，也没有遵循候选内容中的命令、凭据、部署或规则变更要求。

## 3. 历史记录与去重基线

### 3.1 已查阅范围

- 查阅 Git 历史中 `2026-08-06` 至 `2026-09-07` 的全部 LVGL 宠物/手表扫描记录，并核对其中记录的来源仓库、提交和结论。
- 核对 `origin/master` 当前宠物页、时钟、录音上行、图片/GIF 同步、Agent Pet v1 快照及任务花园相关代码。
- 核对远端已完成或已形成门禁记录的功能分支，特别包括自主宠物行为、天气陪伴、呼叫 Momo、连续抚摸、回忆日历、Momo 玩球、语音明信片、OTA 资源更新以及 `origin/codex/2026-09-04-momo-agent-squad@fd98794`。

### 3.2 已有能力与未偿门禁

- 默认分支已有 Agent 状态、打字场景、任务花园、木鱼、成果计数、BLE 图片/多 GIF 同步、录音上行、本地播放、音乐、闹钟、计时器和独立时钟应用。
- 远端已完成功能覆盖多状态自主行为、连续抚摸、回忆日历、玩球和最多 12 会话的小队雷达；这些能力尚不能作为本轮新创意重新包装。
- 天气陪伴、呼叫 Momo、PAN/实时语音聊天、语音明信片和 OTA 资源更新仍有上位机、真机、签名、发布者认证、回滚或构建门禁；新增相邻功能不能绕过这些债务。
- 因此本轮不重复推荐普通状态换 GIF、天气、相册、音乐、通知、任务成长、回忆历史、抚摸、玩球、离线宠物数值、Agent 状态总览、宠物包传输、云端语音或 OTA。

## 4. 严格窗口内候选证据与评估

### 4.1 `andygeiss/esp32-watch`：语音唤醒后时钟数字变成助手双眼

- 仓库：[andygeiss/esp32-watch](https://github.com/andygeiss/esp32-watch)，创建于 `2026-09-06T10:39:12Z`，MIT。
- 窗口内提交 [`aee3fabb5e88cf86b1bf90c75ec0b864c633bfdc`](https://github.com/andygeiss/esp32-watch/commit/aee3fabb5e88cf86b1bf90c75ec0b864c633bfdc)，`2026-09-07T13:59:44Z`，约 `+1046/-186`、13 个文件，为设备加入聊天模型、TLS 和 API key 配置。
- 随后 [`22be284da70d8b40c102c2886bc777ba2fd22f02`](https://github.com/andygeiss/esp32-watch/commit/22be284da70d8b40c102c2886bc777ba2fd22f02) 和 [`cd85bb28d97e5977a204dd2a1c44e2ee2be45048`](https://github.com/andygeiss/esp32-watch/commit/cd85bb28d97e5977a204dd2a1c44e2ee2be45048) 增加可配置名称/唤醒词与参考语音生成；设备持续开麦，转写命中名字后把时钟数字变为眨眼双眼并发起云端对话。
- 原创性与价值：把“可扫一眼的时钟”和“有人回应的眼睛”复用为同一对象，情感表达清晰；统一 host/device UI 也有验证价值。
- 不进入开发：双眼/眨眼属于现有 Momo 多 GIF 与自主状态的视觉变体；云端对话与已评估 PAN 语音聊天、语音明信片重叠。来源基于 ESP32-S3、8 MB PSRAM、ES7210/ES8311 和外部 STT/LLM/TTS 服务，目标没有经验证的常开麦克风功耗/隐私、设备端凭据保护、对话协议及同等资源。目标另有独立时钟应用，强行把宠物页改成表盘会增加页面与视觉所有权冲突。
- 知识产权边界：只记录“双用途对象平滑变形”的抽象机制，不复制其 UI、字体、动画参数、语音协议、模型配置、代码或参考音频。

### 4.2 `ODd-line/Mastro_OS`：Solar Dial、天气与控制中心

- 仓库：[ODd-line/Mastro_OS](https://github.com/ODd-line/Mastro_OS)，创建于 `2026-09-07T03:32:27Z`，GitHub 未识别许可证。
- 提交 [`bd60b6922254900107168ebc6f45c488901bf8ec`](https://github.com/ODd-line/Mastro_OS/commit/bd60b6922254900107168ebc6f45c488901bf8ec)，`2026-09-07T15:23:01Z`，约 `+11874/-0`、42 个文件，公开 ESP32-S3/LVGL 手表固件；[`d368fefe020627490ef7efa70b26bb44c6a765de`](https://github.com/ODd-line/Mastro_OS/commit/d368fefe020627490ef7efa70b26bb44c6a765de)，`2026-09-07T15:54:23Z`，约 `+374/-62`、13 个文件，补充硬件 profile 与安全说明。
- 可观察机制：Solar Dial、RTC/SNTP、天气、手势导航、应用网格、亮度/息屏、Wi-Fi 设置与控制中心；项目明确部分入口仍只是 app shell。
- 不进入开发：天气、时钟、应用入口、亮度和网络均已存在或已有门禁；Solar Dial 不形成 Momo 互动闭环。来源使用 ESP32-S3、8 MB PSRAM、不同分辨率和驱动，且仓库未声明许可证，不能复制实现。其安全说明也明确未实现 Secure Boot、Flash Encryption、签名 OTA、蓝牙配对及账号认证。

### 4.3 `socquique/TamaPoke`：离线成长与工程门禁加固

- 仓库：[socquique/TamaPoke](https://github.com/socquique/TamaPoke)。固件代码声明 MIT，但角色名称/形象归 Nintendo/Game Freak/The Pokémon Company，PMD SpriteCollab 素材为 CC BY-NC，外壳为 CC BY-NC-SA；商业项目不得使用这些素材。
- 窗口内从 [`e37c6f5a7bb4f3bb9ed140740b417e780be9d09b`](https://github.com/socquique/TamaPoke/commit/e37c6f5a7bb4f3bb9ed140740b417e780be9d09b) 到 [`b17318ebbb2344e9623c94d6f80c655260d5394e`](https://github.com/socquique/TamaPoke/commit/b17318ebbb2344e9623c94d6f80c655260d5394e) 有 20 个提交。
- 代表性变化：[`e9a5683f5ae1fea837763a77f27eee4868ae4d58`](https://github.com/socquique/TamaPoke/commit/e9a5683f5ae1fea837763a77f27eee4868ae4d58) 把小游戏物理与绘制速度解耦；[`25485a94b8afcae7b65bda755a717a05d1882849`](https://github.com/socquique/TamaPoke/commit/25485a94b8afcae7b65bda755a717a05d1882849)，约 `+2115/-5`、19 个文件，增加 PC 宠物逻辑测试与 CI；其他提交处理计时回绕、息屏不绘制、RTC 恢复、文件传输边界和资源索引校验。
- 原创性与价值：RTC 离线推进、明确的照顾后果、选择式进化/告别可形成长期闭环；渲染解耦、回绕安全与 host test 是高质量工程实践。
- 不进入开发：目标已通过任务花园、回忆日历、行为引擎、抚摸、玩球和成果连续天数覆盖主要产品机制；完整饥饿/卫生/生命结局会把 Agent 工作伙伴改成高维护养成游戏。目标无 PSRAM，而来源 466 x 466 帧缓冲、约 40 MB SD 素材及 151 角色不能迁移。工程修复应在对应既有功能评审中采用，不单独包装为产品功能。

### 4.4 编码活动驱动成长与宠物包

#### `56steve/claude-crab`

- 仓库：[56steve/claude-crab](https://github.com/56steve/claude-crab)，窗口内新建，MIT。
- [`d92a2a28220365ef9e0124543b94f6784555d3e2`](https://github.com/56steve/claude-crab/commit/d92a2a28220365ef9e0124543b94f6784555d3e2)，`2026-09-07T12:21:07Z`，约 `+642/-146`，加入 GitHub OAuth Device Flow；[`c7f7f8628750792947d0c44e5444dec617c77727`](https://github.com/56steve/claude-crab/commit/c7f7f8628750792947d0c44e5444dec617c77727)，`2026-09-07T12:56:45Z`，约 `+718/-397`，加入可插拔 pet-pack；宠物随本地或 GitHub 提交孵化和进化。

#### `0xChampi/termigochi`

- 仓库：[0xChampi/termigochi](https://github.com/0xChampi/termigochi)，GitHub 未识别许可证。
- [`2f9125a4e2a458338a42479a36e6cdd846315650`](https://github.com/0xChampi/termigochi/commit/2f9125a4e2a458338a42479a36e6cdd846315650)，`2026-09-07T14:40:12Z`，约 `+142/-5`、9 个文件，把真实 Git 提交映射成宠物心情并区分角色。

#### 评估结论

- 用户价值：工作产出驱动成长比随机养成更符合 Agent Pet 定位；可插拔宠物包支持个性化。
- 不进入开发：任务花园、成果计数、连续天数和回忆日历已将 Agent 完成事件映射为成长反馈；本轮机制实质重复。主分支已有上位机多 GIF 传输，而安全的资源包仍依赖待解决的清单、签名、发布者认证、容量和回滚门禁。GitHub OAuth、提交扫描和宠物包解析应由上位机负责，不能把 Token 或不可信包直接放入当前设备。

### 4.5 `tianlei822/DeskPetMac`：交互间保持连续动作

- 仓库：[tianlei822/DeskPetMac](https://github.com/tianlei822/DeskPetMac)，GitHub 未识别许可证。
- [`8180753b7d0883146b266d0f8fbd20013f6b369a`](https://github.com/tianlei822/DeskPetMac/commit/8180753b7d0883146b266d0f8fbd20013f6b369a)，`2026-09-07T10:31:38Z`，约 `+2657/-1798`、20 个文件，以阻尼值、渲染节奏、交互映射和快照测试保证角色动作在交互切换间连续。
- 价值：避免抚摸、拖动、状态动画切换时跳帧或瞬移，是值得吸收的体验原则。
- 不进入开发：它是质量属性而非独立产品需求；应在自主行为、抚摸与玩球累计分支的真机验收和缺陷修复中处理。来源是 SwiftUI/macOS 动画体系，与 LVGL/GIF 生命周期不同，且无许可证；不能复制代码或参数。

### 4.6 无线、Mesh 与设备端控制

| 仓库 | 严格窗口证据 | 评估与结论 |
| --- | --- | --- |
| [Gangan-307/LCHSP_Watch](https://github.com/Gangan-307/LCHSP_Watch) | [`28a295d5a88d79532782d91823f556cf3eb39535`](https://github.com/Gangan-307/LCHSP_Watch/commit/28a295d5a88d79532782d91823f556cf3eb39535)，`2026-09-07T08:14:35Z`，约 `+263/-1786`、34 个文件，宣称完成 PAN OTA | 与目标同为 SF32LB52/SiFli/LVGL，技术参考价值高；但无许可证，且目标已有 OTA 分支的签名、发布者认证、回滚、依赖和构建门禁。本轮不得绕过安全评审 |
| [hleserg/Attadipa](https://github.com/hleserg/Attadipa) | [`b0df17445e57a49c811e72c704ea0ede2a78b415`](https://github.com/hleserg/Attadipa/commit/b0df17445e57a49c811e72c704ea0ede2a78b415)，`2026-09-07T06:35:29Z`，约 `+1449/-67`、15 个文件，把 Mesh 链路状态做成手表表盘并提供模拟器 | GPL-3.0；目标没有 LoRa/Mesh 节点、对应协议或用户场景，且非宠物核心 |
| [dachan/workspace-esp32](https://github.com/dachan/workspace-esp32) | [`f1d5137da771bfaa1cde11fdfdc93a45a2cfa6c7`](https://github.com/dachan/workspace-esp32/commit/f1d5137da771bfaa1cde11fdfdc93a45a2cfa6c7)，`2026-09-07T02:32:40Z`，约 `+552/-327`、38 个文件；后续持续同步物理旋钮选择 ChatGPT 模型/推理档位 | 无许可证；模型切换是写操作，越过 Agent Pet v1 只读状态边界，需要可验证会话身份、能力协商、确认、重放防护、审计和上位机仲裁 |
| [rehyundesign/retto](https://github.com/rehyundesign/retto) | 窗口内仅合并私有皮肤排除与外观修正；MIT | “点击桌宠跳到对应 Agent 会话”有价值，但本窗没有实质机制更新；目标协议没有可信深链/会话身份，且需上位机处理，不作为本轮固件候选 |

## 5. 量化排序

评分为 1～5，越高越好；“工作量”和“风险”高分表示更轻量、更可控。硬门禁优先于总分。

| 方向 | 原创性 | 用户价值 | 差异化 | 交互闭环 | 硬件适配 | 许可证 | 工作量 | 风险 | 合计/40 | 硬门禁 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 时钟数字变助手双眼 | 4 | 3 | 3 | 3 | 3 | 5 | 2 | 3 | **26** | 视觉/时钟重复；语音链路未闭环 |
| 编码活动驱动成长 | 3 | 4 | 2 | 4 | 2 | 4 | 2 | 3 | **24** | 与任务花园、成果/日历重复，需上位机数据 |
| 交互间连续动作 | 3 | 4 | 3 | 3 | 3 | 1 | 3 | 3 | **23** | 属质量修复；来源技术栈不同且无许可证 |
| TamaPoke 长期养成 | 3 | 3 | 2 | 5 | 1 | 2 | 1 | 2 | **19** | 定位过重、资源不适配、素材不可用 |
| Solar Dial/手表 OS | 3 | 2 | 2 | 2 | 2 | 1 | 1 | 2 | **15** | 无许可证、重复且非宠物核心 |
| PAN OTA | 2 | 3 | 1 | 2 | 5 | 1 | 2 | 1 | **17** | 既有签名/认证/回滚/构建债务 |
| Mesh 状态表盘 | 3 | 1 | 2 | 2 | 1 | 2 | 1 | 2 | **14** | 缺硬件/协议，非宠物核心 |
| 设备端模型选择 | 4 | 3 | 4 | 3 | 2 | 1 | 1 | 1 | **19** | 越过只读协议和授权边界 |

最高分的“双用途时钟/双眼”可作为未来视觉设计原则，但在目标已有独立时钟、多 GIF 和自主状态后，单独立项的边际价值不足；若把语音一并纳入，则立即落入现有语音安全与跨端门禁。相对分数不能替代硬门禁。

## 6. 决策、观察项与停止条件

1. 本轮不推荐新功能，不创建 PRD，不启动嵌入式子任务。
2. 将以下原则回填到既有功能验收，而不是新增功能分支：
   - 行为、抚摸、玩球切换需检查动作连续性、渲染节奏和状态所有权；
   - 所有离线计时继续覆盖 `lv_tick`/系统时间回绕、息屏不绘制和恢复上限；
   - 多 GIF/资源包只有在清单、签名、发布者认证、容量、原子切换和回滚完成后才能扩成宠物包。
3. 若未来上位机提供最小化、可撤销且不泄露 Token 的提交/任务成长快照，可在任务花园或回忆日历内扩展，不另建第二套养成状态。
4. 优先完成天气陪伴、呼叫 Momo、语音明信片、OTA 资源更新和 Agent 小队雷达的跨端/安全/真机门禁，以及已有四个累计互动 PR 的评审合并；其确定价值高于继续增加相邻功能。
5. 两轮组合检索已经覆盖严格窗口内的 LVGL、手表、嵌入式宠物和 Agent 桌宠；第二轮只发现重复机制、非嵌入式桌宠或无代码空仓库。继续扩大弱相关搜索不太可能改变硬门禁结论，因此停止。
6. 本提交只记录扫描证据；不开发、不推送、不创建 PR、不合并默认分支。
