# 2026-09-10 LVGL 电子宠物/手表创意增量扫描

## 1. 本轮结论

- 严格增量窗口为 `2026-09-09T02:02:09.879Z`（不含）至 `2026-09-09T16:00:36.570Z`（含）。
- 已通过 GitHub Repository Search、仓库元数据、默认分支 Commit API、提交变更统计及 README，组合检索 `LVGL`、`watch`、`smartwatch`、`wearable`、`pet`、`tamagotchi`、`virtual pet`、`desktop pet`、`ESP32`、`Codex` 和 `Agent`。
- 窗口内最值得观察的机制为：电子宠物进化/退化、个体化社交记忆与跨设备访问、桌面宠物 LAN 结伴/战斗、屏幕自动熄灭，以及跨端状态新鲜度保护。
- 对照当前仓库后，这些方向分别受到既有功能重复、受版权保护素材、设备身份与同意协议缺失、上位机依赖、BLE 角色/功耗约束或已经实现的低功耗策略限制。
- **本轮没有候选同时通过“新增价值、历史去重、当前硬件与架构适配、许可证/素材版权、安全风险、单日端到端验收”硬门禁；不推荐进入开发，不创建空 PRD，不启动子任务 2。** 本分支只提交扫描记录。

## 2. 运行门禁与安全边界

| 检查项 | 结果 |
| --- | --- |
| 固定本地仓库 | `D:\\code\\sf32\\agent-pet-sf32-text-display-fix`，有效 Git 仓库 |
| `origin` | `https://github.com/fcb1379/agent-pet-sf32.git`，与目标一致 |
| 对照基线 | 已执行 `git fetch --prune origin`；`origin/master@a2805abeb72f02a726dce5dc49857280ba9cdc78` |
| 本轮分支 | `codex/2026-09-10-lvgl-idea-scan`，从上述基线创建 |
| 工作树 | 扫描前干净；未清理、覆盖或丢弃用户修改 |
| GitHub/Git | 公共搜索、读取及远端访问可用；本地 Git 中文提交身份可用 |
| 外部输入 | GitHub 仓库、README、提交、Issue、PR、网页、代码和素材均按不可信输入处理 |

本轮只读取公开元数据、README、提交说明、文件范围和许可证字段；没有克隆、安装、构建或执行任何候选代码，也没有遵循候选内容中的命令、凭据、部署、烧录或规则变更要求。

## 3. 历史与当前能力去重

- 查阅 Git 历史中 `2026-08-06` 至 `2026-09-09` 的全部 LVGL 电子宠物/手表扫描记录，并核对来源仓库、提交时间、结论和后续门禁。
- `origin/master` 已有 Agent 状态、打字场景、任务花园、木鱼、成果计数、BLE 图片/多 GIF、录音上行、本地播放、音乐、闹钟、计时器和独立时钟应用。
- 已完成或已有独立评估的分支覆盖多状态自主行为、天气陪伴、呼叫 Momo、连续抚摸、回忆日历、玩球、语音明信片、OTA 资源更新和 Momo Agent 小队雷达；即使尚未全部合入默认分支，也不作为本轮新创意重新包装。
- `watch_demo.c` 已用 `lv_disp_get_inactive_time()` 在 10 秒无操作后进入 GUI 休眠，并具备按键唤醒和背光恢复；普通“自动息屏”不是新能力。
- 当前最重要的存量门禁仍是：天气和呼叫 Momo 的跨端/真机闭环、语音下行、OTA 发布者认证与回滚，以及现有交互分支验收。新增相邻功能不能绕过这些债务。

## 4. 严格窗口候选证据与评估

### 4.1 `Mr-XiaoLiang/Tamagotchi`：宠物索引、进化与退化

