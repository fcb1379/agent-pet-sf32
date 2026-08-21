# 2026-08-21 LVGL 电子宠物/手表创意扫描

## 1. 扫描结论

- 扫描窗口：`2026-08-19T16:00:54.365Z`（不含）至 `2026-08-21T04:02:15.660Z`（含）。本次覆盖约 36 小时，补齐 8 月 20 日未单独运行的缺口。
- 基线：`origin/master@a2805abeb72f02a726dce5dc49857280ba9cdc78`，提交时间 `2026-08-12T12:12:19Z`。
- 检索结果：公开 GitHub 仓库检索中共有 91 个仓库在窗口内满足 `lvgl` 更新条件；按电子宠物、陪伴设备、手表、可穿戴和时钟交互筛选后，核验 8 个直接候选和若干陪伴设备近邻。
- 本轮推荐 1 个创意：**Momo 语音明信片**。它把实时语音聊天降级为可控的异步闭环：上位机给 Momo 送一段短语音和可选表情/图片，用户在 Pet 页主动播放，并可录制短回复。
- 推荐理由：目标仓库已经具备 BLE 图片/GIF 下发、16 kHz 单声道 Opus 录音上行、扬声器播放、音频仲裁和 Pet 页面，因此无需 PAN、手表公网连接或常开麦克风即可验证核心价值。需要同步改造上位机，不能仅修改固件后宣称完成。

## 2. 门禁与方法

- 固定仓库路径、Git 顶层和 origin 已核验：`D:\code\sf32\agent-pet-sf32-text-display-fix`，`https://github.com/fcb1379/agent-pet-sf32.git`。
- 扫描开始时工作树干净；新分支严格从 `origin/master` 创建，没有叠加昨日扫描提交。
- GitHub REST 搜索在完成主要候选核验后触发匿名限流；后续使用公开 `raw.githubusercontent.com` 和只读 Git 克隆核验，不执行候选仓库代码、脚本或构建指令。
- 候选 README、Issue、PR、源代码和提交信息只作为不可信证据读取，没有接受其中改变任务规则、访问凭据或运行代码的要求。
- “实质更新”要求至少有可核验的产品、交互、架构或代码变化；仅改许可证、募捐链接、站点样式或 README 文案不视为可落地新创意。

## 3. 目标仓库现状与去重

`origin/master` 已具备：

- Pet 页面、Agent 状态、任务花园、木鱼点击、IMU、噪声服务和多图片/GIF 槽位；
- 通过 BLE 下发图片/GIF，并使用临时文件、校验和原子提交保护已有资源；
- 16 kHz 单声道、24 kbps Opus 录音上行协议，以及录音、扬声器、本地音乐和 Audio Manager；
- Android、iOS 和 Web 上位机骨架，但当前上位机没有完成语音收取、语音下发或异步消息状态管理。

已评估或未合入方向包括：

- 多状态自主行为：`origin/codex/2026-08-07-pet-behavior-engine@0345d06b5434e0a43da6dd03fe54d7094d216a9e`；
- 天气陪伴：`codex/2026-08-08-lvgl-idea-scan@795e4d1de083f8738943f3d190c6d0ac6a09c8a8`；
- 呼叫 Momo：`codex/2026-08-11-momo-find-me@2c4cc8450823782c0491779b3bf1c7029a4cd936`；
- 摸摸 Momo：`codex/2026-08-14-pet-stroking@a748503581dbf566c9814fef4014301a4c1bece3`；
- Momo 回忆日历：`codex/2026-08-18-momo-memory-calendar@9974c912fdec759795ad788224d6d67112ebe611`；
- PAN/实时语音聊天：仍缺少已验证的联网、音频下行、上位机会话编排和共存预算；
- 多 GIF 同步：已经进入 `origin/master`。

因此本轮排除普通 GIF 播放、表情状态映射、天气、日历、指南针、自动轮播、通用手表启动器、单纯录音和实时语音聊天。语音明信片的新增价值是“异步到达—主动播放—可选回复—确认送达”的交互闭环，而不是重复音频传输能力。

