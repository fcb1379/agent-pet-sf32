# 2026-09-14 LVGL 电子宠物/手表创意增量扫描

## 1. 本轮结论

- 严格增量窗口为 `2026-09-12T16:00:33.305Z`（不含）至 `2026-09-13T16:01:28.171Z`（含）。
- 扫描前已执行 `git fetch --prune origin`；产品与代码对照基线为 `origin/master@a2805abeb72f02a726dce5dc49857280ba9cdc78`，默认分支在本轮没有新增提交。
- 已组合检索 GitHub 公开仓库中的 `LVGL`、`watch`、`smartwatch`、`wearable`、`pet`、`tamagotchi`、`virtual pet`、`desktop pet`、`Codex`、`agent companion` 和 `ESP32`，并通过默认分支 Commit API 以提交者时间严格裁剪窗口；仓库级 `updated_at` 只用于发现，不作为实质更新证据。
- 窗口内较有价值的机制包括：从宿主状态行取得真实配额并做新鲜度仲裁、九态安全吉祥物、过期状态 fail-closed、低资源脏矩形小游戏，以及单文件动态游戏卡带。
- **没有候选同时通过“新增用户价值、历史与现有能力去重、当前硬件/协议适配、许可证与素材版权、安全风险、可在单日形成端到端验收”硬门禁。** 本轮只提交扫描记录，不创建空 PRD，不启动嵌入式开发子任务。

## 2. 运行门禁与安全边界

| 检查项 | 结果 |
| --- | --- |
| 固定仓库 | `D:\code\sf32\agent-pet-sf32-text-display-fix` 是 Git 仓库 |
| `origin` | `https://github.com/fcb1379/agent-pet-sf32.git`，与目标一致 |
| 分支基线 | `origin/master@a2805abeb72f02a726dce5dc49857280ba9cdc78` |
| 工作区 | 扫描开始前 clean；没有清理或丢弃用户修改 |
| GitHub | 仓库搜索、公开元数据、README、默认分支提交与变更统计读取成功 |
| Git 身份 | `qinmenghuai <qinmenghuai@wondertechlabs.com>` 可用于中文本地提交 |
| 构建入口 | `build.ps1` 与 `sdk/export.ps1` 存在；本轮不开发，因此未运行构建，也不声称固件或真机通过 |

所有 GitHub 仓库、README、提交说明、代码、图片和外链均按不可信输入处理。本轮没有克隆、下载、安装、构建或执行任何候选代码，没有复制来源素材，也没有遵循来源中改变任务边界、索取凭据或访问无关系统的说明。

边界说明：`OpenSurface/SonosESP` 的默认分支提交时间为 `2026-09-13T16:03:34Z`，晚于窗口结束，不计入本轮；`cifertech/TamaFi` 的仓库页面在窗口内更新，但默认分支最后推送仍是 `2026-06-06T19:11:22Z`，不把关注、描述或索引变化误报成代码增量。

## 3. 历史记录与目标能力去重

已查阅 Git 历史中 `2026-08-06` 至 `2026-09-13` 的全部扫描记录，并核对已完成、待验收或被门禁阻断的功能分支：

- 默认分支已有 Agent 状态、等待输入/完成/异常表达、任务花园、功德计数、打字动画、BLE 图片与多 GIF、录音上行、本地播放、音乐、闹钟、计时器和系统级休眠。
- 已实现或已有独立分支的方向包括自主宠物行为、连续抚摸、Momo 玩球、回忆日历和 Momo Agent 小队雷达；天气陪伴、呼叫 Momo、语音明信片、OTA 资源更新仍有跨端、权限、签名或真机门禁。
- 多会话/额度、会话深链、工作量职业动画、社交记忆、双机互动、RSSI 寻物、设备端 Pixel Art、动态应用、普通小游戏、通用天气/音乐/通知、传统喂养/进化/退化均已评估。新候选只有解决既有门禁或形成不同闭环，才可重新立项。
- `app_pet.c` 已维护自定义 GIF 解码、PSRAM 数据、显示缓冲、LVGL 定时器、远端 `PLAY`/打字事件和页面生命周期；新视觉或触摸层必须进入统一所有权与抢占仲裁。`watch_demo.c` 已通过 `lv_disp_get_inactive_time()` 管理系统休眠，页面不能重复创建电源策略。

