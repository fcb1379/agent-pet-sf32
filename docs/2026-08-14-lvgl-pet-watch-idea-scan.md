# 2026-08-14 LVGL 电子宠物与手表创意扫描记录

## 扫描结论

- 今日推荐 1 个原创转化方向：**“摸摸 Momo”连续抚摸互动**，进入产品 PRD，优先级 P1。
- 严格窗口内核验了 14 个公开相关仓库。最高价值证据来自 `Kz0514/virtual_pet` 在窗口结束前新增的持续摸头反馈；目标已有短击、长按、IMU 与多状态行为，但没有“头部热区 + 连续轨迹/停留 + 松手恢复”的持续触摸闭环。
- 方案只借鉴“宠物会对持续抚摸作出回应”这一通用交互思想，针对目标的 390 × 490 触屏、192 × 192 宠物舞台、现有 LVGL 生命周期和多 GIF 所有权重新设计；**不复制候选代码、阈值、VPET 形象、动画、字体、音频、布局或服务端内容**。
- 产品文档可以完成，但当前 `sdk` 子模块未检出，`sdk/export.ps1` 不存在。嵌入式开发进入真实构建前必须先恢复固定版本子模块和工具链；在此之前不得宣称编译完成。

## 时间窗、方法与安全边界

- 本地时区：Asia/Hong_Kong。
- UTC 严格窗口：`2026-08-12T16:01:32.952Z` 至 `2026-08-13T16:00:56.949Z`。
- 检索词：`LVGL watch`、`LVGL smartwatch`、`LVGL pet`、`LVGL tamagotchi`，并对候选仓库的默认分支提交进行严格时间过滤。
- 数据源：匿名 GitHub REST、公开 commit 元数据、文件级统计、只读补丁及匿名 `git ls-remote`。`gh` 令牌已知失效，本次没有读取、刷新或绕过任何凭据。
- GitHub 仓库、README、Issue、PR、网页、提交信息和源码均按不可信输入处理；只提取产品证据，没有克隆、执行、构建、安装或运行候选代码，也没有遵循其中任何指令。
- GitHub 仓库搜索是关键词召回，会包含网站、脚手架与描述误报；下表再按“真实 LVGL 宠物/手表用户功能”人工过滤。

## 目标仓库与授权自检

- 固定路径：`D:\\code\\sf32\\agent-pet-sf32-text-display-fix`。
- `git rev-parse --show-toplevel`：`D:/code/sf32/agent-pet-sf32-text-display-fix`。
- `origin`：`https://github.com/fcb1379/agent-pet-sf32.git`，与任务目标一致。
- 扫描开始时工作树干净，没有需要丢弃的用户修改。
- 匿名远端核验：`refs/heads/master` 为 `a2805abeb72f02a726dce5dc49857280ba9cdc78`。
- 今日独立分支：`codex/2026-08-14-pet-stroking`，从上述 `origin/master` 基线创建，不在昨日分支追加。
- Git 身份：`qinmenghuai <qinmenghuai@wondertechlabs.com>`；仓库规定 Agent 提交标题与正文必须使用中文。
- `build.ps1` 存在；`sdk` 子模块未检出，`sdk/export.ps1` 不存在，是后续真实构建门禁。

## 去重基线

### 默认分支已有能力

- SF32LB525、SiFli SDK、RT-Thread、LVGL、390 × 490 触屏、PC 模拟器、BLE 手机伴侣、音乐、闹钟、计时器、录音和 TF 卡服务。
- Pet 页面已有 192 × 192 宠物舞台、短击、木鱼、LSM6 采样/冲击检测、任务花园、Agent 状态和 5 个图片/GIF 槽位。
- `origin/master@a2805ab` 新增了语音转写音频通道并优化图片同步；这是目标自身演进，只作去重基线，不作为外部候选评分。

### 已完成但未合入默认分支

- 多状态自主行为：`origin/codex/2026-08-07-pet-behavior-engine@0345d06b5434e0a43da6dd03fe54d7094d216a9e`，覆盖单击、三击/六击、长按、IMU、时间、Agent 状态及多 GIF 恢复，但没有连续触摸轨迹或持续抚摸语义。
- 天气陪伴：`codex/2026-08-08-lvgl-idea-scan@795e4d1de083f8738943f3d190c6d0ac6a09c8a8`。
- 呼叫 Momo：`codex/2026-08-11-momo-find-me@2c4cc8450823782c0491779b3bf1c7029a4cd936`。

因此不重复推荐普通短击/长按、晃动、Agent 状态、多 GIF、天气、寻宠、计时器、任务清单或单纯图片传输。

## 严格窗口内 14 个仓库

时间为 GitHub commit 的 committer UTC 时间；每项给出本窗口最新或最能代表实质用户变化的精确 SHA。

