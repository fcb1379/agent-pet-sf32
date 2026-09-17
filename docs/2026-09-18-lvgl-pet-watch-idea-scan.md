# 2026-09-18 LVGL 电子宠物/手表创意增量扫描

## 1. 本轮结论

- 严格增量窗口为 `2026-09-16T16:00:24.551Z`（不含）至 `2026-09-17T16:01:11.079Z`（含）。
- 扫描前已执行 `git fetch origin master --prune`；产品和代码对照基线为 `origin/master@a2805abeb72f02a726dce5dc49857280ba9cdc78`，默认分支在本轮没有新增提交。
- 已组合检索 GitHub 公开仓库中的 `LVGL`、`watch`、`smartwatch`、`wearable`、`pet`、`tamagotchi`、`desktop companion` 和 `ESP32`，并用默认分支 Commit API 的提交者时间严格裁剪窗口。仓库级 `pushed_at` 仅用于发现，不代替代码增量证据。
- 本轮最相关的增量是手表抬腕唤醒与麦克风/网络按需启停、跨应用音频焦点与可取消播放、SD 卡 Lua 小应用热更新、MJPEG 直刷播放，以及异步 SPI DMA 刷屏。
- **没有候选同时通过“新增用户价值、与已有功能去重、当前硬件与协议适配、许可证与素材版权、安全风险、可在单日形成端到端验收”硬门禁。** 本轮只提交扫描记录，不创建空 PRD，不启动嵌入式开发子任务。

## 2. 运行门禁与安全边界

| 检查项 | 结果 |
| --- | --- |
| 固定仓库 | `D:\code\sf32\agent-pet-sf32-text-display-fix` 是 Git 仓库 |
| `origin` | `https://github.com/fcb1379/agent-pet-sf32.git`，与目标一致 |
| 分支基线 | `origin/master@a2805abeb72f02a726dce5dc49857280ba9cdc78` |
| 工作区 | 扫描开始前 clean；没有清理或丢弃用户修改 |
| GitHub | 公开仓库搜索、元数据、许可证、默认分支提交、提交统计与文件清单读取成功 |
| Git 身份 | `qinmenghuai <qinmenghuai@wondertechlabs.com>` 可用于中文本地提交 |
| 构建入口 | `build.ps1` 与 `sdk/export.ps1` 存在；本轮不开发，因此未运行构建，也不声称固件或真机通过 |

所有 GitHub 仓库、README、提交说明、Issue、PR、源码、素材、脚本、预编译固件和外链均按不可信输入处理。本轮没有克隆、安装、构建、烧录或执行任何候选代码/二进制，也没有复制来源代码、图片、声音、脚本、布局、参数或文案，更没有遵循来源中改变任务边界、索取凭据或访问无关系统的说明。

## 3. 历史记录与目标能力去重

已核对 Git 全部历史扫描记录、`origin/master` 和已完成/待验收功能分支：

- 默认分支已有 Agent 状态、任务花园、完成事件去重、功德计数、打字动画、BLE 图片和多 GIF、录音上行、本地音频播放、音乐/闹钟/计时器、六轴动作木鱼以及系统级休眠。
- 已实现或已有累计集成分支的方向包括自主宠物行为、连续抚摸、Momo 玩球、回忆日历和 Momo Agent 小队雷达。天气陪伴、呼叫 Momo、语音明信片与 OTA 资源更新仍有上位机、协议、权限、签名或真机门禁。
- 自主行为引擎已有 Calm、Happy、Curious、Sleepy、Lonely 等状态，以及 affinity、energy、arousal、最近互动、确定性随机决策和限频持久化；多 GIF、远端 `PLAY/RESTORE`、触摸、IMU、Agent 状态与页面生命周期已有优先级约束。
- 音频方向已有录音上传、本地播放、音乐、闹钟和计时器。新的语音或音效功能必须先统一播放器、录音、蓝牙、闹钟抢占、取消、静音/音量、页面退出和低功耗所有权，不能另建互不协调的音频通道。

## 4. 严格窗口候选证据与评估

