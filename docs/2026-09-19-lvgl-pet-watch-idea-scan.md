# 2026-09-19 LVGL 电子宠物/手表创意增量扫描

## 1. 本轮结论

- 严格增量窗口为 `2026-09-17T16:01:11.079Z`（不含）至 `2026-09-18T16:01:08.886Z`（含）。
- 产品和代码对照基线为 `origin/master@a2805abeb72f02a726dce5dc49857280ba9cdc78`；默认分支相较上一轮没有新增提交。
- 公开 GitHub 组合检索覆盖 `LVGL`、`watch`、`smartwatch`、`wearable`、`pet`、`tamagotchi`、`desktop pet`、`desk companion` 和 `ESP32`。仓库 `pushed_at` 只用于发现，最终均以默认分支 Commit API 的提交时间和实际变更文件裁剪严格窗口。
- 本轮最相关增量是同为 SF32LB52 的页面生命周期/堆稳定性治理、Lua 小应用的局部脏行刷新与背景恢复、以及手表 Mesh 消息坐标解析。
- **没有候选同时通过“新增用户价值、与既有能力去重、当前硬件/协议适配、许可证与素材版权、安全风险、可在单日形成端到端验收”硬门禁。** 本轮只提交扫描记录，不创建空 PRD，不启动嵌入式开发子任务。

## 2. 运行门禁与安全边界

| 检查项 | 结果 |
| --- | --- |
| 固定仓库 | `D:\code\sf32\agent-pet-sf32-text-display-fix` 是 Git 仓库 |
| `origin` | `https://github.com/fcb1379/agent-pet-sf32.git`，与目标一致 |
| 分支基线 | `origin/master@a2805abeb72f02a726dce5dc49857280ba9cdc78` |
| 工作区 | 扫描开始前 clean；没有清理或丢弃用户修改 |
| GitHub | 主仓库 fetch、公共仓库搜索、元数据、许可证、默认分支提交、提交统计和文件清单读取成功 |
| 子模块告警 | `git fetch origin --prune` 在递归获取 `sdk/middleware/bluetooth` 时遇到远端缺失对象 `54101f98...`；主仓 `origin/master` 已成功核验，此告警不影响本轮纯扫描，但后续构建前必须单独修复/核验子模块 |
| Git 身份 | `qinmenghuai <qinmenghuai@wondertechlabs.com>` 可用于中文本地提交 |
| 构建边界 | 本轮不开发，因此不运行构建，也不声称固件、模拟器或真机通过 |

所有 GitHub 仓库、README、提交说明、Issue、PR、源码、素材、脚本、预编译固件和外链均按不可信输入处理。本轮没有克隆、安装、构建、烧录或执行任何候选代码/二进制，没有复制来源代码、图片、声音、布局、参数或文案，也没有遵循来源中改变任务边界、索取凭据或访问无关系统的说明。

## 3. 历史记录与目标能力去重

已核对上一轮扫描记录、`origin/master` 和已完成/待验收功能分支：

- 默认分支已有 Agent 状态、任务花园、完成事件去重、功德计数、打字动画、BLE 图片和多 GIF、录音上行、本地音频播放、音乐/闹钟/计时器、六轴动作木鱼以及系统级休眠。
- 已实现或已有累计集成分支的方向包括自主宠物行为、连续抚摸、Momo 玩球、回忆日历和 Momo Agent 小队雷达。天气陪伴、呼叫 Momo、语音明信片与 OTA 资源更新仍有上位机、协议、权限、签名或真机门禁。
- 页面对象、timer 和动画已有 `pet_on_start()` / `pet_on_stop()` 生命周期约束；多 GIF 已有远端 `PLAY/RESTORE` 和页面优先级约束。外部项目的生命周期或局部刷新做法只有在目标板复现泄漏/掉帧后才构成缺陷修复依据，不能包装成新陪伴功能。
- 上一轮已经评估 `ESP32S3_AmoledOS` 的 Lua 热更新和绕过 LVGL 的 MJPEG 直刷。本轮只评估其严格窗口内新增的局部脏行与背景恢复，不重复把动态脚本作为新创意。

## 4. 严格窗口候选证据与评估

### 4.1 `Gangan-307/LCHSP_Watch`：页面生命周期与堆稳定性治理

