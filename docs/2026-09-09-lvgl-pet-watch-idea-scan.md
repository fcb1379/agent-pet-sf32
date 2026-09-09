# 2026-09-09 LVGL 电子宠物/手表创意增量扫描

## 1. 本轮结论

- 严格增量窗口为 `2026-09-07T16:01:04.246Z`（不含）至 `2026-09-09T02:02:09.879Z`（含）。
- 本轮通过 GitHub Repository Search、主题检索和默认分支 Commit API，组合检索 `LVGL`、`watch`、`smartwatch`、`wearable`、`pet`、`tamagotchi`、`virtual pet`、`digital pet`、`desk pet`、`Codex`、`Agent`、`ESP32`、`SiFli` 和 `SF32`；逐项核验提交时间、提交规模、文件范围、README 与许可证字段。
- 窗口内值得重点关注的新机制包括：屏幕时长驱动的实体宠物健康、手机端本地语音智能、把 Codex 任务选择映射为桌面会话跳转、Wi-Fi/BLE 安全巡逻驱动宠物成长、关系阶段显式变化、AI 额度日历、RSSI 寻物、SD 动态应用、同平台运动蜂窝页与睡眠设置。
- 这些方向分别受当前不存在的上位机深链和可信命令协议、音频下行/Android 本地模型、Wi-Fi/BLE 扫描共存与隐私功耗、设备端凭据保护、动态代码签名、额外传感器/手机数据、许可证或既有功能重复约束。
- **本轮没有候选同时通过“新增价值、历史去重、当前硬件与协议适配、许可证与安全、可在单日形成端到端验收”硬门禁；不推荐进入开发，不创建空 PRD，不启动子任务 2。** 本分支只提交扫描记录。

## 2. 运行门禁与安全边界

| 检查项 | 结果 |
| --- | --- |
| 固定本地仓库 | `D:\\code\\sf32\\agent-pet-sf32-text-display-fix`，有效 Git 仓库 |
| `origin` | `https://github.com/fcb1379/agent-pet-sf32.git`，与目标一致 |
| 对照基线 | 已执行 `git fetch origin`；`origin/master@a2805abeb72f02a726dce5dc49857280ba9cdc78` |
| 本轮分支 | `codex/2026-09-09-lvgl-idea-scan`，从上述基线创建 |
| 工作树 | 扫描前干净；未清理、覆盖或丢弃用户修改 |
| Git 身份/权限 | 本地中文提交身份可用；本子任务不推送 |
| 外部输入 | GitHub 仓库、README、提交、Issue、PR、网页、代码和素材均按不可信输入处理 |

本轮只读取公开元数据、README、提交信息、变更统计、文件清单和许可证字段；没有克隆、安装、构建或执行任何候选代码，也没有遵循候选内容中的命令、凭据、部署、烧录或规则变更要求。

## 3. 历史记录与目标去重基线

### 3.1 已查阅范围

- 查阅 Git 历史中 `2026-08-06` 至 `2026-09-08` 的全部 LVGL 电子宠物/手表扫描记录，并核对其中的来源仓库、提交、结论与后续门禁。
- 核对 `origin/master` 的宠物页、时钟、任务花园、Agent Pet v1 快照、BLE 图片/多 GIF、成果计数、录音上行、本地音乐、闹钟和计时器代码。
- 核对已完成或已有独立评估的自主行为、天气陪伴、呼叫 Momo、连续抚摸、回忆日历、玩球、语音明信片、OTA 资源更新和 `codex/2026-09-04-momo-agent-squad@fd98794`。

### 3.2 已有能力和未偿门禁