### 4.1 `andygeiss/esp32-watch`：抬腕即开、按需麦克风和按需联网

- 来源：[仓库](https://github.com/andygeiss/esp32-watch)，MIT，ESP32-S3、410×502 AMOLED、LVGL。窗口提交 [`680a8dadd233fbb2d148eff7006c296280d30052`](https://github.com/andygeiss/esp32-watch/commit/680a8dadd233fbb2d148eff7006c296280d30052)，`2026-09-16T16:50:20Z`，约 `+350/-17`，让 RTC 在离线启动时校时，并由 IMU 抬腕/触摸点亮屏幕；[`1922d1c1c25739c3261256f2926e9c4002d3769c`](https://github.com/andygeiss/esp32-watch/commit/1922d1c1c25739c3261256f2926e9c4002d3769c)，`2026-09-16T20:41:54Z`，约 `+58/-10`，把麦克风采集限定在亮屏或对话期间；[`2bcd162a08912ab7f167563a05397c9e71dafd7b`](https://github.com/andygeiss/esp32-watch/commit/2bcd162a08912ab7f167563a05397c9e71dafd7b)，`2026-09-17T11:01:13Z`，约 `+196/-35`，在检测到第一句话时才启用网络。
- 用户价值：用户抬腕或触摸后再进入收音/联网窗口，能把“呼叫角色”做成可感知的交互起点，同时减少全天收音的隐私风险和无线待机功耗；抬腕、亮屏、对话、息屏形成清晰闭环。
- 当前不落地：目标已有 IMU、系统休眠、录音上传和“呼叫 Momo”产品方向，但没有经验证的本地首词/唤醒词检测、连续对话、ASR/TTS、PAN 网络会话及麦克风隐私指示闭环。来源依赖 ESP32-S3、QMI8658、PCF85063、AXP2101、Wi-Fi/TLS 和不同面板，不能证明 SF32LB52 的传感器坐标、唤醒源、功耗或响应时延。若只迁移抬腕亮屏，则属于现有电源/IMU可靠性，不是新的陪伴功能。
- 结论：**No-Go。** 可将“只有明确亮屏/交互窗口才允许收音、可见麦克风状态、超时强制停录、对话退出即断网”加入未来呼叫 Momo 的隐私验收，但需先由上位机/协议和真机功耗门禁完成，不复制来源实现或参数。

### 4.2 `scott987-cmd/ESP32-Round-Clock`：跨应用音频焦点和可取消播放

- 来源：[仓库](https://github.com/scott987-cmd/ESP32-Round-Clock)，Apache-2.0；窗口提交 [`398289bc094da6afa3fb7adfdb6c3aaac8639feb`](https://github.com/scott987-cmd/ESP32-Round-Clock/commit/398289bc094da6afa3fb7adfdb6c3aaac8639feb)，`2026-09-17T02:28:43Z`，约 `+458/-109`，在 music、story、voice、pet dialogue 等模块之间增加可抢占 audio focus、取消链路、主机测试和设备测试；[`8cc2054fb9a413f9b1b09c2c26fb260da56d1844`](https://github.com/scott987-cmd/ESP32-Round-Clock/commit/8cc2054fb9a413f9b1b09c2c26fb260da56d1844)，`2026-09-17T00:55:31Z`，约 `+935/-106`，补齐宠物语音对话、后端服务和设备测试。
- 用户价值：闹钟、音乐、故事、语音输入和宠物回复互相打断时，明确的所有权、优先级和取消路径能避免串音、无法停播或页面退出后继续发声，是未来语音陪伴的关键基础。
- 当前不落地：它是基础设施可靠性而不是新的用户功能，且上一轮已经把音频所有权列为情境音效的前置门禁。来源的 ESP32-S3、Wi-Fi 服务端、应用模型和播放器与当前 SiFli/RT-Thread 音频服务不同；直接迁移会跨越线程、Codec、电源和后端协议边界。当前项目也缺少能在同一天完成的多来源真机并发验收矩阵。
- 结论：**No-Go。** 后续若目标复现音频抢占缺陷，应按现有服务单独立项“音频焦点管理器”，先定义闹钟 > 用户通话/录音 > 明确点播 > 宠物提示的策略、取消 ACK、超时和页面退出测试，再在目标板自研实现。

### 4.3 `charliejgallo/ESP32S3_AmoledOS`：Lua 小应用热更新与 MJPEG 直刷

- 来源：[仓库](https://github.com/charliejgallo/ESP32S3_AmoledOS)，MIT。提交 [`07995ef1460fa256613ac5523d18e6a09a24cd90`](https://github.com/charliejgallo/ESP32S3_AmoledOS/commit/07995ef1460fa256613ac5523d18e6a09a24cd90)，`2026-09-17T15:13:47Z`，约 `+515/-65`，让 SD 卡上的 `.lua` 作为带名字/图标的启动器应用；[`a9a96633a302e139ac62543514fe2443cd55b276`](https://github.com/charliejgallo/ESP32S3_AmoledOS/commit/a9a96633a302e139ac62543514fe2443cd55b276)，约 `+1848/-165`，提供受限绘图、触摸、时钟和音效 API，并报告脚本/缩放/刷屏分项性能；[`cbddeee0c29965a0b8481d9228661614cf5e8293`](https://github.com/charliejgallo/ESP32S3_AmoledOS/commit/cbddeee0c29965a0b8481d9228661614cf5e8293)，`2026-09-16T23:49:00Z`，约 `+2082/-45`，实现 SD 卡 MJPEG+WAV、后台解码和绕过 LVGL 的面板直刷。
- 用户价值：资源或小交互不重刷固件即可迭代，且实测性能分解能帮助识别动画瓶颈；对宠物内容更新有长期吸引力。
- 当前不落地：目标的多 GIF 与 OTA 资源更新仍缺发布者认证、签名、容量、回滚和上位机闭环。引入解释器或动态应用会扩大脚本权限、栈深、堆、超时、崩溃隔离和 ABI 风险；绕过 LVGL 直刷会破坏当前页面生命周期、输入和宠物状态层。来源依赖 8 MB PSRAM、microSD、LVGL 9.5 和不同 SPI 面板，不能证明 240×240 SF32LB52 可承受同样架构。
- 结论：**No-Go。** 优先完成已有多 GIF/资源协议的签名、版本、容量、原子切换和回滚验收，不引入动态代码、Lua、来源图标或视频素材。

### 4.4 `duli07556-sudo/low-power-smartwatch`：异步 SPI DMA 刷屏

- 来源：[仓库](https://github.com/duli07556-sudo/low-power-smartwatch)，窗口内新建，GitHub API 未识别许可证。提交 [`0f7ae3f43042a082349f7bafbadd6c339bbd746e`](https://github.com/duli07556-sudo/low-power-smartwatch/commit/0f7ae3f43042a082349f7bafbadd6c339bbd746e)，`2026-09-17T12:57:21Z`，约 `+208/-51`，修改 STM32F411、LVGL port、LCD 和任务代码，使 SPI DMA 刷新异步化。
- 用户价值：异步刷屏可能降低 GUI 任务等待，提高触摸响应并为动画留出 CPU 时间。
- 当前不落地：这是不同 MCU/LCD/DMA 驱动上的性能优化，不是新的宠物交互；目标已有 SiFli LCDC/EPIC/LVGL 8 驱动，必须先以目标板 profiler、帧时间、撕裂和总线占用证明瓶颈。无许可证也禁止复制代码。
- 结论：No-Go。若多 GIF 真机测试出现掉帧，再对当前 flush 回调、DMA 完成时序、缓存一致性和 LVGL 线程规则做独立性能缺陷分析。

### 4.5 其他窗口结果

| 候选 | 严格窗口证据 | 评估与结论 |
| --- | --- | --- |
| [`Haraldon9847/waveshare-watch-rs`](https://github.com/Haraldon9847/waveshare-watch-rs) | `2026-09-17T15:01:30Z` 仅提交 `Update README.md`；许可证为 `NOASSERTION`，且项目明确不用 LVGL | 没有 LVGL 实质代码增量，不进入候选 |
| [`ricardo-Mi/ovWatch`](https://github.com/ricardo-Mi/ovWatch) | 窗口内新建，但默认分支 Commit API 返回空仓库冲突，且无许可证 | 只有元数据，无法证明可审查的功能、代码或许可证 |
| [`witekin/winterdash`](https://github.com/witekin/winterdash) | GPL-3.0；[`10ddbc0e6c6aad88a1424764182fe08330c3a666`](https://github.com/witekin/winterdash/commit/10ddbc0e6c6aad88a1424764182fe08330c3a666) 增加 Home Assistant 屏幕镜像 | 是充电器仪表而非宠物/手表交互，且 GPL 与当前产品代码迁移边界不合适 |
| `grafana/TamaGROTchi`、`Ellenp2p/esp32-pet-tamagotchi`、`CyberXcyborg/ESP32-TamaPetchi`、`SongZ1Hao/esp32watch`、`lukeswitz/esp32-c3-mini-watchfaces` | 仓库元数据和默认分支 API 均显示本窗口没有提交 | 不重复评估历史静态内容 |

## 5. 量化排序

评分 1～5；适配和许可证越高越有利，工作量与风险高分表示更轻、更可控。硬门禁优先于总分。

| 方向 | 原创性 | 用户价值 | 差异化 | 交互闭环 | 硬件适配 | 许可证/素材 | 工作量 | 风险 | 合计/40 | 决策 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 抬腕触发收音与按需联网 | 3 | 4 | 3 | 4 | 2 | 5 | 2 | 2 | **25** | 与呼叫 Momo 重叠，缺跨端语音/隐私/功耗门禁 |
| 跨应用音频焦点与取消 | 2 | 5 | 2 | 3 | 2 | 5 | 2 | 3 | **24** | 关键基础设施但非新功能，需目标服务单独验证 |
| Lua 小应用与热更新 | 4 | 3 | 4 | 4 | 1 | 5 | 1 | 1 | **23** | 动态代码、资源签名和内存隔离风险过高 |
| MJPEG 直刷播放 | 3 | 2 | 2 | 2 | 1 | 5 | 1 | 1 | **17** | 与宠物主闭环弱，绕过 LVGL 生命周期 |
| 异步 SPI DMA 刷屏 | 1 | 3 | 1 | 1 | 1 | 1 | 2 | 2 | **12** | 无许可证且不同驱动，仅适合作为性能缺陷线索 |

最高分的“抬腕触发收音与按需联网”仍被现有呼叫 Momo 方向、上位机/协议、隐私指示、PAN/ASR/TTS 和真机功耗门禁否决；“音频焦点”值得作为基础设施前置项，但不能包装成每日新功能。相对分数不能替代硬门禁。

## 6. 决策与后续触发条件

1. 本轮不推荐新功能，不创建 PRD，不启动嵌入式子任务；只保存本扫描记录。
2. 呼叫 Momo 下一轮设计必须明确：收音只在可见交互窗口开启；本地指示灯/屏幕状态不可被远端隐藏；超时、息屏、页面退出和错误路径都强制停录；明确音频是否离开设备以及用户撤回方式。
3. 音频功能应先完成目标项目自己的焦点矩阵、取消 ACK、线程和 Codec 电源所有权，再考虑宠物音效、语音聊天或故事；不能从不同 ESP32 应用模型直接复制实现。
4. 多 GIF 和 OTA 资源协议优先完成签名、版本、容量、原子切换、失败回滚与上位机验收；不以 Lua 或动态 `.so` 绕过这些门禁。
5. 抬腕、刷屏和动画性能必须使用当前 SF32LB52、240×240 屏幕、现有 IMU/LCDC/EPIC/LVGL 8 驱动测量，分别报告代码支持、编译、烧录、设备行为和真实功耗；外部仓库测量不能替代目标硬件证据。
6. 本提交不开发、不推送、不创建 PR、不合并默认分支。由主任务核验提交后，按自动化规则推送同名远端分支。
