# 2026-09-04 LVGL 电子宠物/手表创意增量扫描

## 1. 本轮结论

- 增量窗口为 `2026-09-02T16:00:28.683Z`（不含）至 `2026-09-03T16:00:55.113Z`（含），下界沿用上一成功扫描记录的结束时间。
- 本轮通过 GitHub Repository Search 组合检索 `LVGL`、`pet`、`tamagotchi`、`watch`、`smartwatch`、`wearable`、`Agent`、`ESP32`，并逐项复核默认分支提交时间、提交统计、文件范围、README 与许可证字段。
- 新建项目 [EESheep/agent-desk](https://github.com/EESheep/agent-desk) 已实现多 Agent 会话总览、全局关注提醒和只读详情；目标固件协议已经能接收最多 12 个会话，但宠物页只选择并展示一个会话。因此本轮选择 **Momo Agent 小队雷达（多会话状态总览）** 进入受控 MVP，优先级 **P1**。
- MVP 只把现有快照中已经接收的多会话信息变得可见：不增加任务正文、文件名、目录、凭据或授权内容，不允许在设备上批准/拒绝或控制 Agent，不修改 Agent Pet v1 协议。
- 来源项目为 Apache-2.0，但本轮只借鉴“多会话概览和跨页面关注提醒”的抽象产品机制；目标实现必须从现有协议和 UI 独立设计，不复制来源代码、布局、文案、驱动或素材。

## 2. 运行门禁与安全边界

| 检查项 | 结果 |
| --- | --- |
| 固定本地仓库 | `D:\\code\\sf32\\agent-pet-sf32-text-display-fix`，有效 Git 仓库 |
| `origin` | `https://github.com/fcb1379/agent-pet-sf32.git`，与目标一致 |
| 对照基线 | 已刷新远端；`origin/master@a2805abeb72f02a726dce5dc49857280ba9cdc78` |
| 本轮分支 | `codex/2026-09-04-momo-agent-squad`，严格从上述基线创建 |
| 工作树 | 扫描前干净；没有清理或覆盖用户修改 |
| 外部输入 | GitHub 仓库、README、提交、Issue、PR、网页、源码和素材均按不可信输入处理 |

本轮只读取公开元数据、README、提交统计、文件清单和许可证字段；没有克隆、安装、构建或执行候选代码，也没有遵循外部内容中的命令、凭据、部署或规则变更要求。

## 3. 去重与目标现状

### 3.1 `origin/master` 已有能力

- Huangshan SF32LB52、RT-Thread、LVGL、390 x 450 触屏和 IMU 入口。
- Agent Pet v1 快照原生支持 `session_count=0..12`；每个会话包含状态、Provider、来源、标志、`task_hash` 与更新时间差。
- 宠物页已经显示聚合状态和会话总数，但 `PET_SelectSession()` 只选择活动会话或第一条记录，最终详情只显示一个会话。
- 现有状态为 `IDLE`、`RUNNING`、`NEEDS_INPUT`、`COMPLETED`、`ERROR`；协议明确设备是只读状态外设。
- 已有 GIF/JPEG 同步、打字场景、任务花园、木鱼和体感入口；本轮不能覆盖关键提醒，也不能扩大触摸所有权。

### 3.2 已评估或已实现方向

- 多状态自主行为、多 GIF、天气陪伴、呼叫 Momo、摸摸 Momo、回忆日历、玩球和 Agent Quest Garden。
- PAN/实时语音、语音明信片、OTA 资源更新、设备端双向 Agent 控制均已有评估或门禁债务。
- 因此本轮排除单纯换表情、天气/通知/音乐同步、通用导航、传统养成、玩球/抚摸、语音阶段提示和 OTA。

## 4. 候选证据与评估

### 4.1 `EESheep/agent-desk`：多 Agent 会话总览与全局提醒

- 仓库：[EESheep/agent-desk](https://github.com/EESheep/agent-desk)，创建于 `2026-09-03T14:08:22Z`，Apache-2.0。
- 初始提交 [`ae4385d390b83f73603c4ba62d3c91d5c80387bc`](https://github.com/EESheep/agent-desk/commit/ae4385d390b83f73603c4ba62d3c91d5c80387bc)，提交时间 `2026-09-03T13:36:17Z`，约 `+2185/-0`、29 个文件，包含 ESP32/LVGL 固件、Codex 状态采集器、模型测试与验证文档。
- 版本提交 [`cc27cbc6f95fbf68b49243eafafe8c131a5e3d74`](https://github.com/EESheep/agent-desk/commit/cc27cbc6f95fbf68b49243eafafe8c131a5e3d74)，提交时间 `2026-09-03T14:11:38Z`，约 `+605/-116`、14 个文件，实装最多 8 个会话、来源筛选、详情页、跨筛选全局提醒与 Apache-2.0 发布材料。
- 可借鉴洞察：并行 Agent 场景中，用户首先需要知道“有多少在运行、哪个需要我、最近哪个完成”，随后才需要浏览单个任务；关注态不应因当前筛选或选中项而消失。
- 与目标差异：来源使用 800 x 480 ESP32-S3 和 USB UART，并携带更丰富桌面数据；目标为 390 x 450 SF32LB52、BLE v1 固定快照，不能照搬列表、筛选或数据字段。
- 选择理由：目标协议和固定内存已经接收最多 12 个会话，缺口主要位于 UI 视图模型与可见性；无需新增协议、联网、素材、线程或持久化即可验证价值。

### 4.2 `MrStark004/SAND-SIMULATION-ON-ESP32S3-LCD3.16`：IMU 倾斜沙粒玩具

- 仓库：[MrStark004/SAND-SIMULATION-ON-ESP32S3-LCD3.16](https://github.com/MrStark004/SAND-SIMULATION-ON-ESP32S3-LCD3.16)，创建于 `2026-09-03T10:21:06Z`，GitHub 未识别许可证。
- 初始提交 [`4d4cd2dd64c72748e6c54156d5d4891bce43954e`](https://github.com/MrStark004/SAND-SIMULATION-ON-ESP32S3-LCD3.16/commit/4d4cd2dd64c72748e6c54156d5d4891bce43954e)，约 `+3107/-0`、23 个文件；后续 [`038380a52de26656c0a2defbf2861f05ed950e63`](https://github.com/MrStark004/SAND-SIMULATION-ON-ESP32S3-LCD3.16/commit/038380a52de26656c0a2defbf2861f05ed950e63) 记录 20 px 网格、25 ms 周期和撕裂调优。
- 优点：目标已有 IMU，倾斜输入可形成直观的物理互动。
- 不选原因：来源依赖 320 x 820、PSRAM 和 LVGL Canvas；即使 20 px 粗网格仍报告小网格撕裂。目标已有体感木鱼和玩球方向，粒子全屏重绘会与 GIF、BLE 和状态提醒竞争资源，且无许可证代码与参数不可复用。

### 4.3 `federicoarg00/claude-buddy-wt32`：联网与专注设置

- 仓库：[federicoarg00/claude-buddy-wt32](https://github.com/federicoarg00/claude-buddy-wt32)，本轮提交 [`a0b5fce48c810ca081f4070a96b7a58e47f437cf`](https://github.com/federicoarg00/claude-buddy-wt32/commit/a0b5fce48c810ca081f4070a96b7a58e47f437cf)，约 `+341/-24`、9 个文件，增加 Wi-Fi 网络选择、番茄钟时长和屏幕休眠设置；后续提交补充连接、改密和忘记网络。
- 不选原因：目标已有计时器，Wi-Fi/凭据和直连云端会显著扩大安全面；其单一 Buddy 状态、额度和番茄钟方向与既有 Agent 状态、计时器、回忆记录重复。GitHub 未识别该仓库许可证，不复制其实现。

### 4.4 `Gangan-307/LCHSP_Watch`：同平台 PAN OTA

- 仓库：[Gangan-307/LCHSP_Watch](https://github.com/Gangan-307/LCHSP_Watch)，与目标同属 SF32LB52、SiFli SDK、RT-Thread 和 LVGL。
- 提交 [`39bacab3978b56b281356a43e388e0951b9fc597`](https://github.com/Gangan-307/LCHSP_Watch/commit/39bacab3978b56b281356a43e388e0951b9fc597)，`2026-09-03T08:56:25Z`，约 `+1218/-21`、21 个文件，加入 PAN OTA loader、Flash 写入、分区与设置入口。
- 不选原因：目标 OTA 资源更新已有签名认证、依赖和完整构建门禁债务；来源未识别许可证，不能以另一个示例绕过发布者认证、回滚和包完整性设计。

### 4.5 `adnanPBI/lvgl-watch-navigation-demo`：五种手表导航语法

- 仓库：[adnanPBI/lvgl-watch-navigation-demo](https://github.com/adnanPBI/lvgl-watch-navigation-demo)，MIT。
- 本轮从概念图发展为五种导航状态机、LVGL renderer、模糊测试和 native simulator 验证；代表提交 [`884a22fde71ff5741cceade50da6ab21651ce9a6`](https://github.com/adnanPBI/lvgl-watch-navigation-demo/commit/884a22fde71ff5741cceade50da6ab21651ce9a6) 修改 `watch_navigation.c` 约 `+220/-8`。
- 不选原因：这是应用启动器和信息架构实验，不是宠物关系功能。目标当前缺口是多会话信息被隐藏，而不是缺少全局导航模型；引入新手势会加剧返回手势和宠物触摸冲突。

### 4.6 其他窗口更新

| 仓库 | 窗口证据 | 结论 |
| --- | --- | --- |
| `skyflyt/sensecap-ha-panel` | [`206c2461e300764cab728d2b5f6bf8513f707d6c`](https://github.com/skyflyt/sensecap-ha-panel/commit/206c2461e300764cab728d2b5f6bf8513f707d6c)，只增加 Powers 2.0 文档和图片 | 已于上一轮评估；操作权限和房间控制越过 Agent Pet v1 只读边界 |
| `SantoCovato/Stait-Watch` | 上传主文件后主要更新 README | 天气、通知、媒体、来电均依赖伴侣端且属既有能力；无许可证 |
| `newbie130/familybox` | [`33e0f5f3fab360ae332aa23dd76c933c94d281a4`](https://github.com/newbie130/familybox/commit/33e0f5f3fab360ae332aa23dd76c933c94d281a4) 仅 README | 照片/语音留言与语音明信片方向重复，非实质新增 |
| `Magiciakwf/6.LVGL-SmartWatch` | 单次导入约 171 万行，包含 STM32F4、第三方库、构建产物和 OTA 文件 | 无许可证、技术栈不匹配、提交包含大量生成物，不能作为可审查创意来源 |

## 5. 量化排序

评分为 1～5，越高越好；“工作量”和“风险”高分表示更轻量、更可控。

| 方向 | 原创性 | 用户价值 | 差异化 | 交互闭环 | 硬件适配 | 许可证 | 工作量 | 风险 | 合计/40 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Momo Agent 小队雷达 | 4 | 5 | 5 | 4 | 5 | 5 | 4 | 4 | **36** |
| IMU 倾斜沙粒玩具 | 3 | 3 | 3 | 4 | 3 | 1 | 2 | 2 | 21 |
| Buddy 联网/专注设置 | 2 | 3 | 2 | 3 | 2 | 1 | 2 | 2 | 17 |
| PAN OTA | 2 | 3 | 1 | 2 | 5 | 1 | 1 | 1 | 16 |
| 五种手表导航 | 3 | 2 | 2 | 2 | 3 | 5 | 2 | 3 | 22 |

小队雷达高分的关键不是复制大屏 dashboard，而是把目标协议已经传输、固定内存已经保存的多会话状态变得可见。其主要工程风险集中在 390 x 450 信息密度、提醒优先级和触摸所有权，均可通过只读、固定对象数和可关闭的 MVP 控制。

## 6. 决策

1. 本轮只推荐 **Momo Agent 小队雷达（多会话状态总览）**，进入子任务 2 的技术方案与受控 MVP 评估。
2. MVP 保留 Momo 为主视觉，只在底部状态区增加摘要与最多 5 个状态点；更多会话显示 `+N`，不做大屏列表复制。
3. 排序优先级为 `ERROR > NEEDS_INPUT > 最近完成 > RUNNING > IDLE`；设备断连时保留快照但明确显示离线。
4. 允许自动轮播或在底部独立命中区浏览只读详情；绝不把 `approval_pending` 变成设备端批准入口。
5. 必须提供编译期开关、纯 C 视图模型测试、功能开关 A/B 构建、模拟器检查和 390 x 450 真机验收；未完成真机项必须标注“待硬件验收”。
6. 本提交只包含扫描记录与 PRD；不开发、不推送、不创建 PR、不合并默认分支。