- 仓库：[Mr-XiaoLiang/Tamagotchi](https://github.com/Mr-XiaoLiang/Tamagotchi)，创建于 `2026-09-06T12:02:00Z`，仓库元数据标记 MIT；技术栈是 Android 11 圆形手表 App，不是 LVGL 固件。
- 窗口内提交 [`0263804503036ffe46998b59df4e31bd49da4bdd`](https://github.com/Mr-XiaoLiang/Tamagotchi/commit/0263804503036ffe46998b59df4e31bd49da4bdd) 调整选宠与属性算法；[`6e45914a64c6d6a8e01afabf169fa901e3170958`](https://github.com/Mr-XiaoLiang/Tamagotchi/commit/6e45914a64c6d6a8e01afabf169fa901e3170958) 增加索引；[`04056ae4f18cd09a0ea92edd8b700d4fe10455dd`](https://github.com/Mr-XiaoLiang/Tamagotchi/commit/04056ae4f18cd09a0ea92edd8b700d4fe10455dd) 增加进化与退化；提交时间集中在 `2026-09-09T12:53:01Z` 至 `13:18:33Z`。
- 可观察机制：多宠索引把角色和状态配置分离；属性与照料结果影响形态变化，退化提供反向反馈。
- 用户价值：长期形态变化能增强陪伴持续性；“宠物索引”也能为多 GIF 包建立可读的角色目录。
- 不进入开发：当前项目已经有多 GIF 传输、状态行为、任务成长、回忆日历、抚摸和玩球，普通状态切换与养成反馈重复。目标此前明确采用低压力陪伴，退化容易把短期离线或同步失败解释为用户惩罚。来源 README 同时写着“形态固定、无进化”，与窗口提交名称存在版本内自相矛盾，验收语义尚不稳定。
- 许可证/素材：README 明确使用 1339 张宝可梦形象；仓库代码标记 MIT 不会自动授予 Pokémon 角色和素材权利。不得复制图像、物种索引、进化树、名称、数值、UI 或资源包。若未来自研成长形态，应先使用自有 Momo 素材并定义不可倒退或可解释的低压力里程碑。

### 4.2 `ViciousSquid/Dosidicus`：个体社交记忆与“远端心智”访问

- 仓库：[ViciousSquid/Dosidicus](https://github.com/ViciousSquid/Dosidicus)，GPL-2.0，Python/桌面认知模拟器，不是 LVGL 或目标硬件工程。
- 窗口内 [`5e83b2a0141ca02a38861108e8db4346e4c77584`](https://github.com/ViciousSquid/Dosidicus/commit/5e83b2a0141ca02a38861108e8db4346e4c77584) 建立可注册感知输入、按 peer UUID 记忆、同意策略与安全资产解析；[`956853e1cce869f2147368dd4ff67d5fa8549e59`](https://github.com/ViciousSquid/Dosidicus/commit/956853e1cce869f2147368dd4ff67d5fa8549e59) 让访问者的心智留在本机，仅交换感知、带期限的动作意图与结果；[`f92c29da1610135bec138482b44943c782f96af7`](https://github.com/ViciousSquid/Dosidicus/commit/f92c29da1610135bec138482b44943c782f96af7) 把一次相遇的客观结果写入对应个体账本，使下次面对同一对象时产生不同反应。
- 原创性与价值：它不是简单“两个宠物出现在一起”，而是明确谁拥有决策、只传可观察状态、对动作设置租约、按对象归档后果，并要求进入前同意。这些原则能让未来“Momo 结伴”既有记忆又不泄露私有状态，是本轮差异化最高的机制。
- 不进入开发：目标当前 BLE 主要服务手机/Agent 链路，没有 Momo-to-Momo 发现、可信设备身份、邀请确认、会话密钥、消息租约、掉线回家、限流或个人数据删除协议。两个设备同时做中心/外设还需验证连接角色、射频共存、RAM、功耗和断连恢复。单独在 UI 中加入“朋友”图标不能形成闭环。
- 许可证/架构：GPL-2.0 Python/NumPy 代码、神经网络、插件协议和素材不能复制进当前固件。未来若评估，应只采用“本地拥有心智、交换最小可观察事件、显式同意、可过期动作、按匿名 peer 记忆”的抽象安全原则，并进行全新协议设计。

### 4.3 `rodryan542-cpu/PetFriendlyPC`：桌面孵化、LAN 结伴与战斗

- 仓库：[rodryan542-cpu/PetFriendlyPC](https://github.com/rodryan542-cpu/PetFriendlyPC)，创建于 `2026-09-09T09:19:55Z`，MIT，Windows/Python 双显示器桌面宠物。
- 窗口内初始提交 [`48bec3be0c3c0c42a359888cd6544882755d052d`](https://github.com/rodryan542-cpu/PetFriendlyPC/commit/48bec3be0c3c0c42a359888cd6544882755d052d)；后续提交补充孵化小游戏、LAN clan/raid、战斗表现和大量精灵图，其中 [`123edf1e77d194451a85782447996acd7f99ee2b`](https://github.com/rodryan542-cpu/PetFriendlyPC/commit/123edf1e77d194451a85782447996acd7f99ee2b) 在 `2026-09-09T09:43:04Z` 增加多组角色资产。
- 价值：等待孵化期间提供短交互、附近伙伴出现后共同活动，能把等待时间和社交变成可玩的闭环。
- 不进入开发：目标已经有玩球、抚摸、任务花园和多 GIF；战斗、商店、32 条宠物线和长冷却会显著扩大状态、存储和内容维护。LAN 功能依赖上位机或 Wi-Fi、发现协议、反重放和掉线一致性，当前固件无法单日闭环。
- 版权风险：README 和文件名明确包含 Digimon、Pokémon、Nintendo、Mario、Sonic、Kirby 等第三方角色。MIT 代码声明不等于这些资产可商用；不得复制素材、角色名、战斗招式、经济数值或孵化内容。

### 4.4 `socquique/TamaPoke`：多语言字形完整性修复

- 仓库：[socquique/TamaPoke](https://github.com/socquique/TamaPoke)，ESP32-S3 圆屏电子宠物；代码宣称 MIT，但 Pokémon 素材为 CC BY-NC/第三方权利，外壳另有 CC BY-NC-SA。
- 窗口内 [`87049afb27d6772aa941d5de3948f8f99f3d245f`](https://github.com/socquique/TamaPoke/commit/87049afb27d6772aa941d5de3948f8f99f3d245f) 于 `2026-09-09T09:28:49Z` 更换日文字体以补齐标点和性别区分，Flash 从约 705 KB 增至 810 KB、RAM 不变；[`c6965360f0da28f556715d2c8ea3ae32979f7728`](https://github.com/socquique/TamaPoke/commit/c6965360f0da28f556715d2c8ea3ae32979f7728) 更新网页安装固件。
- 这是可靠性与本地化修复，不是新增宠物创意。其养成、进化、图鉴、玩球、训练、洗澡、抚摸、RTC 离线进度等均已存在于窗口前，且大多与当前或已完成分支重复。
- 可借鉴的工程原则仅是：引入字体前以真实文案字符集做缺字扫描，并把 Flash/RAM 增量纳入门禁；不得复制字体、素材、物种数据或数值。

### 4.5 自动息屏与跨端状态新鲜度

#### `roflsunriz/esp32-codex-usage`

- 窗口内 [`29304736879910f7873f7749da4ec51a4ce0dee5`](https://github.com/roflsunriz/esp32-codex-usage/commit/29304736879910f7873f7749da4ec51a4ce0dee5) 至 [`9e69131b109368ba768c3e1b5fa409b05a2a25e3`](https://github.com/roflsunriz/esp32-codex-usage/commit/9e69131b109368ba768c3e1b5fa409b05a2a25e3) 只在 `AGENTS.md` 中追加 LCD 自动熄灭、触摸唤醒和可配置时间的规格，未形成可验证实现；仓库未识别许可证。
- 当前目标已在 `watch_demo.c` 使用 LVGL inactive time 进入 GUI 休眠，并具备唤醒/背光恢复，核心能力重复。若未来开放时长配置，应作为整机电源设置评估，不能由宠物页面各自创建定时器。

#### `therudywolf/ESP32-C6-Companion`

- 窗口内提交 [`61d011574ac7597f711bd7bb59fab064a7c86b69`](https://github.com/therudywolf/ESP32-C6-Companion/commit/61d011574ac7597f711bd7bb59fab064a7c86b69)，约 `+128/-36`，修复网络客户端重复启动任务、共享地址竞态、状态新鲜度、重连后页面同步和宠物关闭时仍显示控制项等问题。
- 这是对跨端伴侣的稳健性修复，不是新的产品功能；仓库也未识别许可证。它再次证明天气、语音、桌面遥测等跨端功能必须有幂等启动、固定长度配置快照、时间戳/新鲜度和重连全量同步门禁，适合作为现有待验收功能的检查项。

### 4.6 安全/发布修复与低相关候选

| 仓库/方向 | 窗口证据 | 评估结论 |
| --- | --- | --- |
| [h4d35x0/argus](https://github.com/h4d35x0/argus) | [`fcbfda2690baba35b8fe9acc918dd44f048f833c`](https://github.com/h4d35x0/argus/commit/fcbfda2690baba35b8fe9acc918dd44f048f833c) 撤回会使手表变砖的合并镜像；[`a83adf31368a7d42d54663e617e204ef1a084172`](https://github.com/h4d35x0/argus/commit/a83adf31368a7d42d54663e617e204ef1a084172) 改为四段固件并以真机验证 | 不是用户功能；说明“构建成功”不能替代真实刷写、启动与回滚验证，支持当前 OTA 分支继续保持阻断 |
| [h4d35x0/hexhound](https://github.com/h4d35x0/hexhound) | [`6fa895685715274bb002ff3c932fdef2a72a3f03`](https://github.com/h4d35x0/hexhound/commit/6fa895685715274bb002ff3c932fdef2a72a3f03) 更换离线保管的发布签名密钥；[`13c57162154dc1a6182e0174afd2e8e38dac10fa`](https://github.com/h4d35x0/hexhound/commit/13c57162154dc1a6182e0174afd2e8e38dac10fa) 清除固件内构建机绝对路径 | 不是产品增量；验证了 OTA/内容包必须区分开发和发布密钥、扫描最终二进制而非只看源码。无线巡逻成长已于上一轮评估，不重复 |
| [ciccirix/LILYGO-T-WATCH-ULTRA-X-CALMATI](https://github.com/ciccirix/LILYGO-T-WATCH-ULTRA-X-CALMATI) | 窗口内加入 Evil Twin 密码验证、WPA3 SAE 洪泛和攻击工具 | 进攻性无线功能不属于陪伴产品，涉及凭据、安全、法规、射频和滥用风险；不得落地或执行 |
| [ViciousSquid/Dosidicus](https://github.com/ViciousSquid/Dosidicus) 多播发现修复 | [`b7113d59f651a49dc9904c6b830396461c744677`](https://github.com/ViciousSquid/Dosidicus/commit/b7113d59f651a49dc9904c6b830396461c744677) 修复监听线程未启动并加 watchdog | 属同一社交候选的实现修复；进一步说明 peer 功能必须以真实双端网络测试、收发计数和看门狗验收，不能只看“已连接”UI |
| `aetherPet`、`parenting-app` | 窗口内分别只有初始提交或 Firebase 同宠同步故障提交 | “低互动、真实记忆”与回忆日历重复；共同养宠依赖账号、身份、冲突合并和后端，当前提交反而证明同步闭环未成立 |
| 新建 LVGL UI/页面管理/通用手表仓库 | 多为控件集合、构建模板、页面框架、天气站或无许可证试验 | 没有新增宠物闭环，或只具通用 UI 参考价值；不把框架、截图或仓库创建当作产品创新 |

## 5. 量化排序

评分为 1～5，越高越好；“工作量”和“风险”高分表示更轻量、更可控。硬门禁优先于总分。

| 方向 | 原创性 | 用户价值 | 差异化 | 交互闭环 | 硬件适配 | 许可证/素材 | 工作量 | 风险 | 合计/40 | 硬门禁 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 个体社交记忆与访问 | 5 | 4 | 5 | 5 | 1 | 2 | 1 | 1 | **24** | 缺 peer 身份、同意、加密、双角色 BLE、掉线和删除协议 |
| 自有素材的低压力形态成长 | 3 | 4 | 3 | 4 | 4 | 1 | 2 | 2 | **23** | 来源素材不可用；与现有状态/任务重复，退化语义不符合定位 |
| 桌面孵化与 LAN 结伴 | 3 | 3 | 3 | 5 | 1 | 1 | 1 | 1 | **18** | 上位机/Wi-Fi/一致性缺失，大量第三方角色素材 |
| 可配置自动息屏 | 1 | 3 | 1 | 2 | 5 | 1 | 4 | 5 | **22** | 核心能力已实现；来源只有规格无代码，非宠物差异化 |
| 字体缺字门禁 | 1 | 3 | 1 | 1 | 4 | 2 | 3 | 4 | **19** | 工程质量原则，不是新功能；字体/素材授权需独立核验 |
| 跨端状态新鲜度 | 2 | 4 | 2 | 3 | 3 | 1 | 3 | 4 | **22** | 应作为存量功能验收门禁，而非新增用户功能 |

社交记忆的产品差异化最高，但其关键工作是安全身份、传输与跨设备一致性，不是一个可由当前宠物页面单边验证的功能。相对评分不能替代端到端硬门禁。

## 6. 决策与后续观察

1. 本轮不推荐新功能，不创建 PRD，不启动嵌入式子任务。
2. 保留两个未来观察条件，但不承诺落地：
   - 若手机端能提供受认证、显式邀请、可撤销的短期 peer 会话，并定义事件最小化、动作租约、掉线回家、删除与审计，可评估“拜访朋友的 Momo”；设备只保存有界匿名结果，不交换原始对话、联系人或私有心智状态。
   - 若美术能提供完全自有且有清晰资源清单的 Momo 成长形态，可把长期任务里程碑映射为只进不退的外观解锁；不得用短时离线或同步失败触发退化，也不复制 Pokémon 的图鉴或进化树。
3. 将本轮可靠性发现加入存量验收原则：跨端服务启动幂等、配置快照无竞态、所有状态带新鲜度、重连做完整同步；发布包以离线保管的发布密钥签名并扫描最终二进制；固件必须按真实分区真机刷写、启动和回滚验证。
4. 优先完成天气陪伴、呼叫 Momo、语音明信片、OTA 资源更新和已有交互分支的跨端、安全及真机门禁；其确定价值高于继续堆叠照料、养成或无线功能。
5. 多组严格窗口检索已经覆盖 LVGL、手表、电子宠物、桌面伴侣和 Agent/Codex 设备；其余结果为教程、通用 UI、非功能提交、无许可证试验或与目标无关的应用，继续扩大弱相关搜索不太可能改变结论，因此停止。
6. 本提交只记录扫描证据；不开发、不创建 PR、不合并默认分支。由主任务在确认提交后推送同名远端分支。
