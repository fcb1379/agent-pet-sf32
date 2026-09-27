# 2026-09-28 LVGL 电子宠物/手表创意扫描记录

## 1. 结论与边界

- 严格扫描窗口：`(2026-09-26T16:01:15.950Z, 2026-09-27T16:00:04.228Z]`。
- 评估基线：`origin/master` 的 `a2805abeb72f02a726dce5dc49857280ba9cdc78`。
- 结论：**No-Go**。窗口内有 LVGL 手表、桌面终端和智能面板的实质更新，但没有候选同时通过原创性、用户价值、当前架构适配、许可证、资源可验证性与风险门禁。
- 本次只提交扫描记录；不创建空 PRD，不修改固件，不开发、不编译、不烧录、不推送、不建 PR、不合并。

## 2. 授权与安全自检

- 固定仓库：`D:\code\sf32\agent-pet-sf32-text-display-fix`；`origin` 为 `https://github.com/fcb1379/agent-pet-sf32.git`，与目标一致。
- 已读取仓库 `AGENTS.md`；本次只新增 Markdown，提交信息使用中文。
- GitHub 仓库、README、Issue、PR 和网页仅作不可信证据源；未执行候选仓库中的代码或指令。
- 已验证公开搜索/读取、fetch、本地建分支、写入和提交能力；本次不测试推送权限。

## 3. 搜索与去重

- 使用 GitHub 仓库搜索 `lvgl pushed:2026-09-26..2026-09-27`，并叠加 `watch`、`wearable`、`pet`、`desktop`、`assistant` 等主题词。
- 宽口径 `lvgl` 返回 108 个仓库；`watch` 返回 5 个，`wearable` 返回 1 个，`pet` 返回 0 个。大量通用示例、教学仓库或仅文档更新在初筛排除。
- 对候选核对仓库元数据、许可证、严格窗口内提交时间和补丁摘要，并与既有扫描记录、主线功能和远端功能分支去重。
- 当前项目已有最多 12 会话、静态内存、LVGL 状态 UI 和 v1 只读协议，并已有多状态/多 GIF 行为引擎、Momo Agent Squad、天气陪伴和 OTA/BLE 资源更新方向。

## 4. 候选评估

### 4.1 lvreng/codex-assistant

- 来源：https://github.com/lvreng/codex-assistant
- 窗口证据：创建于 `2026-09-27T13:17:40Z`；提交 `df275d06cf1c`、`f465628f3ff9`、`6c18b3ce0573`、`5d6cddef0f59` 覆盖固件/主机端发布、CJK 字体 CI、MIT 许可及 SD 资源复用、模型动画画廊循环。
- 创意与价值：根据模型显示本地动画，多会话自动轮换，并支持模型控制与用量展示，把 AI 会话抽象成可见角色，形成“状态—角色—会话”反馈闭环。
- 适配与差异：原项目使用 ESP32-P4 + ESP32-C6、Linux/Python 桥接器和 800×480 LVGL 9.2.2 屏幕。目标虽已有会话协议和多状态表现基础，但核心价值依赖协议 v2、上位机模型/标题/用量字段、写控制、安全确认与隐私脱敏，不能直接移植到当前 390 宽屏与 SF32 架构。
- 许可/素材：代码标注 MIT；第三方字体受各自许可证约束，生成式表情/天体素材仍需资产清单，不可直接复制。
- 工作量/风险：中高；风险包括标题和用量隐私、模型切换误操作、协议兼容、上位机联动和资源预算未知。
- 结论：**No-Go**。它是本轮最值得保留的观察项，但主线仍为 v1 只读协议，缺少上位机字段、安全交互与目标硬件资源证据。

### 4.2 charliejgallo/ESP32S3_AmoledOS