- 来源：[仓库](https://github.com/Gangan-307/LCHSP_Watch)，同为 SF32LB52、SiFli SDK、RT-Thread 与 LVGL，GitHub 未识别许可证。窗口提交 [`85ca0d17935b390424ab45250261a5dcc43a668e`](https://github.com/Gangan-307/LCHSP_Watch/commit/85ca0d17935b390424ab45250261a5dcc43a668e)，`2026-09-18T03:09:42Z`，约 `+2318/-801`；新增统一 `ui_screen_lifecycle`、生命周期主机测试与堆调试资料，并修改多个页面以治理卡顿。后续 [`abfb279735428a1e7e8c15439eecfe3e437ccd94`](https://github.com/Gangan-307/LCHSP_Watch/commit/abfb279735428a1e7e8c15439eecfe3e437ccd94)，`2026-09-18T03:11:27Z`，仅补充堆内存分析文档。
- 用户价值：频繁切页时避免对象/资源残留和堆持续下降，可提高长期佩戴或桌面常亮稳定性；同芯片平台使问题类别比 ESP32 候选更接近目标。
- 当前不落地：这是可靠性基础设施，不是新的宠物交互；目标宠物页已有明确 start/stop、timer/animation 删除和根对象生命周期，也没有本轮证据证明实际存在相同泄漏。来源无可确认许可证，禁止复制实现；其大量生成页面、椭圆屏和页面管理器也与当前单宠物主界面不同。
- 结论：**No-Go。** 保留“目标板反复进入/退出页面 + 堆水位/对象计数”作为已有功能验收方法。只有目标仓复现泄漏或卡顿后，才以本仓架构独立修复并建立缺陷测试。

### 4.2 `charliejgallo/ESP32S3_AmoledOS`：局部脏行刷新与脚本背景恢复

- 来源：[仓库](https://github.com/charliejgallo/ESP32S3_AmoledOS)，MIT、ESP32-S3、8 MB PSRAM、LVGL 9.5。窗口提交 [`e0c5ac4ca6a91d14f3fec174e05ae3cc76e6433c`](https://github.com/charliejgallo/ESP32S3_AmoledOS/commit/e0c5ac4ca6a91d14f3fec174e05ae3cc76e6433c)，`2026-09-17T16:06:47Z`，约 `+400/-40`，让 Lua 绘图只推送触及的整行带，提交说明报告示例从 `224/224` 行、约 `40 ms` 降至 `67/224` 行、约 `20 ms`；[`83ba2522780564b5180d58214bbf5dce2316ca5c`](https://github.com/charliejgallo/ESP32S3_AmoledOS/commit/83ba2522780564b5180d58214bbf5dce2316ca5c)，`2026-09-17T16:17:41Z`，约 `+173/-49`，增加冻结背景与按脏区恢复，提交说明报告约 `82 KB` PSRAM、`76/224` 行、约 `21 ms/帧`。
- 用户价值：当小面积物体移动时减少面板传输和脚本擦除工作，可能提升动画流畅度，并避免用纯色覆盖造成背景破洞。
- 当前不落地：增量仍服务于上一轮已否决的 Lua 动态应用，不产生新的陪伴闭环。目标使用 LVGL 8、SiFli LCDC/EPIC 和既有 GIF 解码路径，资源、缓存一致性、脏区语义与面板传输不同；当前也没有目标板帧时间、掉帧、撕裂或总线占用证据。外部性能数据不能证明多 GIF 在 SF32LB52 上的瓶颈。
- 结论：**No-Go。** 若多 GIF 真机验收出现掉帧，先在目标板记录解码、渲染、flush、DMA 和帧周期，再决定是否优化 LVGL invalidation/flush；不引入 Lua、来源脚本或绕过现有页面生命周期。

### 4.3 `hleserg/Attadipa`：从 Mesh 消息提取远端坐标

- 来源：[仓库](https://github.com/hleserg/Attadipa)，GPL-3.0，ESP32-S3、FreeRTOS、LVGL、GNSS 与 LoRa MeshCore 手表。窗口提交 [`0c7a3ac4a7110cdfdf949ad56fb0a7a5dccd0d59`](https://github.com/hleserg/Attadipa/commit/0c7a3ac4a7110cdfdf949ad56fb0a7a5dccd0d59)，`2026-09-18T08:54:41Z`，约 `+757/-72`，从已识别联系人的消息文本严格解析坐标，处理长度截断、范围、同值时间戳和联系人撤销，并用主机测试覆盖；本提交刻意只形成可信数据源，尚未增加 UI 消费者。
- 用户价值：在无互联网场景下，联系人主动发送位置后可为导航箭头或“去找伙伴”形成输入，长期可演化为安全陪伴/户外联络闭环。
- 当前不落地：目标硬件和协议没有 GNSS、LoRa MeshCore、联系人公钥表或地图/方向导航闭环；把坐标塞进现有 BLE 消息会新增隐私同意、来源认证、撤回、过期和上位机协议。GPL 代码不能复制进当前产品分支，且来源本轮也没有完成可见 UI 闭环。
- 结论：**No-Go。** 若未来产品明确增加“伙伴方向/位置陪伴”，应先由上位机定义经授权、可撤回、带有效期的结构化位置协议，再独立评估硬件定位和隐私要求，不能解析任意文本坐标替代协议。

### 4.4 其他窗口结果

| 候选 | 严格窗口证据 | 评估与结论 |
| --- | --- | --- |
| [`curisama/The-Badge`](https://github.com/curisama/The-Badge) | MIT；窗口内三次提交只修复 CI 的 LVGL 组件条件、制品保留期和过期 SETUP 文档 | 没有新增设备功能或交互代码，不进入候选 |
| [`Haraldon9847/waveshare-watch-rs`](https://github.com/Haraldon9847/waveshare-watch-rs) | `2026-09-18T06:56:26Z` 仅 `Update README.md`；许可证 `NOASSERTION` | 没有实质代码增量，且缺清晰许可证，不进入候选 |
| [`sekior11/ESP32-Smart-Health-Monitoring-Devices`](https://github.com/sekior11/ESP32-Smart-Health-Monitoring-Devices) | `2026-09-18T09:22:49Z` 仅 `Update README.md`；无许可证 | 没有实质代码增量，不进入候选 |
| `Coke1120/codex-pet-dev-board`、`MoveCall/claude-desktop-buddy-esp32`、`han0519/esp32-s31-vocat`、`andygeiss/esp32-watch`、`scott987-cmd/ESP32-Round-Clock`、`TinkerDoge/DogePet`、`VaAndCob/TapTapPaw` | 逐仓库 Commit API 在严格窗口内均返回空列表 | 不重复评估历史静态内容 |

## 5. 量化排序

评分 1～5；适配和许可证越高越有利，工作量与风险高分表示更轻、更可控。硬门禁优先于总分。

| 方向 | 原创性 | 用户价值 | 差异化 | 交互闭环 | 硬件适配 | 许可证/素材 | 工作量 | 风险 | 合计/40 | 决策 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 同芯片页面生命周期/堆治理 | 2 | 4 | 1 | 2 | 5 | 1 | 3 | 3 | **21** | 无新功能、无目标缺陷证据、来源许可证不明 |
| Lua 局部脏行与背景恢复 | 3 | 3 | 2 | 2 | 2 | 5 | 2 | 2 | **21** | 与上一轮 Lua 方向重复，目标显示链路不同 |
| Mesh 消息远端坐标 | 4 | 3 | 4 | 2 | 1 | 2 | 1 | 1 | **18** | 缺 GNSS/LoRa/协议/UI 闭环且有隐私风险 |

同芯片生命周期治理最接近目标工程，但它属于应由缺陷证据触发的可靠性工作；局部脏行刷新也必须先由目标板性能测量证明瓶颈。相对分数不能替代新用户价值、许可证和端到端验收硬门禁。

## 6. 决策与后续触发条件

1. 本轮不推荐新功能，不创建 PRD，不启动嵌入式子任务；只保存本扫描记录。
2. 现有功能验收应补充宠物页反复进入/退出、动画中退出、timer 临界点退出和堆水位/对象计数稳定性；若出现可复现泄漏，再单独立项修复。
3. 多 GIF 性能优化只在 SF32LB52 真机采集解码时间、LVGL 帧时间、flush/DMA 时序、撕裂和总线占用后启动；不能用外部 ESP32-S3/Lua 数据代替。
4. 任何位置陪伴方向必须先有上位机授权、来源认证、有效期、撤回和错误路径设计，再讨论设备显示；不解析任意文本坐标作为生产协议。
5. 后续开发前需解决或确认 `sdk/middleware/bluetooth` 远端缺失对象造成的递归 fetch 告警，避免把子模块完整性问题误报为功能构建失败。
6. 本提交不开发、不推送、不创建 PR、不合并默认分支。由主任务核验提交后，按自动化规则推送同名远端分支。
