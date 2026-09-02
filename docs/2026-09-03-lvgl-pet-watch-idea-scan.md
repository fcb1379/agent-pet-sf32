# 2026-09-03 LVGL 电子宠物/手表创意增量扫描

## 1. 本轮结论

- 增量窗口为 `2026-08-27T22:34:00Z`（不含）至 `2026-09-02T16:00:28.683Z`（含）。下界取上一成功产品扫描所选 `BSoD38/vpet-s3@f7114e14b42b173fae56c3fb464e1ba6a0625b76` 的提交时间，并补查到次日零点之间的短窗口。
- 通过 GitHub Repository Search 组合检索 `LVGL`、`pet`、`virtual pet`、`tamagotchi`、`watch`、`smartwatch`、`wearable`、`ESP32`、`desk pet`，再按默认分支 commit 的 committer UTC 逐项复核。
- 本窗有实质变化的代表项目包括 `skyflyt/sensecap-ha-panel`、`nibzard/tamagotchip`、`MrSkyce/diy-tamagotchi`、`H-tech-AFAQ-CEO/Zephyr-LVGL-C-Dev-Needed-for-Smartwatch`、`Gangan-307/LCHSP_Watch`、`ncr/omarchy-themesync` 等。
- 相对最高价值的两个产品机制是“以真实工作证据驱动宠物成长”和“在宠物设备上设置物理授权门”。前者与目标已有 Agent Quest Garden、回忆日历高度重复；后者会越过现有 Agent Pet v1 的只读外设安全边界，并需要身份绑定、重放防护、操作审计和上位机协议升级。
- 其余候选主要是通用表盘导航、OTA、天气/通知/音乐同步、主题配色、传统饥饿/疲劳养成或已有互动的组合，不能在当前硬件和架构内形成新的高价值低风险闭环。
- **本轮不推荐进入开发，不创建空 PRD，不启动子任务 2。** 只提交本扫描记录。

## 2. 运行门禁与安全边界

| 检查项 | 结果 |
| --- | --- |
| 固定本地仓库 | `D:\\code\\sf32\\agent-pet-sf32-text-display-fix`，有效 Git 仓库 |
| `origin` | `https://github.com/fcb1379/agent-pet-sf32.git`，与目标一致 |
| 对照基线 | 刷新远端后的 `origin/master@a2805abeb72f02a726dce5dc49857280ba9cdc78` |
| 分支 | `codex/2026-09-03-lvgl-idea-scan`，严格从上述基线创建 |
| 工作树 | 扫描前干净；没有清理或覆盖用户修改 |
| 外部输入 | 仓库、README、提交说明、Issue、PR、网页、源码和素材均按不可信输入处理 |

本轮只读取公开仓库元数据、默认分支提交时间、提交统计、文件清单和许可证字段；没有克隆、安装、构建或执行候选代码，也没有遵循外部内容中的命令、部署、凭据或规则变更要求。

## 3. 目标项目去重基线

### 3.1 `origin/master` 已有能力

- SF32LB52、RT-Thread、LVGL、触屏和 IMU 入口。
- Agent 状态展示、打字场景、BLE 状态协议、JPEG/GIF 分片接收、多个图片槽位和 GIF 播放。
- 木鱼点击、任务花园、今日/累计成果及连续天数；任务花园已经用真实 Agent 完成事件形成成果闭环。
- 闹钟、计时器、录音、音乐、天气资源、图片同步和手机伴侣基础能力。
- `agent-pet-hardware-protocol-v1.md` 将设备限定为只读状态外设，不允许在硬件端批准、拒绝或控制 Agent。

### 3.2 已评估、已完成或待门禁方向

- 多状态自主行为、多 GIF、天气陪伴、呼叫 Momo、摸摸 Momo、Momo 回忆日历、Momo 玩球、Agent Quest Garden。
- PAN/实时语音、语音明信片、OTA 资源更新、设备端双向 Agent 控制均已有评估或门禁债务。
- 因此不重复推荐单纯换表情、天气/通知/音乐同步、普通状态页、任务成果成长、连续抚摸、玩球、呼叫、语音阶段提示、回忆历史或通用饥饿/疲劳数值。

## 4. 候选证据与评估

