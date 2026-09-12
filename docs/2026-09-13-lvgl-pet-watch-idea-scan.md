# 2026-09-13 LVGL 电子宠物/手表创意增量扫描

## 1. 本轮结论

- 增量窗口：`2026-09-10T16:00:36.234Z`（不含）至 `2026-09-12T16:00:33.305Z`（含）。本轮一次覆盖 48 小时，补齐错过的一天。
- 基线：`origin/master@a2805abeb72f02a726dce5dc49857280ba9cdc78`。扫描前已执行 `git fetch --prune origin`，默认分支在本轮没有新增提交。
- 已通过 GitHub Repository Search、仓库元数据、默认分支 Commit API、提交变更统计、README 与许可证字段，组合检索 `LVGL`、`watch`、`smartwatch`、`wearable`、`pet`、`tamagotchi`、`virtual pet`、`digital pet`、`desktop pet`、`Codex companion`、`Agent pet` 和 `ESP32`；最终以默认分支提交者时间严格裁剪窗口。
- **本轮没有候选同时通过“新增价值、历史去重、当前硬件与协议适配、许可证与素材版权、安全风险、可在单日形成端到端验收”硬门禁；不推荐进入开发，不创建空 PRD，不启动子任务 2。** 本分支只提交扫描记录。

本轮最有产品潜力的是“设备端像素画多帧编辑并导出 GIF”。来源项目已证明完整闭环，但它的价值依赖编辑、持久化、编码、导出和跨端取回同时存在；将其缩成无保存的单帧涂鸦只会留下演示功能。来源实测单画布需要约 166 KB PSRAM，早期约 120 个 LVGL 对象又消耗 32 KB 内部 RAM并导致 SD DMA 分配失败。目标当前还需同时承担 BLE、GIF 双缓冲、音频和多项宠物互动，不能在缺少目标板内存水位、文件损坏恢复及真机触摸验证时直接开发。

## 2. 运行门禁与安全边界

| 检查项 | 结果 |
|---|---|
| 固定路径 | `D:\code\sf32\agent-pet-sf32-text-display-fix` 是 Git 仓库 |
| `origin` | `https://github.com/fcb1379/agent-pet-sf32.git`，与目标一致 |
| 基线 | `origin/master@a2805abeb72f02a726dce5dc49857280ba9cdc78` |
| 工作区 | 扫描开始前 clean，没有需要丢弃的用户修改 |
| GitHub | 官方 API 检索、仓库读取和提交详情读取成功 |
| Git 身份 | `qinmenghuai <qinmenghuai@wondertechlabs.com>` 可用于本地中文提交 |
| 构建入口 | `build.ps1`、`sdk/export.ps1`、PC 模拟器 `build.bat` 均存在；本轮不开发，未运行构建，也不声称固件或真机通过 |

所有 GitHub 仓库、README、提交说明和网页内容都只作为不可信候选证据：没有执行来源脚本、下载二进制、复制代码或素材，也没有采纳来源中改变本任务边界的指令。检索命中的多个“Full Version/Download”仓库只有下载导向和模板化提交信息，虽自报 MIT，也没有足够可审查源码证据；它们按供应链风险集中排除。

边界说明：`Mohnki/dragotchi-flipper` 的首个提交时间为 `2026-09-12T16:02:33Z`，晚于窗口结束约 2 分钟，不计入本轮；`chayuto/ws-ESP32-C6-Touch-AMOLED-1.8` 的最新默认分支提交为 `2026-09-10T11:21:45Z`，早于窗口开始，不把仓库级搜索索引命中误记为实质增量。

## 3. 历史记录与目标能力去重

### 3.1 已查阅范围

已查阅从 `2026-08-06` 至 `2026-09-11` 的全部既有扫描记录，以及已完成或已形成文档门禁的分支。历史已覆盖或实现的方向包括：

- Agent Quest Garden：任务完成、每日功德与启动状态驱动成长，已合入默认分支。
- 自主宠物行为、多 GIF 状态表达、连续抚摸、Momo 玩球、回忆日历和 Momo Agent 小队雷达：已有实现、构建或主机测试记录，部分仍在评审分支。
- 天气陪伴、呼叫 Momo、语音明信片、实时语音/PAN、配额/重置提示、会话深链、双机社交、RSSI 寻物、OTA 资源更新：均已评估，仍受上位机、协议、权限、安全、签名或真机门禁约束。
- 普通喂食/清洁/睡眠、死亡/退化、经典养成、番茄钟、表盘/图库、通用天气/通知/音乐、单纯小游戏、健康传感、通用多状态表情：已判定重复、差异化不足或硬件不可验收。