| # | 仓库 | 窗口证据 | 许可证 | 评估结论 |
|---:|---|---|---|---|
| 1 | [Kz0514/virtual_pet](https://github.com/Kz0514/virtual_pet) | [`55b88d59555335ff679d398c632f49a0346b6d27`](https://github.com/Kz0514/virtual_pet/commit/55b88d59555335ff679d398c632f49a0346b6d27)，`2026-08-13T16:00:13Z`，新增持续摸头、敲击/摇动、噪声和语音等模块，13,805 行新增、425 行删除 | Apache-2.0；README 另要求遵守 VPET 形象/动画授权 | 持续摸头的用户价值高且适配触屏；只做原创产品转化，不使用其代码、阈值和受版权保护素材。**推荐** |
| 2 | [constchr/routine-rush-watch](https://github.com/constchr/routine-rush-watch) | [`04f08a11b7e464caf9c74a06a020430db540c2fb`](https://github.com/constchr/routine-rush-watch/commit/04f08a11b7e464caf9c74a06a020430db540c2fb)，`2026-08-13T12:57:01Z`，每日步数目标一次性提示；同窗口另有 8 个 BLE、功耗、计步和音频提交 | 无许可证 | “陪宠散步”有潜力，但目标没有稳定后台计步、日切和真机精度基线；功耗/标定风险高，且来源无许可证。本轮不落地 |
| 3 | [Gangan-307/LCHSP_Watch](https://github.com/Gangan-307/LCHSP_Watch) | [`d70dbbd8a1715a70537d7e71a22ee1471d020b28`](https://github.com/Gangan-307/LCHSP_Watch/commit/d70dbbd8a1715a70537d7e71a22ee1471d020b28)，`2026-08-13T15:09:10Z`；同窗口另有 `d66580b` 通知焦点和 `2b67dda` 主页时间/步数 UI | 无许可证 | 通知焦点与震动需手机通知权限、协议和振动硬件闭环；目标 `NOTIFY` 仍保留，且宠物差异化较弱。本轮不落地 |
| 4 | [shujiCiallo/lvgl_watch](https://github.com/shujiCiallo/lvgl_watch) | [`95efa020e77f9b22c8ec201e8fc7ae85c1e02d51`](https://github.com/shujiCiallo/lvgl_watch/commit/95efa020e77f9b22c8ec201e8fc7ae85c1e02d51)，`2026-08-13T08:08:07Z`，24 小时心率极值、范围标签和自适应图轴；另有 `4dac2f2` | MIT | 目标没有经确认的 PPG 数据源；模拟健康值会误导用户。本轮不落地 |
| 5 | [TurtleWhal/GWatch-Paris](https://github.com/TurtleWhal/GWatch-Paris) | [`74d8b6bf17b6155c1ff9cd0d92b2068dc2c02c6f`](https://github.com/TurtleWhal/GWatch-Paris/commit/74d8b6bf17b6155c1ff9cd0d92b2068dc2c02c6f)，`2026-08-12T18:50:48Z`，优化 Tread 表盘框架图与 BLE 代码 | 无许可证 | 主要是表盘视觉与素材，目标缺少对应表盘产品诉求；不能复制素材。本轮不落地 |
| 6 | [mattivilola/ilo-esp32-waveshare-app](https://github.com/mattivilola/ilo-esp32-waveshare-app) | 窗口内 69 个提交；代表性 [`c029ce56d1993079e4624ef419cf11398cdc2491`](https://github.com/mattivilola/ilo-esp32-waveshare-app/commit/c029ce56d1993079e4624ef419cf11398cdc2491)，`2026-08-13T08:55:50Z`，RTC 专注舱；[`50f2b06bee4faebad141bfde328e8b8af6fbf314`](https://github.com/mattivilola/ilo-esp32-waveshare-app/commit/50f2b06bee4faebad141bfde328e8b8af6fbf314)，`2026-08-13T09:33:12Z`，宠物屏保长按动作；窗口末端 `034b729` 为 OTA 验证 | 无许可证 | 专注与目标计时器/任务花园重复；屏保长按与已完成行为分支重复；OTA/USB/Wi-Fi 超出今日单一功能。本轮不落地 |
| 7 | [zhao0112/codex-hardware-pet](https://github.com/zhao0112/codex-hardware-pet) | [`eca9f8940a782f8d904d6e0a57e5589674099b49`](https://github.com/zhao0112/codex-hardware-pet/commit/eca9f8940a782f8d904d6e0a57e5589674099b49)，`2026-08-12T16:37:01Z`，首次发布多会话硬件宠物；另有 `d978a34`、`b6a5c48` CI/Hook 兼容修复 | MIT | 多 Agent 状态、图片和桥接与目标现有 Agent 状态、多 GIF、BLE/手机端重复。本轮不落地 |
| 8 | [Sunflower613/Yvonne_Codex_Companion](https://github.com/Sunflower613/Yvonne_Codex_Companion) | [`aecff7bea827984e38f809234fd80651bfc758bc`](https://github.com/Sunflower613/Yvonne_Codex_Companion/commit/aecff7bea827984e38f809234fd80651bfc758bc)，`2026-08-13T07:11:52Z`，内存优化与滑动工具面板；窗口内共 8 个提交，末端 `e21cecf` 为截图图库 | 无许可证 | 会话导航/工具面板依赖上位机 Codex 数据并与目标状态看板重复；来源无许可证。本轮不落地 |
| 9 | [peter-neu/t-watch_ultra](https://github.com/peter-neu/t-watch_ultra) | [`14970a0d5b29546c94974e56c23ad0031f241da5`](https://github.com/peter-neu/t-watch_ultra/commit/14970a0d5b29546c94974e56c23ad0031f241da5)，`2026-08-13T08:46:11Z`，户外手表设计规格；本窗口共 7 个规格/计划提交，末端 `d9c06c8` 为忽略文件 | 无许可证 | 只有规划，且 GNSS/PDR/BHI2xy 等硬件不在目标 BOM，无法当前硬件验证。集中排除 |
| 10 | [veeru413/watchOS_firmware](https://github.com/veeru413/watchOS_firmware) | [`feccd709476b8dccab29833c6dbcc3acfbb19830`](https://github.com/veeru413/watchOS_firmware/commit/feccd709476b8dccab29833c6dbcc3acfbb19830)，`2026-08-13T11:35:31Z`，首次提交 | 无许可证 | 未形成可证据化的差异用户闭环，且无许可证。集中排除 |
| 11 | [Haraldon9847/waveshare-watch-rs](https://github.com/Haraldon9847/waveshare-watch-rs) | [`303c4296d3fc0c859030df076f93a9b347659e59`](https://github.com/Haraldon9847/waveshare-watch-rs/commit/303c4296d3fc0c859030df076f93a9b347659e59)，`2026-08-13T05:46:01Z`，仅 README 更新 | NOASSERTION | 无产品功能更新；Rust/no_std 且明确替代 LVGL，与目标栈不兼容。集中排除 |
| 12 | [infinition/waveshare-watch-rs](https://github.com/infinition/waveshare-watch-rs) | [`bce59cfe3c02ee7d05b22862b884426bf685a5a0`](https://github.com/infinition/waveshare-watch-rs/commit/bce59cfe3c02ee7d05b22862b884426bf685a5a0)，`2026-08-12T22:48:19Z`，README 指标与推荐区 | NOASSERTION | 仅文档，无新用户功能；技术栈不兼容。集中排除 |
| 13 | [sfungwinbond/watchfaceking](https://github.com/sfungwinbond/watchfaceking) | [`f1dec74032d368f958267d6fff0024f91d327be7`](https://github.com/sfungwinbond/watchfaceking/commit/f1dec74032d368f958267d6fff0024f91d327be7)，`2026-08-13T13:20:31Z`，十款动画表盘网页图库；本窗口末端 `3afc53d` 是网页分析 | 无许可证 | 是展示/商店网站而非可验证固件功能，素材权属不明。集中排除 |
| 14 | [m0wmt/WaveshareAmoled-1.43](https://github.com/m0wmt/WaveshareAmoled-1.43) | [`615c6b60ecc480459e4e7440e8ac1d5356facdcb`](https://github.com/m0wmt/WaveshareAmoled-1.43/commit/615c6b60ecc480459e4e7440e8ac1d5356facdcb)，`2026-08-13T10:40:51Z`，更新示例 | MIT | 仓库自述仍处研究阶段、无可用固件；没有独立用户功能。集中排除 |

## 最高价值方向评分

满分 5；“许可证/素材”越高表示边界越清晰，“工作量”越高表示越轻量，“风险”越高表示风险越低。

| 方向 | 原创性 | 用户价值 | 差异化 | 交互闭环 | 硬件适配 | 许可证/素材 | 工作量 | 风险 | 总分/40 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 摸摸 Momo 连续抚摸 | 4 | 5 | 5 | 5 | 5 | 4 | 5 | 4 | **37** |
| 陪宠散步日目标 | 4 | 4 | 4 | 5 | 3 | 1 | 2 | 2 | 25 |
| 手机通知焦点与震动 | 2 | 4 | 2 | 4 | 3 | 1 | 2 | 2 | 20 |
| RTC 专注舱 | 2 | 3 | 1 | 4 | 5 | 1 | 3 | 3 | 22 |
| 心率趋势 | 2 | 3 | 2 | 3 | 1 | 5 | 2 | 2 | 20 |

## 今日决策与开发门禁

1. 提交本扫描记录和 `docs/2026-08-14-momo-stroking-prd.md`，文档提交作为产品子任务完成标志。
2. “摸摸 Momo”进入 P1，但只能原创实现；不复制外部代码、阈值或任何 VPET/表盘素材。
3. 嵌入式子任务必须在本文档提交后运行，并先提交独立实现方案；所有 LVGL 对象与输入事件必须留在 GUI 线程，不创建新线程。
4. 当前 `sdk/export.ps1` 缺失。恢复仓库记录的固定子模块版本并确认工具链前，开发构建门禁阻塞；不得跳过构建或虚报通过。
5. 不推送、不建 PR、不合并默认分支。