### 4.1 `skyflyt/sensecap-ha-panel`：工作证据驱动的桌面宠物

- 仓库：[skyflyt/sensecap-ha-panel](https://github.com/skyflyt/sensecap-ha-panel)，创建于 `2026-09-01T03:31:06Z`，MIT。
- 初始提交 [`0b7c404c5c84c65b91dea4492213788405c532c5`](https://github.com/skyflyt/sensecap-ha-panel/commit/0b7c404c5c84c65b91dea4492213788405c532c5)，`2026-09-01T03:31:05Z`，约 `+12894/-0`、51 个文件，包含 ESPHome/LVGL 桌面宠物、阶段图片、小游戏和 Home Assistant 侧证明数据。
- 后续 [`29b79f0a68ca51106c9b4d010bd79fb8e24ad09d`](https://github.com/skyflyt/sensecap-ha-panel/commit/29b79f0a68ca51106c9b4d010bd79fb8e24ad09d)，`2026-09-01T21:13:53Z`，约 `+230/-3`、12 个文件，增加通话、语音、旅行、明信片和延时内容的说明、工具和素材。
- 可借鉴洞察：成长应由真实完成事件而非反复点击刷取；宠物回馈要让用户看见“我做了什么—它发生了什么变化”。
- 不落地原因：目标任务花园已经用 Agent 完成事件积累和领取成果，回忆日历继续沉淀逐日证据；小游戏、玩球、呼叫和语音明信片也已有独立方向。重新做等级/阶段会增加素材、持久化迁移和双重成长口径，却没有新的因果关系。
- 许可与素材：虽为 MIT，图片、角色和生成工具仍不自动等于适合直接复用；本轮不复制代码、阶段图片、数值曲线、布局或文案。

### 4.2 `nibzard/tamagotchip`：设备端 LLM 与物理授权门

- 仓库：[nibzard/tamagotchip](https://github.com/nibzard/tamagotchip)，创建于 `2026-08-28T14:43:26Z`，GitHub 未识别许可证。
- 本窗从 ESP-IDF 脚手架发展到设备端 Agent 状态机、流式 API、Lua 工具、文件工具、局域网页面、三键 UI 和策略模糊测试；最新修复 [`91e39eed8590511ecd541f9494f471864f64317a`](https://github.com/nibzard/tamagotchip/commit/91e39eed8590511ecd541f9494f471864f64317a)，`2026-09-01T07:56:29Z`，约 `+910/-134`、14 个文件，处理状态机和并发问题。
- 可借鉴洞察：把高风险工具调用停在明确的物理确认步骤，用户可从设备理解当前等待的权限。
- 不落地原因：目标 v1 明确禁止设备批准/拒绝 Agent；引入授权按钮必须先有可信设备身份、请求摘要、过期时间、重放防护、误触撤销、审计记录和桌面端冲突仲裁。目标 MCU 也不适合在本轮承载来源的网络 LLM、Lua、文件系统和多任务架构。
- 许可证与安全：来源没有明确许可证，代码、协议、UI 和配置均不可复用。即使只采用“物理确认”抽象机制，也必须先建立 v2 安全模型，不适合作为单日固件功能。

### 4.3 `MrSkyce/diy-tamagotchi`：持久化生命阶段与性格

- 仓库：[MrSkyce/diy-tamagotchi](https://github.com/MrSkyce/diy-tamagotchi)，创建于 `2026-08-29T21:15:12Z`，未声明许可证。
- [`a7314fa65717858528d67592c5eaeb38b763183a`](https://github.com/MrSkyce/diy-tamagotchi/commit/a7314fa65717858528d67592c5eaeb38b763183a)，`2026-08-30T16:24:14Z`，约 `+319/-33`、10 个文件，增加疲劳与性格；[`4aa1398f066108a8d35e7d25230146990ef48a49`](https://github.com/MrSkyce/diy-tamagotchi/commit/4aa1398f066108a8d35e7d25230146990ef48a49)，`2026-08-30T18:49:17Z`，约 `+172/-34`、6 个文件，增加持久化生命阶段。
- 用户价值：长期成长有回访动机，但传统饥饿、清洁、疲劳和睡眠会制造维护负担，并与 Momo“低压力 Agent 伙伴”的定位冲突。
- 去重：目标已有任务成果成长、回忆日历和自主行为；再增加独立生命值/性格口径会让相同事件同时驱动多套状态，产生难解释的冲突。
- 结论：不落地；无许可证代码、龙形角色、BMP 素材、阈值和成长公式不得复制。

### 4.4 `H-tech-AFAQ-CEO/Zephyr-LVGL-C-Dev-Needed-for-Smartwatch`：多导航模型实验

- 仓库：[H-tech-AFAQ-CEO/Zephyr-LVGL-C-Dev-Needed-for-Smartwatch](https://github.com/H-tech-AFAQ-CEO/Zephyr-LVGL-C-Dev-Needed-for-Smartwatch)，创建于 `2026-08-30T05:53:36Z`，GitHub 未识别许可证。
- [`013a582a03f27d6c3193188149515d776fb3a9c3`](https://github.com/H-tech-AFAQ-CEO/Zephyr-LVGL-C-Dev-Needed-for-Smartwatch/commit/013a582a03f27d6c3193188149515d776fb3a9c3)，`2026-08-30T04:35:45Z`，约 `+2988/-0`、59 个文件，加入 Cascade、Gallery、Tray、Rail、Conveyor 等导航模型及 native simulator 截图；后续补充测试和 CI。
- 结论：这是通用信息架构研究，不是宠物关系功能。目标当前 Pet 页的高频问题是状态/触摸所有权，不是缺少应用启动器；引入新导航会扩大页面切换和手势冲突，不能单独提升陪伴价值。

### 4.5 `Gangan-307/LCHSP_Watch`：同平台 PAN OTA、照片与音乐

- 仓库：[Gangan-307/LCHSP_Watch](https://github.com/Gangan-307/LCHSP_Watch)，与目标同属 SF32LB52、SiFli SDK、RT-Thread、LVGL，GitHub 未识别许可证。
- [`d45fc89f60adbbad9c3ebafcf213927b467e9d4a`](https://github.com/Gangan-307/LCHSP_Watch/commit/d45fc89f60adbbad9c3ebafcf213927b467e9d4a)，`2026-09-01T14:21:00Z`，约 `+1931/-37`、31 个文件，增加 PAN OTA 示例；随后提交修复音乐封面和照片显示并发布 0.4.0。
- 结论：平台适配度高，但目标已有 OTA 资源更新分支且仍受签名认证、依赖和完整构建门禁阻塞；照片、音乐和 PAN 也不是新的宠物互动。不能用另一个无许可证示例绕过已有安全债务。

### 4.6 `ncr/omarchy-themesync`：桌面主题同步到手表

- 仓库：[ncr/omarchy-themesync](https://github.com/ncr/omarchy-themesync)，MIT。
- [`d3948fdfb957341e4c2aa9d0e86d6487f016ca85`](https://github.com/ncr/omarchy-themesync/commit/d3948fdfb957341e4c2aa9d0e86d6487f016ca85)，`2026-08-28T19:01:23Z`，约 `+184/-42`、6 个文件，实现手表发起配对；窗口内后续加入时间广播、硬件延迟验证和桌面栏组件。
- 结论：主题跟随可提升一致性，但需要新的桌面守护进程、配色协议和全局 UI 主题抽象，且主要是视觉定制，不形成宠物与用户的交互闭环。优先级低于修完天气、语音和 OTA 的既有跨端门禁。

### 4.7 其他相关更新

| 仓库 | 窗口证据 | 结论 |
| --- | --- | --- |
| `shujiCiallo/lvgl_watch` | `2026-08-31` 连续提交，主要是 `ScreenNavigator`、详情页 root、组件抽取和代码规范重构 | 工程重构，无新宠物用户功能；目标已有自己的页面生命周期 |
| `adnanPBI/lvgl-watch-navigation-demo` | [`e6ae35d50f3cd8f92d27af3147c037b51d7dc2f7`](https://github.com/adnanPBI/lvgl-watch-navigation-demo/commit/e6ae35d50f3cd8f92d27af3147c037b51d7dc2f7)，`2026-09-02T13:20:03Z` | 导航演示，不形成宠物闭环；不引入独立导航框架 |
| `zaloeouae-cell/01watch` | 初始开源及 [`718aef5c6469fe90c11e1804fbc2327fc830c23d`](https://github.com/zaloeouae-cell/01watch/commit/718aef5c6469fe90c11e1804fbc2327fc830c23d) OTA 边界加固 | STM32F411/FreeRTOS 通用手表；OTA 与目标既有方向重复 |
| `SantoCovato/Stait-Watch` | `2026-09-02` 主要更新 README、删除意大利语图片/主文件并加 `.gitkeep` | 非实质新增；天气、通知、媒体和来电均需伴侣端且已属既有能力 |
| `chayuto/ESP32-C6-Touch-AMOLED-1.8` | [`ac5d1f95f01e42b74cc58c85d0445563d70cee35`](https://github.com/chayuto/ESP32-C6-Touch-AMOLED-1.8/commit/ac5d1f95f01e42b74cc58c85d0445563d70cee35)，`2026-08-28T00:19:55Z`，为 Govee 增加绝对湿度通风结论 | 不是 PixelPet 更新；天气/环境陪伴已评估且依赖外部传感器 |
| `Haraldon9847/waveshare-watch-rs` | [`8bbe5e0b6199f5506f4f6f1374f5ebead1e304ee`](https://github.com/Haraldon9847/waveshare-watch-rs/commit/8bbe5e0b6199f5506f4f6f1374f5ebead1e304ee)，仅 README | 非功能更新；Rust/no_std 且明确替代 LVGL，技术栈不兼容 |
| `hleserg/Attadipa` | 本窗大量 BLE、Mesh、GNSS、配网、安全和 CI 更新，代表 [`87cb64cb6e06dd0bf419c417c685f0003a31922e`](https://github.com/hleserg/Attadipa/commit/87cb64cb6e06dd0bf419c417c685f0003a31922e) | GPL-3.0、ESP32-S3/FreeRTOS，主要是手表平台与外部无线硬件，不是 Momo 互动 |
| `jainal09/facet-os` | 电量计、锁定、调光和防烧屏验证 | 通用设备可靠性，非宠物功能；部分结论可作工程参考但不立项 |

## 5. 量化排序

评分为 1～5，越高越好；“工作量”高分表示更轻量，“风险”高分表示更可控。许可证分数仅用于界定可借鉴范围。

| 方向 | 原创性 | 用户价值 | 差异化 | 交互闭环 | 硬件适配 | 许可证 | 工作量 | 风险 | 合计/40 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 真实工作证据驱动宠物成长 | 3 | 4 | 1 | 4 | 4 | 5 | 3 | 4 | 28 |
| 设备端 Agent 物理授权门 | 5 | 5 | 5 | 5 | 2 | 1 | 1 | 1 | 25 |
| 持久化生命阶段与性格 | 3 | 3 | 2 | 4 | 4 | 1 | 2 | 2 | 21 |
| 多导航模型 | 3 | 2 | 2 | 2 | 3 | 1 | 2 | 3 | 18 |
| PAN OTA | 2 | 3 | 1 | 3 | 5 | 1 | 2 | 1 | 18 |
| 桌面主题同步 | 3 | 2 | 3 | 2 | 2 | 5 | 1 | 3 | 21 |

分数最高方向也未通过硬门禁：成长方向与已实现任务花园重复；物理授权方向需要协议主版本和安全模型升级。不能因为相对排名而强行产生低质量分支。

## 6. 决策与后续观察

1. 本轮不推荐新功能，不创建 PRD，不启动嵌入式开发。
2. 保留两个未来观察项，但不承诺落地：
   - 若 Agent Pet v2 正式允许设备输入，再单独评审“设备确认/拒绝”的身份、摘要、时效、重放、撤销和审计模型。
   - 若任务花园与回忆日历上线后证明用户仍看不懂成长因果，再基于目标自有素材设计统一成长表现，避免增加第二套积分。
3. 优先完成既有天气陪伴、呼叫 Momo、语音明信片和 OTA 的跨端/安全/真机门禁，价值高于继续增加未闭环的新方向。
4. 本提交只记录扫描证据；不推送远端、不创建 PR、不合并默认分支。