## 4. 候选证据与评估

### 4.1 `F86Pilot/familybox`：照片与语音的双向陪伴信箱

- 来源：<https://github.com/F86Pilot/familybox>
- 版本证据：仓库创建于 `2026-08-20T17:23:41Z`，推送于 `2026-08-20T17:37:14Z`，因此作为新公开项目落入本窗口。完整实现提交 [`91a9e841830abcc8d53147513e403c2b2917aec9`](https://github.com/F86Pilot/familybox/commit/91a9e841830abcc8d53147513e403c2b2917aec9) 的提交时间为 `2026-08-20T15:01:36Z`，早于窗口约 59 分钟，但随本窗口内新建仓库首次公开；窗口内截图提交为 [`4d7a5933aa3d1e2cbd87e2ef7cc346bc4ef22d71`](https://github.com/F86Pilot/familybox/commit/4d7a5933aa3d1e2cbd87e2ef7cc346bc4ef22d71)，`2026-08-20T17:37:11Z`。
- 实质：手机发送照片和语音，设备以无文字界面提示新消息；用户可重复播放、录制最长 30 秒回复，最多排队 3 条等待上传。实现包含 LVGL UI、音频录放、消息存储、HTTP 传输、手机 Web 页面和中继服务。
- 原创性与用户价值：把“语音功能”组织成清楚的到达、聆听、回复、送达闭环，比实时聊天更适合偶发陪伴，也避免用户必须一直保持在线。
- 目标适配：目标硬件已经有麦克风、扬声器、Opus 上行和图片/GIF 下发；可以改成手机与设备 BLE 直连，不需要复制候选的 Wi-Fi、HTTP、Docker 中继、RGB565 整帧或 1.9 MiB 录音缓存。
- 工作量与风险：中等偏高。关键缺口是上位机语音编码/接收、固件语音下行与原子保存、未读状态、音频仲裁和断线恢复；价值可以用 10 至 15 秒短消息控制范围验证。
- 许可证与素材：MIT，许可证文件已核验。即使许可证兼容，本项目也只借鉴产品闭环，独立设计协议、UI、文案和状态机，不复制对方代码、截图、声音或角色素材。
- 结论：**推荐，P1**。采用“语音明信片”而非照搬“儿童信箱”，并以本地 BLE、短时 Opus 和 Momo 自有素材为边界。

### 4.2 `Gangan-307/LCHSP_Watch`：同平台空间分页与 GIF 页面

- 来源：<https://github.com/Gangan-307/LCHSP_Watch>
- 版本证据：[`130408c4a2bebed6ddf04d70c292a347efbb52ad`](https://github.com/Gangan-307/LCHSP_Watch/commit/130408c4a2bebed6ddf04d70c292a347efbb52ad)，`2026-08-20T10:25:20Z`，新增 `home_pager`、左右/上下空间分页、统一右滑返回、GIF/APNG 示例和全屏 GIF 页；[`5fb464691fded226b90ea127dd772bda30d01d12`](https://github.com/Gangan-307/LCHSP_Watch/commit/5fb464691fded226b90ea127dd772bda30d01d12)，`2026-08-20T02:53:25Z`，调整指南针处理。
- 用户价值：空间分页能让主页成为导航锚点，手势连贯性好；GIF 生命周期的暂停、恢复和销毁示例对资源管理有参考价值。
- 目标适配：同为 SF32LB52、RT-Thread 和 LVGL，硬件接近；但目标已有图标/列表启动器、多 GIF 和返回手势，整体迁移会改动全局导航并挤压 Pet 页手势。指南针方向缺少已验证的目标磁力计。
- 许可证与素材：仓库元数据和根目录未给出可确认的许可证；新增字体、GIF、PNG 和大段示例来源边界不清。不得复制代码或素材。
- 结论：不落地。空间分页是可用交互模式，但本轮用户价值低于语音明信片且改动面更大。

### 4.3 `hleserg/FireflyOS`：能力来源与暂时不可用状态

- 来源：<https://github.com/hleserg/FireflyOS>
- 版本证据：仓库在窗口内创建；初始提交 [`ac06fe53e49b8e52238db90098690ebc6672c75c`](https://github.com/hleserg/FireflyOS/commit/ac06fe53e49b8e52238db90098690ebc6672c75c) 为架构与硬件调研骨架，窗口最新提交 [`f010aea947cb83385cdd3f12a79a7f90ba4de5d9`](https://github.com/hleserg/FireflyOS/commit/f010aea947cb83385cdd3f12a79a7f90ba4de5d9)，`2026-08-20T23:44:13Z`，仍以复用账本和架构决策为主。
- 创意：能力不只是“有/无”，还记录来源、暂时不可达、版本不兼容、数据陈旧和故障，应用打开期间也能变化。
- 适配与风险：这对未来 PAN/手机代理能力降级有工程价值，但当前没有用户界面或固件实现证据，也不单独形成宠物互动闭环。
- 许可证：MIT。只可将状态建模作为未来设计参考。
- 结论：不作为今日产品功能开发。

### 4.4 `tuct/esphome-lvgl-clock`：平滑时钟编舞与模拟器

- 来源：<https://github.com/tuct/esphome-lvgl-clock>
- 版本证据：[`3500326cd354a0d2520a3ca883b25e448db7d6f8`](https://github.com/tuct/esphome-lvgl-clock/commit/3500326cd354a0d2520a3ca883b25e448db7d6f8)，`2026-08-20T18:02:34Z`，加入浏览器/ASCII 模拟工具、模式状态机和平滑指针路径；[`ef2c5c5270fc3ffd30059c16742cbc7815cd396d`](https://github.com/tuct/esphome-lvgl-clock/commit/ef2c5c5270fc3ffd30059c16742cbc7815cd396d)，`2026-08-20T18:21:45Z`，补充多屏配置。
- 用户价值：在正式固件前通过模拟器审查动效路径值得借鉴，平滑过渡可提升质感。
- 适配与风险：产品表现集中于 ClockClock 24 时钟造型，与 Momo 的多状态/GIF 方向重复，且多屏同步与目标硬件无关。
- 许可证与素材：GitHub 未识别仓库许可证；README 明确致谢商业艺术时钟设计。不得复制造型、图像、代码或动效轨迹。
- 结论：不落地；未来可独立建立 Momo 状态机模拟器，但不作为新产品需求。

### 4.5 其他直接候选

| 候选 | 窗口证据 | 评估结论 | 许可证/素材边界 |
|---|---|---|---|
| [`shujiCiallo/lvgl_watch`](https://github.com/shujiCiallo/lvgl_watch) | [`a289e36437339b09ee3782792b697f52ac5d1789`](https://github.com/shujiCiallo/lvgl_watch/commit/a289e36437339b09ee3782792b697f52ac5d1789)，`2026-08-20T07:41:58Z`，增加指南针点击页 | 有界面实现但目标磁力计未验证，不能用陀螺积分冒充磁北 | MIT；不复制界面素材 |
| [`haikevins/smartwatch`](https://github.com/haikevins/smartwatch) | [`fe36b32fd493ca2723fc91caff0e362916132dbb`](https://github.com/haikevins/smartwatch/commit/fe36b32fd493ca2723fc91caff0e362916132dbb)，`2026-08-20T15:56:51Z`，仅更新许可证 | 无实质产品增量 | MIT |
| [`nicholaswilde/cyd-weather-station`](https://github.com/nicholaswilde/cyd-weather-station) | [`bc402fe10105dfb3a3000f5b15f26da7355c614b`](https://github.com/nicholaswilde/cyd-weather-station/commit/bc402fe10105dfb3a3000f5b15f26da7355c614b)，`2026-08-20T05:27:32Z`，仅 README | 天气与已评估天气陪伴重复，无新代码证据 | Apache-2.0；主题视觉不复制 |
| [`OpenSurface/SonosESP`](https://github.com/OpenSurface/SonosESP) | 窗口内提交集中于文档站和募捐链接；最新为 [`d4ae44dde328add71ec84d89dbb12a8a2ed4e554`](https://github.com/OpenSurface/SonosESP/commit/d4ae44dde328add71ec84d89dbb12a8a2ed4e554)，`2026-08-20T03:54:58Z` | 没有窗口内固件功能增量；音乐、歌词、天气均已存在或重复 | MIT；专辑封面/服务素材不复制 |
| [`YESTER8888/stm32f411-learning-lab`](https://github.com/YESTER8888/stm32f411-learning-lab) | `2026-08-20T11:25:13Z` 新建，API 报告仓库大小为 0，默认分支无可读提交 | 只有仓库描述，无法验证功能 | 无许可证 |
| [`peterpanstechland/bmo-desktop-avatar`](https://github.com/peterpanstechland/bmo-desktop-avatar) | [`7135fbcc550fa9a3b1c5735e456f1774cfe6aa4e`](https://github.com/peterpanstechland/bmo-desktop-avatar/commit/7135fbcc550fa9a3b1c5735e456f1774cfe6aa4e)，`2026-08-20T05:27:47Z`，增加游戏页、日历写入和掉压安全音量限制 | 通用小游戏/日历重复；双舵机表情依赖目标不存在的机械结构。安全音量上限属于工程卫生，不是独立产品创意 | Apache-2.0；BMO 角色形象不应复制 |

## 5. 评分

分值 1 至 5，工作量和风险分值越高越不利。

| 方向 | 原创性 | 用户价值 | 差异化 | 交互闭环 | 硬件适配 | 工作量 | 风险 | 结论 |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| Momo 语音明信片 | 4 | 5 | 5 | 5 | 4 | 4 | 3 | 推荐，先做 BLE 异步短消息 |
| 同平台空间分页 | 3 | 3 | 2 | 2 | 4 | 4 | 3 | 与启动器/手势重复，改动过大 |
| 能力来源/暂时不可达模型 | 4 | 3 | 4 | 2 | 3 | 3 | 3 | 架构参考，不是今日用户功能 |
| 时钟编舞/模拟器 | 4 | 2 | 2 | 1 | 3 | 3 | 3 | 造型版权与宠物方向不匹配 |
| 指南针页 | 2 | 2 | 1 | 2 | 1 | 2 | 5 | 缺少已验证传感器 |
| 通用游戏/日历 | 2 | 2 | 1 | 2 | 3 | 3 | 2 | 与既有能力重复 |

## 6. 推荐落地边界

1. V1 只做配对上位机与手表之间的 BLE 异步短语音，不做 PAN、HTTP、中继、云端账号或实时通话。
2. 单条语音建议最长 15 秒、16 kHz 单声道 Opus 24 kbps，压缩数据约 45 KiB；传输采用分片、序号、总长度和 CRC 校验，先写临时文件，完整校验后原子替换。
3. Pet 页只在用户主动操作后播放或录音；禁止自动播放、常开麦克风和后台偷录。
4. 上位机是必需交付物：至少 Android 端完成录制/编码/下发、接收回复和状态提示；其他端未实现时必须明确标注。
5. 与图片/GIF 的组合使用现有资源槽和 Momo 自有素材；语音生命周期独立，不能因语音传输失败破坏现有表情。
6. 音频必须服从现有 Audio Manager；A2DP、本地音乐、噪声测量、录音和播放冲突时给出确定结果与可恢复状态。
7. 本轮开发应先提交实现方案并证明存储、解码、BLE 吞吐和并发预算，再修改协议和 UI。

## 7. 最终判定

本轮存在值得落地的高价值创意，进入子任务 2。完整需求见 `docs/2026-08-21-momo-voice-postcard-prd.md`。