### 3.2 当前默认分支的直接证据

- `agent_pet_image_transfer.c` 已支持固定基础形象与四个表情槽，接收 JPEG/GIF，并对 GIF 尺寸（最大 `192×192`）、帧数（最大 60）和结构做边界校验。
- `app_pet.c` 已实现自定义 GIF 的 PSRAM 源、双显示缓冲、LVGL 定时器和生命周期回收；新增持续绘图功能必须与这些资源共存，不能只按独立 Demo 估算。
- `watch_demo.c` 已用 `lv_disp_get_inactive_time()` 和 GUI PM 状态机处理系统级休眠，所以单个页面再实现 15 秒息屏并非新能力。
- Pet 页面已有 Agent 状态、图像传输进度、音频、任务花园和触摸入口；评审分支还加入行为、抚摸、玩球、日历与会话雷达，任何新触摸或视觉层都必须经过统一所有权与抢占仲裁。

## 4. 严格窗口候选证据与评估

### 4.1 `charliejgallo/ESP32S3_AmoledOS`：设备端 Pixel Art、多帧动画与 GIF/PNG 导出

- 来源：[仓库](https://github.com/charliejgallo/ESP32S3_AmoledOS)，MIT；窗口核心提交 [`8960b0e3a8491d6aba5d3ae2d92da9bd668f1a7d`](https://github.com/charliejgallo/ESP32S3_AmoledOS/commit/8960b0e3a8491d6aba5d3ae2d92da9bd668f1a7d)，`2026-09-11T05:11:20Z`，约 `+3443/-10`、19 个文件。
- 原创性与闭环：8 个画布，8×8/16×16 网格，最多 16 帧；提供铅笔、填充、取色、调色板、逐帧复制/删除、播放、撤销、自动保存、GIF/PNG 导出和网页端编辑回写。它不是简单图片查看器，输入、创作、预览、保存和取回闭环完整。
- 用户价值与差异化：用户自行制作宠物徽章或表情动画，能降低每次更换素材都依赖开发者/上位机预制包的成本；与目标已有“接收并播放多 GIF”互补，而非完全重复。
- 硬件与资源：来源用 288×288 RGB565 画布，单画布约 166 KB PSRAM；早期约 120 个 LVGL 对象曾消耗 32 KB 内部 RAM，使 SD 卡 DMA 缓冲分配失败。目标 240×240、SF32/RT-Thread、现有 GIF 双缓冲和 BLE/音频与来源 ESP32-S3/ESP-IDF/动态应用架构不同。来源导出器自身约 17 KB `.text`，仍不能替代目标板链接和运行水位测量。
- 许可证/素材：代码为 MIT，但示例小猫、图片和动画属于内容资产；本项目即便后续实现也必须使用自有图形与独立交互设计，不复制示例素材、像素表或来源阈值。
- 工作量与风险：若保持完整价值，至少需要固定容量画布模型、原子文件格式、写入失败退避、损坏识别、GIF 编码、SD/Flash 磨损策略、触摸热区、页面生命周期、远端 PLAY/GIF 传输仲裁及真机内存/帧率测试，属于 L～XL，不能在单日固件闭环。
- 结论：**本轮 No-Go，保留为最高价值观察项。** 后续只有在先确定“自有素材、最大 16×16×8 帧、固定静态内存、可恢复原子保存、导出/取回路径”和目标板内部 RAM 最低水位后，才值得另立跨端需求；不做无保存的空壳涂鸦。

同仓库窗口内的触摸边缘校准提交 [`5777a01b3646291e520eca699a161cb4a33111a7`](https://github.com/charliejgallo/ESP32S3_AmoledOS/commit/5777a01b3646291e520eca699a161cb4a33111a7) 约 `+77/-53`，修正原始触摸到边框的映射。它说明边缘热区需以真实面板落点审计，但校准值和控制器特性是板级数据，不能从 Waveshare ESP32-S3 复制到 SF32；应归入真机触摸可靠性测试，不立宠物功能。

### 4.2 `ncr/omarchy-themesync`：签名 BLE 广播双向主题同步

- 来源：[仓库](https://github.com/ncr/omarchy-themesync)，MIT；窗口提交 [`253127ea5f96d3ba9b601cc1d871d95343416a85`](https://github.com/ncr/omarchy-themesync/commit/253127ea5f96d3ba9b601cc1d871d95343416a85)，`2026-09-12T15:22:30Z`，新增 201 行小设备二进制主题提案。
- 原创性与闭环：现有实现可在桌面主题变化后通过带 HMAC 和计数器的 BLE 广播同步手表，手表也能发请求反向切换桌面；包含配对、重放防护、重试、回执和主题列表提交，机制成熟。
- 用户价值：可将用户电脑环境与宠物背景色/装饰风格联动，形成“陪伴环境同色”的弱情感价值。
- 适配与风险：依赖 Linux/Omarchy/BlueZ、BLE 5 扩展广播、持续主动扫描、配对密钥、计数器持久化和新的 GATT/广播协议。目标 BLE 当前承担 Agent/手机链路，广播扫描共存、连接稳定性、功耗、随机地址和密钥恢复均未验证；来源也明确主题名会公开广播，存在可观察的隐私泄漏。
- 结论：No-Go。固件单边无法产生或消费真实桌面主题；在上位机、协议版本、安全评审和射频共存之前不创建功能。仅将“签名、计数器、回执、重放防护”记为未来跨端控制的安全原则。

### 4.3 `saucegeo/tamagotchi`：倾斜观星与小游戏可靠性修复

- 来源：[仓库](https://github.com/saucegeo/tamagotchi)，MIT；倾斜观星提交 [`e5443f17f4ac8bc6fd61e3b7c5b733cfed17e20c`](https://github.com/saucegeo/tamagotchi/commit/e5443f17f4ac8bc6fd61e3b7c5b733cfed17e20c)，`2026-09-11T15:42:19Z`，约 `+20/-12`；同窗还修复幸运饼干重启、蜡烛麦克风卡死和电量显示。
- 价值与适配：拿起/倾斜设备进入观星是可理解的姿态互动，但目标已有 IMU 触发、自主行为、多 GIF、玩球和天气陪伴方向。来源本轮只是方向判断和可靠性修补，没有形成区别于现有状态切换的长期闭环。
- 结论：No-Go。观星如要成立还需自有星空素材、姿态防抖、误触恢复、与远端状态的优先级及夜间场景；仅复制一个倾斜阈值既无差异化也不可跨硬件复用。小游戏崩溃修复属于来源可靠性，不是目标新功能。

### 4.4 `PietroMezzaroba/crosspet-x3`：阅读统计驱动宠物状态

- 来源：[仓库](https://github.com/PietroMezzaroba/crosspet-x3)，MIT；提交 [`371cec528558fe16f9e8a60a02dae3b61133f1a1`](https://github.com/PietroMezzaroba/crosspet-x3/commit/371cec528558fe16f9e8a60a02dae3b61133f1a1)，`2026-09-12T13:28:41Z`，把跨格式阅读统计自动同步到 Tamagotchi，并在启动/休眠保存状态。
- 用户价值：把真实阅读行为转为宠物成长，比手动喂养更有意义；来源对电子阅读器有完整数据入口。
- 适配与去重：目标不是阅读器，也没有可信阅读时长/页数源；若改成 Agent 工作完成，则已由任务花园和小队雷达覆盖。新增手机阅读统计又需要上位机权限、数据最小化、日切、去重和协议字段，不能由固件单边验收。
- 结论：No-Go。保留“真实活动驱动成长优于手工喂养”的产品原则，不新增重复成长系统。

### 4.5 Agent/额度类桌面伴侣连续更新

#### `yhyh0000/dsh-esp32-dial`

- 来源：[仓库](https://github.com/yhyh0000/dsh-esp32-dial)，未识别许可证；窗口提交 [`9ae2227749bc01364f89f63e0b5cef2bd15e3d3e`](https://github.com/yhyh0000/dsh-esp32-dial/commit/9ae2227749bc01364f89f63e0b5cef2bd15e3d3e) 增加默认用量/重置端点，另有圆屏安全区、配网页与 NVS key 修复。
- 不进入开发：目标小队雷达已经覆盖会话状态；真实额度仍缺可信上位机字段和供应商语义。来源未声明许可证，端点与 NVS 参数不能复制；把 API 密钥或供应商凭据放到设备也不符合现有安全边界。

#### `Ljhhhhhh/codex-passport`

- 来源：[仓库](https://github.com/Ljhhhhhh/codex-passport)，未识别许可证；窗口提交 [`86ce134f141750bf2f7b87f88870843def881569`](https://github.com/Ljhhhhhh/codex-passport/commit/86ce134f141750bf2f7b87f88870843def881569)，`2026-09-12T01:26:10Z`，增加 15 秒无操作/无消息更新自动息屏。
- 不进入开发：目标已在系统层用 LVGL inactive time 和 GUI PM 休眠，页面级重复计时会造成双计时器、误唤醒和状态分叉。更合适的是后续验证现有全局休眠在 GIF、BLE 传输和本地互动期间的策略，而不是新增功能。

#### `tobymarks/esp32-ai-monitor`

- 来源：[仓库](https://github.com/tobymarks/esp32-ai-monitor)，MIT；窗口主要加入 Windows Companion、串口互斥、刷写、发布与多 Provider 展示。
- 不进入开发：多 Provider 额度与前几轮观察项重复；Windows/Tauri 安装、串口刷写和 Provider 采集是独立上位机产品，不能由 SF32 固件单日完成。串口独占和签名发布可作为上位机可靠性参考，不构成宠物功能。

### 4.6 通用手表与平台更新

| 候选 | 窗口证据 | 评估与结论 |
|---|---|---|
| [xISPx/ESP32-S3-Touch-AMOLED-2.06](https://github.com/xISPx/ESP32-S3-Touch-AMOLED-2.06) | [`21b89350bfe60cdd2cf6e1345d7d2474bc051e8a`](https://github.com/xISPx/ESP32-S3-Touch-AMOLED-2.06/commit/21b89350bfe60cdd2cf6e1345d7d2474bc051e8a)，`2026-09-12T08:11:23Z`，初次公开；随后补 MIT、构建工具固定与英文文档 | 通用 ESP32-S3/LVGL 手表发布，暂无区别于目标时钟、应用菜单、触摸和状态页的宠物闭环；芯片、屏幕和 Arduino/PlatformIO 栈不同，不整体移植 |
| [hleserg/Attadipa](https://github.com/hleserg/Attadipa) | GPL-3.0；窗口内导航方向模式、亮度持久化、认证 Flash 恢复、IMU/Mesh 诊断等提交 | 工程质量参考价值高，但属平台可靠性；GNSS/Mesh/磁力计等 BOM 不匹配，GPL 与目标集成需单独法务判断，不作为本轮功能来源 |
| [ciccirix/LILYGO-T-WATCH-ULTRA-X-CALMATI](https://github.com/ciccirix/LILYGO-T-WATCH-ULTRA-X-CALMATI) | MIT；窗口内离线唤醒词、天气点击刷新和随后撤掉唤醒 UI 的提交 | 离线唤醒依赖 ESP-SR 模型和 ESP32-S3 音频栈；天气方向已有跨端门禁；同仓库包含进攻性无线工具，不能执行或整体引入。功能在窗口内反复增删，产品语义也不稳定 |
| [Haraldon9847/waveshare-watch-rs](https://github.com/Haraldon9847/waveshare-watch-rs) | [`b640c105dbad7a8d2db6d6b5dbd1b2e97de9d314`](https://github.com/Haraldon9847/waveshare-watch-rs/commit/b640c105dbad7a8d2db6d6b5dbd1b2e97de9d314)，仅 README | 无功能增量；Rust/no_std 且明确替代 LVGL，技术栈不兼容 |
| `ZSWatch/ZSWatch`、`ElenixOS/ElenixOS` | 仓库级搜索命中，但默认分支 Commit API 在严格窗口无提交 | 不把搜索索引或非默认分支活动冒充已发布功能增量 |

### 4.7 其他宠物/陪伴候选

| 候选 | 证据与许可证 | 评估与结论 |
|---|---|---|
| [MarcoAmmirati/arm-tamagotchi](https://github.com/MarcoAmmirati/arm-tamagotchi) | MIT；窗口初始提交 [`116f902c2a5f7a6bc32c3fd75f1ad4b958311ee9`](https://github.com/MarcoAmmirati/arm-tamagotchi/commit/116f902c2a5f7a6bc32c3fd75f1ad4b958311ee9)，LPC1768/Keil/GLCD | 触摸、摇杆移动、音效、音量和疏忽动画是经典课程式 Tamagotchi，目标已有更丰富互动；硬件和图形栈不兼容，不复制素材 |
| [adrenalinegpj/Maroon](https://github.com/adrenalinegpj/Maroon) | 未识别许可证；[`12a274af3eebd6c920705f000996d40463a07511`](https://github.com/adrenalinegpj/Maroon/commit/12a274af3eebd6c920705f000996d40463a07511)，ESP32+128×64 OLED+三键+DFPlayer | 表情、番茄钟、Snake、反应游戏、音乐和情绪状态均与现有能力或已排除方向重复；无许可证且显示/输入/音频硬件不同 |
| [mzhnk/ai-pet-robot](https://github.com/mzhnk/ai-pet-robot) | 未识别许可证；窗口仅 2 行初始 README 后扩写，描述相机、超声、IMU、电机、Wi-Fi/Flask 和 13 状态 FSM | “断网仍有本地人格、安全反射高于 AI”是正确原则，但目标缺相机、超声、电机和 Wi-Fi 闭环；硬件和上位机工作量远超本轮，描述不能替代可复用实现证据 |
| [nithyaganesh77/deskbuddy-esp32s3](https://github.com/nithyaganesh77/deskbuddy-esp32s3) | 未识别许可证；窗口初次提交，ESP32-S3 Zero+SSD1306，天气/世界时钟 | 天气陪伴和表情已评估，仍缺上位机天气授权与同步；来源自述为移植项目，素材与原创权属不足 |
| [FocusPaws](https://github.com/parsa0navid/FocusPaws)、[Producktivity](https://github.com/DanielAPerez20/Producktivity) | 均未识别许可证；窗口新建，分别是全栈番茄宠物与完成任务赚钱养宠 | 与任务花园、低压力陪伴和既有番茄方向重复；不是 LVGL/嵌入式实现，商店/货币会扩大状态和内容维护 |
| `larden29/Tamagotchi-Full-Version` 等下载型仓库 | 批量模板化提交、下载导向描述，虽元数据自报 MIT，但缺少足够可审查源码证据 | 按不可信供应链命中排除；不下载、不运行、不作为产品事实或素材来源 |

## 5. 量化排序

评分 1～5，适配分越高越容易在当前 SF32/RT-Thread/LVGL 架构落地；风险分越高风险越大。

| 方向 | 原创性 | 用户价值 | 差异化 | 交互闭环 | 硬件适配 | 许可/素材清晰 | 工作量 | 风险 | 决策 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| 设备端 Pixel Art 与多帧导出 | 5 | 4 | 5 | 5 | 2 | 4 | 5 | 5 | 最高价值观察项；资源与单日门禁不通过 |
| 签名 BLE 双向主题同步 | 4 | 3 | 4 | 5 | 1 | 5 | 5 | 5 | 需上位机/协议/射频/隐私专项 |
| 阅读统计驱动宠物成长 | 4 | 4 | 3 | 4 | 1 | 5 | 5 | 4 | 无目标数据源，且成长表达重复 |
| 倾斜观星 | 3 | 3 | 2 | 3 | 3 | 5 | 3 | 3 | 与 IMU/状态互动重复，素材与真机门禁未过 |
| 15 秒自动息屏 | 1 | 3 | 1 | 4 | 5 | 2 | 1 | 2 | 系统已有同类能力，不立项 |
| 机器人安全反射/人物跟随 | 4 | 4 | 5 | 4 | 1 | 1 | 5 | 5 | BOM 与架构不匹配 |

## 6. 决策与后续触发条件

本轮只保存扫描记录。没有候选进入产品开发，因此不创建功能分支以外的实现提交、不写空 PRD、不运行编译、不启动嵌入式子任务。

后续可重新评估设备端 Pixel Art，但必须同时满足：

1. 产品范围保留“创作→预览→原子保存→导出/取回”完整闭环，并明确所有示例素材由项目自制；
2. 以固定静态容量实现，给出内部 RAM、PSRAM、Flash/SD、对象数、写放大和最坏 GIF 大小预算；
3. 在功能 ON/OFF 构建、目标板低内存压力、SD 拔出/写失败/损坏文件、60 分钟编辑与播放、页面退出重入中通过验证；
4. 与 BLE 图像传输、自定义 GIF 解码、远端 `PLAY`、行为状态、抚摸、玩球、日历和小队雷达建立唯一触摸与视觉所有权；
5. 若需要网页或手机取回文件，先定义经过能力协商、长度/哈希/事务提交、取消/超时和路径白名单约束的上位机协议。

在这些前置条件没有成立前，直接开发会把一个有价值的创作闭环降级为资源风险高、无法持久使用的 Demo，不符合每日流程的质量门槛。