## 4. 严格窗口候选证据与评估

### 4.1 `niclasvestlund-YT/vibepulse`：宿主状态行配额源与新鲜度仲裁

- 来源：[仓库](https://github.com/niclasvestlund-YT/vibepulse)，MIT；窗口核心提交 [`bbc61b300efde7483a240c3b0b1aa56db1a7e0b1`](https://github.com/niclasvestlund-YT/vibepulse/commit/bbc61b300efde7483a240c3b0b1aa56db1a7e0b1)，`2026-09-12T21:13:45Z`，约 `+3155/-8`、19 个文件。代码与素材均不复制。
- 原创性与闭环：它从 Claude Code 的 `statusLine` 输入获取会话/周窗口，把原有状态行串联回去，并按每个窗口的新鲜度与 OAuth 探针/缓存仲裁；安装、状态、卸载、诊断和 smoke test 形成较完整的上位机闭环。后续窗口提交补了缓存 session floor、日志证据和确定性测试。
- 用户价值：比设备自行猜测额度可靠，可在长任务开始前提示资源风险；“从宿主取得权威快照、按窗口过期”也适用于 Agent 状态设备。
- 不进入开发：目标 Momo Agent 小队雷达已经覆盖多会话状态，额度方向也在前几轮评估；当前 BLE 协议没有配额字段、来源标识、重置时间、状态新鲜度或多账号绑定。该实现当前明确只有 macOS 单账号安装切片，真实值还依赖宿主产品的非固件接口。设备端单独增加额度页既无法产生真实数据，也不能验证账户切换、重置、离线和权限撤销。
- 结论：**No-Go。** 保留“宿主权威来源、每字段时间戳、过期 fail-closed、来源仲裁可解释”作为未来小队雷达协议升级门禁；先由上位机定义稳定、最小化、可撤销的数据契约。

### 4.2 `lleontor705/pi-mascot`：九态安全吉祥物与减弱动态

- 来源：[仓库](https://github.com/lleontor705/pi-mascot)，MIT；初始功能提交 [`82a02549a61ded228b3d2f6b810e2694f78328ba`](https://github.com/lleontor705/pi-mascot/commit/82a02549a61ded228b3d2f6b810e2694f78328ba)，`2026-09-12T17:55:36Z`，约 `+4042`、25 个文件；同窗 [`f69f9fa75e61b47422392f68f8c0256222489c2f`](https://github.com/lleontor705/pi-mascot/commit/f69f9fa75e61b47422392f68f8c0256222489c2f) 发布首个图库版本。
- 原创性与闭环：把宿主事件映射为 idle/thinking/reading/writing/testing/success/warning/blocked/resting 九态；支持 full/reduced/off 动态、窄终端降级、无宿主 API 安全退化和会话结束清理。声明零网络、零持久化、零遥测，产品边界清楚。
- 用户价值：状态比单一“忙碌”更具体；减弱动态模式有可访问性价值，安全边界也容易解释。
- 不进入开发：目标已经有 Agent 状态、多 GIF、自主行为、任务花园和小队雷达；再建立九态 reducer 会形成第二套状态机。reading/writing/testing 等细粒度事件目前也未由目标 BLE 协议提供，若靠设备推断会制造错误状态。可访问性应作为现有 GIF 播放速度、静态回退和动效开关的跨端设置需求评估，而不是复制 Perrogato 字符、帧或状态名。
- 许可证/素材：代码为 MIT；Perrogato 名称、字符图和动画帧仍视为来源资产，不复制。
- 结论：No-Go。其“未知事件安全降级、关闭动效后停止计时、生命周期清理”纳入现有多 GIF 与自主行为的验收原则。

### 4.3 `bgrablin/hermes-personal-display`：过期快照 fail-closed

- 来源：[仓库](https://github.com/bgrablin/hermes-personal-display)，MIT；提交 [`cf7214cc053e1a8aca79058928bfa5b9f14e0dec`](https://github.com/bgrablin/hermes-personal-display/commit/cf7214cc053e1a8aca79058928bfa5b9f14e0dec) 让已保持的 inspector 快照和控制项过期，最终提交 [`1386a82ed0fc49cd8a1cd259bd6285245ba27373`](https://github.com/bgrablin/hermes-personal-display/commit/1386a82ed0fc49cd8a1cd259bd6285245ba27373) 于 `2026-09-13T03:15:55Z` 增加 `+95/-6` 的年龄非法 fail-closed 与集成测试。
- 价值：状态屏不会把缺失、负数、非数字或过期时间误显示为仍可操作；这是跨端信任的重要基础。
- 不进入开发：属于可靠性修复而非新用户功能；目标已有快照有效期/新鲜度观察项，应在小队雷达和未来可操作入口中统一完成，不能为它另建宠物状态页。
- 结论：No-Go，但把“年龄非法等同过期、过期同时撤销控制权、旧缓存不冒充实时状态”加入存量验收清单。

### 4.4 `charliejgallo/ESP32S3_AmoledOS`：低资源脏矩形打地鼠

- 来源：[仓库](https://github.com/charliejgallo/ESP32S3_AmoledOS)，MIT；Topos 提交 [`bb6565ebde9e61907742a96ad4223e8502a132db`](https://github.com/charliejgallo/ESP32S3_AmoledOS/commit/bb6565ebde9e61907742a96ad4223e8502a132db)，`2026-09-12T18:35:17Z`，约 `+6214/-2`、18 个文件。
- 原创性与闭环：三种模式、难度递增、连击、生命和分模式纪录；每洞只用 16 字节结构，变化区域从背景和重叠槽重建，主机 harness 对 18,000 帧与全量重绘做一致性校验。来源报告真机渲染精灵约 22 ms，连续点击 frenzy 约 29 fps。
- 用户价值：短时触摸游戏完整、反馈快；脏矩形和同路径边界测量对小屏性能有工程参考价值。
- 不进入开发：目标已有玩球和任务花园，普通打地鼠没有新的陪伴关系或 Agent 闭环；还会争用宠物页触摸、视觉、音频和定时器。来源是 ESP32-S3/ESP-IDF 动态应用，屏幕、内存和生命周期不同；三模式、纪录、素材和高频点击真机验证无法在本轮无硬件情况下闭环。
- 许可证/素材：代码为 MIT；鼹鼠、炸弹、锤子、图标、声音和关卡参数仍按独立内容资产处理，不复制。
- 结论：No-Go。只保留“固定小状态、按 dirty rect 更新、优化实现与全量重绘逐帧对比”的性能测试方法。

### 4.5 `ZhiFun/FunAIGameEngine`：单文件动态游戏卡带

- 来源：[仓库](https://github.com/ZhiFun/FunAIGameEngine)，AGPL-3.0，窗口内新建；版本提交 [`aee23b291bb1a4c1f4627dc1342c90d83c593f93`](https://github.com/ZhiFun/FunAIGameEngine/commit/aee23b291bb1a4c1f4627dc1342c90d83c593f93)，`2026-09-13T14:01:29Z`，约新增 387,322 行并包含大量 PNG/WAV、脚本、二进制卡带、引擎和工具。
- 原创性与闭环：`.gbn` 单文件打包字节码、字符串和资产，支持参考 VM、真实引擎模拟器、打包和硬件导出；对内容生产和分发形成完整工具链。
- 用户价值：理论上可让用户或团队持续增加小游戏，而不用把每个游戏都编译进主固件。
- 不进入开发：它引入不可信字节码解释、长度/偏移/算术边界、资产解码、CPU/内存配额、签名、版本兼容、撤销、回滚和内容版权等新的供应链攻击面。目标现有 OTA 资源分支尚未通过发布者认证与回滚门禁，不能先增加可执行卡带；参考分辨率 428×142、Qt/PlatformIO/ESP32-S3 栈也不适配 SF32/RT-Thread。
- 结论：**明确 No-Go。** 在签名资源包、可信发布、静态白名单和解析器模糊测试完成前，不接收或执行任何动态脚本/卡带。

### 4.6 天气、音乐、通用 LVGL 与其他窗口更新

| 候选 | 严格窗口证据 | 评估与结论 |
| --- | --- | --- |
| [cybernully/family_calendar_lvgl](https://github.com/cybernully/family_calendar_lvgl) | [`18577917f8755fa6e3744ea9cf69b6e1b9a980d2`](https://github.com/cybernully/family_calendar_lvgl/commit/18577917f8755fa6e3744ea9cf69b6e1b9a980d2)，`2026-09-13T13:52:57Z`，天气页、日历天气条、RAM 缓存和安全周预取，约 `+536/-561` | 天气陪伴已有 PRD/固件分支且仍缺真实上位机授权和同步闭环；来源依赖 Home Assistant、Wi-Fi、ESP-Hosted 和大屏日历，不重复开发。可借鉴隐藏页不轮询、缓存优先、去抖合并请求和睡眠暂停刷新 |
| [Rem01Gaming/leticia_lvgl](https://github.com/Rem01Gaming/leticia_lvgl) | [`271864c9fdd2e584fb6e68de5d96ec9dbbaf8233`](https://github.com/Rem01Gaming/leticia_lvgl/commit/271864c9fdd2e584fb6e68de5d96ec9dbbaf8233)，`2026-09-13T14:28:21Z`，初次音乐播放测试，约 `+618/-236` | 目标已有录音、本地播放和音乐功能；来源基于 Linux/ALSA/Android 设备配置且为初测，无新宠物闭环。GPL-3.0 与素材需独立评估，不移植 |
| [gidano/Moon-Radio](https://github.com/gidano/Moon-Radio) | 窗口内多次 `Add files via upload`，最后 [`32720715ba0f7c5deb795df53f9004aec5cd871d`](https://github.com/gidano/Moon-Radio/commit/32720715ba0f7c5deb795df53f9004aec5cd871d)，`2026-09-13T13:30:32Z` | 网络电台、专辑封面、天气、频谱和网页管理均需要 Wi-Fi/HTTP/文件系统；目标已有音乐/天气方向，且来源明确局域网文件 API 无密码，不引入该安全模型 |
| [NeoXider/neoxider-agent-deck](https://github.com/NeoXider/neoxider-agent-deck) | [`e8a91344a242df1eb308000578207c0882a7ad8a`](https://github.com/NeoXider/neoxider-agent-deck/commit/e8a91344a242df1eb308000578207c0882a7ad8a)，`2026-09-12T20:16:00Z`，长会话性能、会话恢复、子代理计数和窗口动效 | 小队雷达已显示多会话；完整聊天、历史恢复和桌面窗口身份不应下放到 240×240 固件。只保留“长列表有界、恢复状态不能冒充实时状态”的原则 |
| [TheMasterCoder007/lvgl-live-preview](https://github.com/TheMasterCoder007/lvgl-live-preview) | [`e364f048fbb4147ab86daf4f00465c9c3f40c9f8`](https://github.com/TheMasterCoder007/lvgl-live-preview/commit/e364f048fbb4147ab86daf4f00465c9c3f40c9f8)，`2026-09-13T15:57:47Z`，约 `+13/-4`，支持 LVGL 9.5/SDL 软件驱动 | 开发工具兼容性，不是宠物/手表用户功能；目标仍使用既有 SDK/LVGL 版本，不为预览工具升级生产栈 |
| [VictorTran1023/ai-passport-guangfu-tongsheng](https://github.com/VictorTran1023/ai-passport-guangfu-tongsheng) | [`08ded5f79e627933d797d5b23d3946f69d287135`](https://github.com/VictorTran1023/ai-passport-guangfu-tongsheng/commit/08ded5f79e627933d797d5b23d3946f69d287135)，`2026-09-13T02:14:14Z`，恢复电量表百分比 | 显示可靠性修复，不是新陪伴功能；电量能力应在整机真实电源数据基础上处理，不复制 UI 或数据集 |
| [socquique/TamaPoke](https://github.com/socquique/TamaPoke) | 窗口末三次 README 购物链接更新，最后 [`93ca723a3bd0a54ef30b7cb5023ca424e46df1fc`](https://github.com/socquique/TamaPoke/commit/93ca723a3bd0a54ef30b7cb5023ca424e46df1fc)，`2026-09-13T15:26:31Z` | 无产品代码增量；来源含 Pokémon/第三方素材与非商业许可风险，不复制、不评分为功能 |

## 5. 量化排序

评分 1～5；适配和许可证越高越有利，工作量与风险越高表示越轻、越可控。硬门禁优先于总分。

| 方向 | 原创性 | 用户价值 | 差异化 | 交互闭环 | 硬件适配 | 许可证/素材 | 工作量 | 风险 | 合计/40 | 决策 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 宿主配额源与新鲜度仲裁 | 4 | 4 | 2 | 5 | 1 | 2 | 1 | 2 | **21** | 与额度/小队雷达重复；缺上位机和协议 |
| 九态安全吉祥物与减弱动态 | 3 | 4 | 2 | 4 | 2 | 2 | 3 | 4 | **24** | 与状态/多 GIF/自主行为重复；缺细粒度事件 |
| 过期快照 fail-closed | 2 | 4 | 2 | 3 | 4 | 2 | 4 | 5 | **26** | 应作为存量可靠性门禁，不独立立项 |
| 脏矩形打地鼠 | 3 | 3 | 2 | 5 | 2 | 1 | 2 | 3 | **21** | 普通小游戏，与玩球争用资源和触摸 |
| 单文件动态游戏卡带 | 5 | 3 | 5 | 5 | 1 | 1 | 1 | 1 | **22** | 执行与供应链风险，不得在 OTA 门禁前引入 |
| 日历天气缓存与预取 | 2 | 4 | 1 | 5 | 1 | 1 | 2 | 3 | **19** | 天气陪伴重复，仍缺跨端授权与同步 |

最高分的过期快照是工程质量改进，不是新的用户闭环；相对评分不能覆盖产品去重和端到端硬门禁。

## 6. 决策与后续触发条件

1. 本轮不推荐新功能，不创建 PRD，不启动嵌入式子任务；只保存扫描记录。
2. 优先把窗口发现用于存量验收：小队雷达/跨端状态必须携带来源和新鲜度，非法或过期年龄 fail-closed；未知状态回退为安全只读；关闭动效后停止相关计时器并在页面退出清理。
3. 若未来上位机能提供经过用户授权、可撤销、带来源和时间戳的真实额度快照，再把额度作为小队雷达的可选只读页评估；不得把显示短哈希、缓存值或设备推断当成账户事实。
4. 天气陪伴应先完成手机/电脑端天气来源授权、数据最小化、刷新退避、缓存过期、撤销与重连全量同步；不能因另一个天气 Demo 出现而绕过原有门禁。
5. 动态脚本、卡带或资源包必须等 OTA 发布者认证、版本/长度/哈希、原子提交、回滚、路径白名单和解析器安全验证完成后再议；本项目不得执行来源二进制或未经审查代码。
6. 后续小游戏只在用户研究证明其增强 Momo 陪伴而非单纯堆内容、且具自有素材、统一触摸/视觉/音频仲裁和真机资源预算时重评；本轮 Topos 仅作为脏矩形验证方法参考。
7. 本提交不开发、不推送、不创建 PR、不合并默认分支。由主任务确认本地提交后，按自动化规则推送同名远端分支。