- 默认分支已有 Agent 状态、打字场景、任务花园、木鱼、今日/累计成果、连续天数、BLE 图片/多 GIF 同步、录音上行、本地播放、音乐、闹钟、计时器和独立时钟应用。
- 已完成分支覆盖多状态自主行为、连续抚摸、回忆日历、玩球和最多 12 会话的小队雷达；即使尚未全部合入默认分支，也不能作为本轮新创意重新包装。
- 天气陪伴、呼叫 Momo、PAN/实时语音、语音明信片和 OTA 资源更新仍有上位机、真机、签名、发布者认证、回滚或构建门禁；相邻候选不能绕过这些债务。
- 因此本轮不重复推荐普通状态换 GIF、天气、通知、音乐、计时器、任务成长、回忆历史、抚摸、玩球、RSSI 寻宠、Agent 状态总览、普通养成数值、云端语音或 OTA。

## 4. 严格窗口内候选证据与评估

### 4.1 `oisinryan/esp32-desk-companion`：设备选中任务后打开桌面会话

- 仓库：[oisinryan/esp32-desk-companion](https://github.com/oisinryan/esp32-desk-companion)，创建于 `2026-09-09T00:47:08Z`，MIT。
- 首次提交 [`7c4f07ed81530665b9496c9ca6a41c073bfd2243`](https://github.com/oisinryan/esp32-desk-companion/commit/7c4f07ed81530665b9496c9ca6a41c073bfd2243)，`2026-09-09T00:47:07Z`，约 `+2577/-0`、57 个文件。
- 可观察机制：本地桥接进程读取桌面任务索引；设备轮播最近活动任务，按键选中后由桥接端打开对应桌面会话；API key 留在电脑，设备只接收受限文案和表情。
- 原创性与价值：这是本轮对 Momo 小队雷达最有价值的增量——从“看见哪条会话需要处理”延伸到“回到对应会话”，可明显缩短用户在多任务间寻找窗口的时间。
- 不进入开发：当前 Agent Pet v1 是只读快照，`task_hash`、Provider 和 source 不是可信会话身份，也没有“打开会话”命令、请求确认、重放防护、结果回执或上位机处理入口。固件单边增加点击只能产生无法消费的请求，或误把短哈希当授权身份。该机制必须先由上位机定义不可伪造的临时会话句柄、能力协商、显式用户确认、幂等与审计，再做协议版本升级和跨端测试，无法在本次固件单日闭环。
- 知识产权边界：只记录“匿名状态总览到桌面深链”的抽象产品价值；不复制其桥接脚本、协议、任务索引解析、UI、素材、安装器或配置。

### 4.2 `brenpoly/polymo`：屏幕时长影响宠物生命，手机承担本地语音智能

- 仓库：[brenpoly/polymo](https://github.com/brenpoly/polymo)，创建于 `2026-09-08T00:11:26Z`；固件和源码为 Apache-2.0，发布 APK 因 `espeak-ng` 组合要求 GPL-3.0。
- 首次提交 [`f3f3f2b45af61c17e138aeb3d13053f62175c0a9`](https://github.com/brenpoly/polymo/commit/f3f3f2b45af61c17e138aeb3d13053f62175c0a9)，`2026-09-08T00:09:58Z`，约 `+53720/-0`、300 个文件；窗口内后续提交补充依赖、签名说明、演示和外壳文件。
- 可观察机制：宠物的健康与手机刷屏行为关联；生命状态保存在设备端。设备通过 BLE 把 Opus 语音送往 Android，手机本地运行 Whisper、llama.cpp 和 Piper，再把语音回应下发，断开手机后宠物仍继续生活但不再说话。
- 原创性与价值：“人的数字习惯成为宠物环境”能形成明确的陪伴与自律闭环；把计算和凭据留在手机、设备保留生命状态，也比把云端密钥放进固件更符合能力分层。
- 不进入开发：当前目标只有录音上行，没有语音下行、Android 本地模型、屏幕时长权限、数据最小化协议和明确撤销流程；目标也没有已验证的 Opus 编解码、扬声器与 BLE 同时流式预算。引入死亡/不可逆后果还会把轻陪伴产品变成高压力养成，需要独立伦理与用户研究。它与既有实时语音、语音明信片和长期成长方向重叠，不能在本轮绕过跨端门禁。
- 许可证与素材：不得复制其生成脸谱、角色声音、阈值、生命周期公式或 Android 依赖；未来若独立实现也必须分开核对固件、应用二进制和模型许可证。

### 4.3 `h4d35x0/hexhound`：安全巡逻驱动宠物成长

- 仓库：[h4d35x0/hexhound](https://github.com/h4d35x0/hexhound)，创建于 `2026-09-08T21:44:04Z`，MIT。
- 首次提交 [`846039a768792d8587a7b3e80c5115e1dfb0f58e`](https://github.com/h4d35x0/hexhound/commit/846039a768792d8587a7b3e80c5115e1dfb0f58e)，`2026-09-08T21:32:18Z`，约 `+266951/-0`，包含多板固件、模拟器、测试、图片与生成物；窗口内后续提交主要修正文档和阶段表一致性。
- 可观察机制：用户显式发起被动 Wi-Fi/BLE 巡逻，发现开放/重复 SSID 和疑似跟踪器后获得材料、经验、外观与进化；仓库强调只用于获授权环境，BLE 跟踪判断只是启发式结果。
- 原创性与价值：把一次安全检查变成“带 Momo 巡逻”的任务，结果、奖励和成长形成完整闭环，差异化高于普通网络工具。
- 不进入开发：目标 SF32 当前没有经过验收的 Wi-Fi 主动扫描，BLE 作为 Agent/手机链路时再做持续扫描涉及角色切换、共存、功耗和断连；疑似跟踪器判断还涉及误报、位置隐私与安全声明。现有任务花园和成长反馈可承接安全任务结果，但真实检测应先由受控上位机或专用硬件提供签名快照。直接复制整个巡逻栈会引入巨量代码、素材、持久化、OTA 和硬件差异，远超单日闭环。
- 版权边界：不复制其角色、进化名、精灵图、任务、启发式阈值、材料表、代码或 HID 行为。

### 4.4 Codex/Claude 桌面伴侣集中更新

#### `rcostache230/code-familiar`

- 仓库：[rcostache230/code-familiar](https://github.com/rcostache230/code-familiar)，窗口内首次公开提交 [`bd32e2a62ab458adbfe0ecac96468dd78ac88a7d`](https://github.com/rcostache230/code-familiar/commit/bd32e2a62ab458adbfe0ecac96468dd78ac88a7d)，`2026-09-08T06:51:01Z`，约 `+20739/-0`、242 个文件；仓库元数据未识别顶层许可证。
- 它展示 Codex/Claude 的细分执行阶段、额度重置时间、事件收件箱、触摸批准/拒绝和本地桥接；部分第三方库为 GPL-3.0，来源与资产许可需逐项处理。
- 不进入开发：小队雷达已覆盖只读状态总览；细分阶段和额度字段不在 Agent Pet v1；设备端批准会跨越现有只读安全边界，并需要可信会话身份、请求绑定、过期、回执与撤销。不得从描述推断为可直接兼容。

#### `yhyh0000/dsh-esp32-dial`

- 仓库：[yhyh0000/dsh-esp32-dial](https://github.com/yhyh0000/dsh-esp32-dial)，窗口内首次提交 [`b0445776e5c4b7aaa8322bb3962b6e9e05bd6041`](https://github.com/yhyh0000/dsh-esp32-dial/commit/b0445776e5c4b7aaa8322bb3962b6e9e05bd6041)，`2026-09-08T15:15:02Z`，约 `+173796/-0`、300 个文件；未识别许可证。
- 它把 Codex 控制台、插件、桌面宠物、桥接和在线烧录集成在 ESP32-S3/LVGL9 桌面基座。
- 不进入开发：核心机制与目标 Agent 状态、Momo 多 GIF 和小队雷达重复；插件壳、在线烧录和上位机桥接均不能由本次 SF32 固件独立验收，且无许可证不能复制。

### 4.5 `HontoUKI/M.A.R.I.A.-Micro-Engine`：关系变化与会话轮次所有权

- 仓库：[HontoUKI/M.A.R.I.A.-Micro-Engine](https://github.com/HontoUKI/M.A.R.I.A.-Micro-Engine)，Apache-2.0；样例角色内容另行授权。
- 窗口内提交 [`b4a8992ad14e5b21b832f01ace045343aa3bb89b`](https://github.com/HontoUKI/M.A.R.I.A.-Micro-Engine/commit/b4a8992ad14e5b21b832f01ace045343aa3bb89b)，`2026-09-08T14:30:59Z`，约 `+74/-11`，将游戏动作放入独立 `<play>` 块；后续 [`a5dc6b056cd5ebc6627056bcc49e01fe0ba0a6f2`](https://github.com/HontoUKI/M.A.R.I.A.-Micro-Engine/commit/a5dc6b056cd5ebc6627056bcc49e01fe0ba0a6f2) 处理沉默后的轮次所有权。
- 可观察机制：关系以 affection、trust、bond 三个明确轴和阶段变化，回答、旁白和玩耍动作采用结构化标签；角色包与引擎分离。
- 价值：长期关系应可解释，语音/触摸/玩球之间的轮次所有权也应避免宠物在用户或音频尚未结束时抢动作。
- 不进入开发：这是服务端聊天引擎机制，不是 LVGL/固件闭环；目标行为引擎、任务花园、回忆日历和本地互动已经有多套状态来源，再增加三轴会造成重复且需持久化迁移。结构化 `play` 还需上位机协议、内容安全、动作白名单和抢占仲裁。只把“关系变化可解释”和“轮次所有”记为未来语音验收原则，不立新功能。

### 4.6 额度、RSSI 寻物、动态应用和手表通用更新

| 仓库/方向 | 严格窗口证据 | 评估结论 |
| --- | --- | --- |
| [hellonick/ai-passport-token-journey](https://github.com/hellonick/ai-passport-token-journey) | [`729633a9c1b461f31dd4056e2f177073d88cb92f`](https://github.com/hellonick/ai-passport-token-journey/commit/729633a9c1b461f31dd4056e2f177073d88cb92f)，`2026-09-08T16:01:51Z`，约 `+13898/-0`、189 文件；MIT | 30 天用量热力图有信息价值，但把 Wi-Fi 与供应商 API key 存在设备 NVS 与目标安全边界相反；目标协议也没有准确额度/费用字段。与 9 月 7 日“配额与重置提示”观察项重复 |
| [khlebobul/esp_ble_finder](https://github.com/khlebobul/esp_ble_finder) | [`697b43a329af8a5e934accf00f91a4192726f538`](https://github.com/khlebobul/esp_ble_finder/commit/697b43a329af8a5e934accf00f91a4192726f538)，`2026-09-07T19:06:19Z`，约 `+959/-0`、14 文件；无许可证 | RSSI 点阵和泊车音寻找手机与“呼叫 Momo/找手表”和现有 BLE 连接重复；按名称扫描易误认，持续扫描影响连接与功耗，且无许可证 |
| [charliejgallo/ESP32S3_AmoledOS](https://github.com/charliejgallo/ESP32S3_AmoledOS) | [`1c85956c048c4a98a4de95e97b5b3ec05d27f4fd`](https://github.com/charliejgallo/ESP32S3_AmoledOS/commit/1c85956c048c4a98a4de95e97b5b3ec05d27f4fd)，`2026-09-09T00:21:41Z`，约 `+179979/-0`；MIT | 从 microSD 加载 `.so` 的动态应用对 SF32 是任意代码执行面；在缺少签名、ABI、能力沙箱、回滚和资源预算时禁止落地。内置天气、游戏、通知、音乐等又与现有能力重复 |
| [Gangan-307/LCHSP_Watch](https://github.com/Gangan-307/LCHSP_Watch) | [`23225cbc73af6fcc9bc575572d029171bc2091de`](https://github.com/Gangan-307/LCHSP_Watch/commit/23225cbc73af6fcc9bc575572d029171bc2091de)，`2026-09-08T01:20:44Z`，约 `+329/-3`，同平台运动蜂窝页；无许可证 | SF32LB52 适配参考价值高，但步数/卡路里/距离是通用手表能力，不形成 Momo 互动；目标需先确认传感器 BOM、计步精度、功耗与健康表述，且无许可证不能复制 |
| [TurtleWhal/GWatch-Paris](https://github.com/TurtleWhal/GWatch-Paris) | [`b8f8ec60abd20d2c715b25814f59cd3747ad6c67`](https://github.com/TurtleWhal/GWatch-Paris/commit/b8f8ec60abd20d2c715b25814f59cd3747ad6c67)，`2026-09-07T19:30:35Z`，约 `+993/-139`，睡眠时间设置与表盘信息；无许可证 | 依赖 ESP32-S3/Gadgetbridge 和手机配置；目标已有闹钟/时间能力，且没有睡眠检测闭环。仅设置睡眠区间不能证明睡眠，不落地 |
| [andygeiss/esp32-watch](https://github.com/andygeiss/esp32-watch) | [`6d36bee4e3f7c101e60cb951df24fd6317aedadf`](https://github.com/andygeiss/esp32-watch/commit/6d36bee4e3f7c101e60cb951df24fd6317aedadf) 和 [`3fc9dd11f59576d737fe8d418a6bd8cb96cf2907`](https://github.com/andygeiss/esp32-watch/commit/3fc9dd11f59576d737fe8d418a6bd8cb96cf2907)，`2026-09-08`，增加时间工具并缩短语音回复；MIT | 属 9 月 8 日已评估的云端语音助手连续优化；时间问答可直接由既有时钟展示替代，语音链路门禁未改变 |
| [hleserg/Attadipa](https://github.com/hleserg/Attadipa) | [`bf550b52f8cff1f200b45daa6b6e4631349e7c3d`](https://github.com/hleserg/Attadipa/commit/bf550b52f8cff1f200b45daa6b6e4631349e7c3d) 应用清单，及 [`9f5539e59122bb4439bded3ac8043f1feee7de4f`](https://github.com/hleserg/Attadipa/commit/9f5539e59122bb4439bded3ac8043f1feee7de4f) 配网同意入口；GPL-3.0 | 配网同意和能力清单是良好平台原则，但目标尚无对应 GNSS/Mesh/Wi-Fi 产品场景；通用框架不构成宠物功能，技术栈和许可证也不适合直接移植 |
| [ZSWatch/ZSWatch](https://github.com/ZSWatch/ZSWatch) | [`2d93427ba620241d84b87ea8d68bc5fccca5f4d6`](https://github.com/ZSWatch/ZSWatch/commit/2d93427ba620241d84b87ea8d68bc5fccca5f4d6)，仅修改 CI 以检出 PR 代码；GPL-3.0 | 没有用户功能变化 |
| [ciccirix/13-37-threat-radar](https://github.com/ciccirix/13-37-threat-radar) | [`4e65446638d93b45e5cbff6d30924f6b85c8a7a3`](https://github.com/ciccirix/13-37-threat-radar/commit/4e65446638d93b45e5cbff6d30924f6b85c8a7a3)，仅刷新贡献者图；MIT | 没有实质产品更新；其反跟踪方向也与 HexHound 同受无线、误报和隐私门禁约束 |
| [Haraldon9847/waveshare-watch-rs](https://github.com/Haraldon9847/waveshare-watch-rs) | [`6238694a20ddfa4ce793bb660dfd23f5cca1ae63`](https://github.com/Haraldon9847/waveshare-watch-rs/commit/6238694a20ddfa4ce793bb660dfd23f5cca1ae63)，仅更新 README；未识别许可证 | 无功能增量；Rust/no_std 且明确替代 LVGL，技术栈不兼容 |
| [chayuto/ESP32-C6-Touch-AMOLED-1.8](https://github.com/chayuto/ESP32-C6-Touch-AMOLED-1.8) | [`661afd423cdc31bc41d1514eaf2fd833a3867fb7`](https://github.com/chayuto/ESP32-C6-Touch-AMOLED-1.8/commit/661afd423cdc31bc41d1514eaf2fd833a3867fb7)，只修复 Govee Monitor 数据库 RLS 角色；MIT | 与 PixelPet/Tamagotchi 无关，不把安全修复冒充宠物功能更新 |

## 5. 量化排序

评分为 1～5，越高越好；“工作量”和“风险”高分表示更轻量、更可控。硬门禁优先于总分。

| 方向 | 原创性 | 用户价值 | 差异化 | 交互闭环 | 硬件适配 | 许可证 | 工作量 | 风险 | 合计/40 | 硬门禁 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 小队雷达到桌面会话深链 | 4 | 5 | 4 | 4 | 3 | 5 | 2 | 2 | **29** | 缺可信会话身份、上位机和双向协议 |
| 屏幕时长驱动实体宠物 | 4 | 4 | 4 | 5 | 2 | 4 | 1 | 2 | **26** | Android 权限/模型/音频下行缺失，产品压力风险 |
| 关系阶段与轮次所有权 | 3 | 4 | 3 | 4 | 2 | 5 | 2 | 3 | **26** | 服务端机制；与现有成长/行为重复，缺协议 |
| 安全巡逻驱动成长 | 4 | 3 | 5 | 5 | 1 | 5 | 1 | 1 | **25** | 缺 Wi-Fi/扫描共存，隐私、功耗和误报风险 |
| 30 天 AI 用量旅程 | 3 | 3 | 3 | 3 | 1 | 5 | 1 | 1 | **20** | 与配额观察重复；不能把供应商密钥放设备 |
| RSSI 寻找手机 | 2 | 3 | 2 | 4 | 2 | 1 | 3 | 2 | **19** | 呼叫 Momo 重复；无许可证、身份与功耗问题 |
| SD 动态应用 | 4 | 2 | 3 | 2 | 1 | 5 | 1 | 1 | **19** | 任意代码执行、签名/ABI/沙箱/回滚缺失 |
| 同平台运动蜂窝页 | 2 | 2 | 1 | 2 | 4 | 1 | 2 | 2 | **16** | 非宠物核心，BOM/精度/健康门禁 |

得分最高的“会话深链”确实是小队雷达自然的下一步，但它的核心工作在上位机身份映射与安全协议，不是一个可由固件单边验证的按钮。相对评分不能替代端到端硬门禁。

## 6. 决策与后续观察

1. 本轮不推荐新功能，不创建 PRD，不启动嵌入式子任务。
2. 保留三个未来观察条件，但不承诺落地：
   - 若上位机能提供短期、不可伪造、可撤销的会话句柄，并定义能力协商、确认、幂等、回执和审计，可在小队雷达内评估“回到电脑打开该会话”；不能使用显示哈希充当身份。
   - 若 Android 端完成隐私评审并能以最小化、可关闭的方式提供屏幕时长摘要，可研究低压力的“专注陪伴”，不得引入死亡惩罚，也不得把原始应用使用记录下发设备。
   - 若未来具备受控 Wi-Fi/BLE 扫描能力和可解释的安全检测数据源，可让任务花园消费一次“巡逻完成”结果；扫描、判断和敏感发现不应由当前宠物 UI 自行推断。
3. 将以下机制作为既有功能验收原则：上位机/手机承担高算力和凭据；设备保持有界状态；关系变化必须可解释；所有语音与动作必须有轮次所有权；任何配网、深链或写操作都需显式同意和可撤销。
4. 优先完成天气陪伴、呼叫 Momo、语音明信片、OTA 资源更新和现有交互 PR 的跨端、安全及真机门禁，并验收 Momo Agent 小队雷达；它们的确定价值高于继续增加相邻功能。
5. 多组严格窗口检索已经覆盖 LVGL 主题、手表、电子宠物、Agent/Codex 桌宠和同平台项目；补充检索只得到重复能力、通用设备、非功能提交或无法适配候选，继续扩大弱相关搜索不太可能改变结论，因此停止。
6. 本提交只记录扫描证据；不开发、不推送、不创建 PR、不合并默认分支。