- 来源：https://github.com/charliejgallo/ESP32S3_AmoledOS
- 窗口证据：约 27 次实质提交，包括 `40df53b` 离线矢量地图/搜索、`ba03a47` MP3/AAC/HLS 网络电台、`f5bb72f` MJPEG 摄像头，并有 RTSP/H.264 更新。
- 评估：功能完整，但强依赖 ESP32-S3、Wi-Fi、SD、PSRAM、在线服务和特定编解码；虽标注 MIT，仍需核验地图、流媒体和依赖许可证。媒体、带宽、功耗和服务风险均高。
- 结论：**No-Go**。不满足当前硬件资源与架构内快速验证门禁。

### 4.3 Hydr0neFN/NLDeskCYD

- 来源：https://github.com/Hydr0neFN/NLDeskCYD
- 窗口证据：创建于 `2026-09-26T22:03:58Z`；`24909f8` 增加动画天气天空，`fa8c829` 使用 30 FPS `lv_timer` 并增加性能遥测，后续还有太阳/月亮路径、视差、空闲变暗和背光控制。
- 评估：天气氛围和性能遥测有价值，但目标已有天气陪伴方向；候选依赖 Home Assistant/MQTT/Wi-Fi，其开发板测量不能外推到 SF32。代码标注 MIT，素材来源仍需核验。
- 结论：**No-Go**。重复度较高，价值不足以覆盖网络接入和平台验证成本。

### 4.4 Kris-ben/ESP32_Watch

- 来源：https://github.com/Kris-ben/ESP32_Watch
- 窗口证据：`f37b815` 更新横屏 UI；`3f8bc36` 增加离线 RTC 设置、计时器、秒表和待机刷新，补丁约 `+913/-48`。
- 结论：**No-Go**。属于基础手表能力，与当前时钟/计时方向重复，且未发现明确许可证。

### 4.5 Sekiro22/ch32v307vct6-lvgl-watch

- 来源：https://github.com/Sekiro22/ch32v307vct6-lvgl-watch
- 窗口证据：`0d7890e` 约 `+565/-52`，覆盖按键、事件队列和 RTC 表盘；README 给出约 236896 字节 text、53136 字节 bss，并称剩余 Flash 约 24 KB。
- 结论：**No-Go**。功能常规，CH32V307 的资源、驱动和工具链数据不能证明 SF32 适配，且未发现明确许可证。

## 5. 其他排除项

- `BaselYAS/esp32-p4-ha-keypad`、`GalusPeres/HomeTiles`、`Axellum/M5-Tab5-Home-Assistant-Control`：Home Assistant/MQTT/以太网或摄像头墙面板，依赖 480×800/1280×720 大屏平台，超出宠物核心场景。
- `domgrimm/tab5-devos`：MIT 的 1280×720 cyberdeck/OTA 方向，但 OTA 缺少完整镜像身份与回滚门禁；目标已有 OTA/BLE 资源更新分支。
- `kwinter/ESP32LVGL` 主要为视频/示例集合；`RTechAI/Dungeon-Escape` 严格窗口内仅文档图片更新。
- `posfilu/c3-weather-clock`、`Haraldon/watch-rs` 窗口内主要为骨架、ADR、CI 或 README 更新。
- `Jepl4r/sonix-player`、`bitcoin3us/blocktv` 需要新增网络、账号/API 和隐私边界，并与现有本地音乐或会话状态能力部分重复。

## 6. 后续触发条件

`codex-assistant` 的“模型人格化动画”只有同时满足以下条件才建议重新立项：

1. 上位机协议向后兼容地提供稳定 `model_id`、脱敏标题和可选用量字段；
2. 模型切换写操作具备确认、权限校验、失败回退和审计；
3. 在目标硬件完成 GIF/动画的 Flash、RAM、解码峰值、帧率和功耗测量；
4. 素材来源、字体许可证和分发清单完整；
5. 与 Momo Agent Squad、行为引擎和多 GIF 表情划清边界，避免重复状态机。

条件具备前维持 No-Go，不建立 PRD 或开发分支。
