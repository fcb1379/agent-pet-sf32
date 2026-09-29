# 2026-09-30 LVGL 电子宠物/手表创意扫描记录

## 1. 结论与边界

- 严格扫描窗口：`(2026-09-28T16:00:45.689Z, 2026-09-29T16:01:12.870Z]`。
- 评估基线：`origin/master` 的 `a2805abeb72f02a726dce5dc49857280ba9cdc78`。
- 结论：**No-Go**。窗口内存在倾斜小游戏、儿童学习游戏、动态表盘工具和 AI 设备模拟器等实质更新，但没有候选同时通过差异化、目标硬件适配、许可证/素材、资源预算和一天内可验证闭环门禁。
- 本次只提交扫描记录；不创建空 PRD，不启动开发、不编译、不烧录、不推送、不建 PR、不合并。

## 2. 授权与安全自检

- 固定仓库：`D:\code\sf32\agent-pet-sf32-text-display-fix`；`origin` 为 `https://github.com/fcb1379/agent-pet-sf32.git`，与目标一致。
- 已读取根目录 `AGENTS.md`；本次仅新增 Markdown，提交信息使用中文。
- 已执行 `git fetch --prune origin`，本地工作区起始干净，并以最新 `origin/master` 创建本次分支。
- GitHub 仓库、README、Issue、PR 和网页均按不可信输入处理；只读取公开元数据和提交证据，未执行任何候选代码、脚本或指令。

## 3. 搜索与去重

- 使用 GitHub 公共仓库搜索严格筛选 `lvgl pushed:2026-09-28T16:00:46Z..2026-09-29T16:01:12Z`，返回约 80 个活跃仓库；再按 `watch`、`wearable`、`pet`、`assistant`、`game`、`desktop` 等场景词筛选。
- 对候选核对创建/推送时间、许可证元数据、项目描述和窗口内实质更新，并与前一日扫描及目标已有多 GIF、多状态行为、IMU 交互、天气陪伴、语音、Momo Agent Squad 等能力去重。
- 通用模板、工业仪表、Home Assistant 大屏、仅 README/依赖更新，以及显著依赖大屏、Wi-Fi、PSRAM 或特定外设的项目在初筛排除。

## 4. 重点候选评估

### 4.1 Sethyrus/ESP32Watch-Maze

- 来源：https://github.com/Sethyrus/ESP32Watch-Maze
- 窗口证据：仓库创建于 `2026-09-28T20:31:00Z`，并在窗口内持续更新；项目描述明确为 Waveshare ESP32-S3 AMOLED 手表上的 IMU 倾斜迷宫，许可证为 MIT。
- 创意与价值：用短局迷宫把姿态输入转为即时反馈，交互闭环清楚，适合可穿戴设备的碎片时间。
- 差异与适配：前一日已评估 `dreamiurg/porthole` 的多款倾斜小游戏；目标项目也已有 LSM6 IMU 行为输入和玩球/木鱼等交互。候选依赖 ESP32-S3、特定 AMOLED 手表和其 IMU 标定，不能证明 SF32、240×240、52 Hz 采样下的精度、帧率、功耗和误触表现。
- 工作量/风险：中高；需重做关卡、输入标定、状态优先级、退出机制和真机性能测试，并避免争用宠物 GIF/LVGL 生命周期。
- 结论：**No-Go**。产品方向与既有倾斜游戏观察项重复，缺少目标硬件新增证据。

### 4.2 liangdabiao/ai-passport-sanzijing

- 来源：https://github.com/liangdabiao/ai-passport-sanzijing
- 窗口证据：仓库在窗口内创建并更新，许可证为 MIT；项目描述为面向 AI Passport 的《三字经》儿童学习游戏，包含三档自带 LVGL 中文点阵字库。
- 创意与价值：通过短时学习任务、反馈和可控字库形成亲子陪伴闭环，中文资源离线可用。
- 差异与适配：目标是电子宠物而非儿童学习机，当前没有年龄分级、内容运营、家长控制或课程进度协议；引入大规模中文字库还会改变 Flash 预算。内容版权、文本版本和未成年人体验需独立审查，不能仅凭代码 MIT 视为全部素材可分发。
- 工作量/风险：高；产品范围、内容合规、字库资源和上位机管理均未闭环。
- 结论：**No-Go**。用户价值场景偏离当前宠物主线，且内容与资源门禁不完整。

### 4.3 AIR-pixel/miband11-watchface-maker

- 来源：https://github.com/AIR-pixel/miband11-watchface-maker
- 窗口证据：仓库在窗口内创建并更新，许可证为 MIT；项目描述为小米手环动态表盘生成工具，支持帧数、色深、编码调整及实时体积估算。
- 创意与价值：资产制作阶段即时显示体积，可帮助多 GIF 资源在传输前满足设备预算。
- 差异与适配：它是针对逆向 `.face` 格式的制作工具，不是可直接移植的宠物功能；目标已有上位机多 GIF 传输与设备资源协议，真正落地需要目标格式、解码峰值、槽位配额、传输失败回滚和许可证清单的联合设计。
- 工作量/风险：中高；格式和工具链不兼容，逆向格式还带来维护与分发边界。
- 结论：**No-Go**。可保留“上传前预算预估”原则，但本轮不足以创建固件 PRD。

### 4.4 BlackHole1/passportsim

- 来源：https://github.com/BlackHole1/passportsim
- 窗口证据：仓库在窗口内创建并实质更新，许可证为 MIT；项目描述为运行未修改 ESP-IDF 镜像的浏览器/桌面模拟器，并提供 CLI 与 MCP 接口。
- 创意与价值：可提升固件回归和交互自动化效率，但属于开发基础设施而非宠物用户功能。
- 差异与适配：目标为 SF32/RT-Thread，候选聚焦 ESP32-C3/ESP-IDF，二进制执行模型、外设仿真和工具链均不兼容。
- 结论：**No-Go**。不能把 ESP32 模拟器证据外推为当前项目可用的模拟器能力。

### 4.5 其他排除项

- `hzn29/01watch`、`Kris-ben/ESP32_Watch`、`charliejgallo/ESP32S3_AmoledOS`：手表能力或 ESP32 平台更新与既有观察项重叠，未提供新的 SF32 适配证据。
- `chen9751/esp32-ai-robot`：待机页与转场方向此前已评估，且未声明明确许可证。
- `MichalZaniewicz/*`、`Axellum/M5-Tab5-ESPHome-LVGL`、`MaxGramser/homeassistant_espscreen`：依赖 Home Assistant、Wi-Fi、ESPHome 或大屏平台，不符合当前 BLE/小屏架构。
- `whatshu/loading_anime`、`diankao/lvgl-web-bridge`、`IWILLTBEST/eez-studio-mcp`：属于资源/开发工具，不能直接形成宠物端用户闭环。

## 5. 后续触发条件

若重新考虑倾斜小游戏或多 GIF 预算工具，必须先满足：

1. 在目标 LSM6、52 Hz 和真实外壳上采集姿态样本，给出误触、漏检、延迟、帧率与稳定功耗数据；
2. 明确与现有玩球、木鱼、抚摸、睡眠、语音及 GIF 状态机的优先级和安全退出；
3. 上位机提供目标格式的单文件/总槽位 Flash 预算、解码 RAM 峰值和上传前校验，并验证失败回滚；
4. 资产、字体和内容许可证形成可审计清单；来源项目只作产品启发，不复制不兼容实现或素材。

条件具备前维持 No-Go，不创建 PRD 或启动嵌入式开发子任务。
