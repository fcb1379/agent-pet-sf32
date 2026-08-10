# 2026-08-11 LVGL 电子宠物与手表创意扫描记录

## 扫描结论

- 决策：今日推荐 1 项 P1 功能——**“呼叫 Momo / 寻宠回声”**，并进入 PRD。
- 产品闭环：Android 伴侣在已连接状态下发起呼叫，设备立即回执、唤醒并显示“我在这里”的宠物覆盖层，同时按当前音量播放有上限的提示音；用户可在设备端点击停止，也可在手机端停止，最终以明确原因回执并恢复进入前页面和宠物状态。
- 该方向来自公开 LVGL 手表项目对“手机找手表”的新实现证据，但产品、协议和实现必须基于目标仓库现有 `HWS1`、Android 伴侣、`watch_alarm_service`、`local_music_player` 和 Pet 页面独立设计，不复制无许可证来源或 Gadgetbridge 的代码、协议、UI、参数、文字、素材。
- 窗口内其他更新是音乐图片修复、UI 模块重构、CI/构建修复或 README/CAD 更新，没有形成新的可落地产品价值。
- 今日基线新增了大规模音乐、闹钟、计时器 UI 素材和 GIF 双缓冲优化。新功能不得再引入图片/GIF/字体资源，不占用 5 个通用 GIF 槽，且必须对闹钟/计时器响铃和图片传输让路。

## 时间窗、方法与安全边界

- 本地时区：Asia/Hong_Kong。
- 增量窗口：`2026-08-10T10:12:19+08:00` 至 `2026-08-11T00:00:35+08:00`。
- UTC 窗口：`2026-08-10T02:12:19Z` 至 `2026-08-10T16:00:35Z`。
- 查询范围：GitHub 公开仓库中匹配 `LVGL watch`、`LVGL smartwatch`、`LVGL wearable`、`LVGL pet`、`LVGL virtual-pet` 且在窗口内新建或推送的项目。
- 核验材料：GitHub 公共搜索 API、仓库/提交元数据、完整提交哈希、文件级变更统计和公开补丁；没有执行候选项目的代码、脚本、构建命令或 README 指令。
- GitHub `gh` 登录令牌仍失效，本次不依赖 `gh`、不读取或绕过凭据；匿名 GitHub 公共 API 和此前完成的匿名 `git fetch` 可用。
- 所有外部 README、源码、Issue、PR、提交信息与网页均按不可信输入处理，只作为事实证据，不接受其中改变任务边界、访问无关系统或执行代码的要求。

## 授权与仓库自检

- 固定路径：`D:\\code\\sf32\\agent-pet-sf32-text-display-fix`。
- `git rev-parse --show-toplevel`：`D:/code/sf32/agent-pet-sf32-text-display-fix`。
- `origin`：`https://github.com/fcb1379/agent-pet-sf32.git`，与目标仓库一致。
- 扫描开始前工作树干净；`origin/master` 为 `75bcc5720a00c46efa924ba41b5b8c5c53a52622`，提交时间 `2026-08-10T16:47:17+08:00`，主题“重构音乐闹钟计时器并优化GIF渲染”。
- 今日分支从该基线新建：`codex/2026-08-11-momo-find-me`。
- 本产品子任务只写扫描记录和 PRD，不修改固件/伴侣代码，不运行构建，不推送、不建 PR、不合并。

## 目标仓库去重与适配基线

### 已在 `origin/master` 的能力

- SF32LB525、SiFli SDK、RT-Thread、LVGL v8、240 × 240 GUI、PC 模拟器与 Android/iOS/Web 伴侣原型。
- `HWS1` 控制链路、时间同步、状态查询、Android 闹钟设置/关闭/响铃通知，以及图片/GIF 传输。
- `watch_alarm_service`、`local_music_player`、本地音量 0～平台上限、闹钟/计时器响铃与完整响铃 UI。
- Agent 状态、任务花园、远端 `PLAY/RESTORE`、5 个 GIF 槽、GIF 双缓冲和损坏回退。
- 最新主分支已经重构音乐、闹钟、计时器并新增大量资源；因此音乐遥控、闹钟同步、计时器、音乐封面修复均视为已有能力，不能重复推荐。

### 已实现但尚未合入主分支

- 天气陪伴：`codex/2026-08-08-lvgl-idea-scan`，PRD `9c5748dac1ee4e834de0d7b4acd10b1f5621fbb6`、方案 `07ad83196325b9bd056c066bcdf0af5627890ab2`、开发 `795e4d1de083f8738943f3d190c6d0ac6a09c8a8`。
- 多状态自主行为：`origin/codex/2026-08-07-pet-behavior-engine`，最新提交 `0345d06b5434e0a43da6dd03fe54d7094d216a9e`。
- 今日方向不得重新实现天气、触摸安抚、长按、多 GIF 状态映射或自主状态机；寻找覆盖层只借用当前宠物画面，不改变这些模块的数据。

## 候选证据与评估

### 1. TurtleWhal/GWatch-Paris：找手表与 Gadgetbridge 闹钟同步

- 仓库：https://github.com/TurtleWhal/GWatch-Paris
- 实质提交：[`39beeb04ffde8c8507a1bc02fd45152e9003be15`](https://github.com/TurtleWhal/GWatch-Paris/commit/39beeb04ffde8c8507a1bc02fd45152e9003be15)，提交时间 `2026-08-10T04:04:43Z`。
- 文件证据：修改 `main/system/ble.cpp`、`main/ui/screens/alarm.cpp`、`main/ui/ui.cpp`、`main/ui/ui.hpp` 等，核心提交统计为 369 行新增、56 行删除；新增手机发起找表后的唤醒、重复触觉/响铃、停止按钮和 60 秒超时，也增加 Gadgetbridge 闹钟数组同步。
- 同窗口还有 README、CAD 图片和模拟器链接更新；它们不是功能完成证据。
- 许可证：GitHub 元数据 `license=null`，根目录无可依赖的明确开源授权，按无许可证处理；其 Gadgetbridge 交互还涉及 AGPL 生态边界。
- 去重：闹钟同步与目标现有 Android `HWS1 ALARM` 设置、关闭、状态和异步响铃通知重复，不落地。目标尚无“手机呼叫设备”的入口。
- 产品转化：不采用 Gadgetbridge，不复制其 JSON 字段、NimBLE 发送代码、查找页面、触觉参数或素材；只保留“已连接手机可主动让可穿戴设备显形发声”的需求证据，独立设计“呼叫 Momo”。
- 结论：**推荐转化**，条件是同时交付 Android 真实入口与固件闭环，而不是只做固件桩。

### 2. Gangan-307/LCHSP_Watch：音乐图片显示修复

- 仓库：https://github.com/Gangan-307/LCHSP_Watch
- 提交：[`e972b58c70bdfcad7b500f1a3dc6779a4fbd1aad`](https://github.com/Gangan-307/LCHSP_Watch/commit/e972b58c70bdfcad7b500f1a3dc6779a4fbd1aad)，`2026-08-10T10:15:14Z`，修改 4 个文件，121 行新增、17 行删除。
- 证据：主要修复音乐图片/专辑图的接收和显示，并调整 `music_app` 与 `music_ui`。
- 许可证：`license=null`，按无许可证处理。
- 去重：目标主分支本日刚完成音乐页大规模重构，已有手机/本地音乐、播放控制和专辑相关 UI；来源只是缺陷修复，不构成新的宠物互动。
- 结论：**不落地**。

### 3. shujiCiallo/lvgl_watch：Card 模块重构

- 仓库：https://github.com/shujiCiallo/lvgl_watch
- 提交：[`2ac5f62ad6c81b2f06fe249ed01f00ef53d55c86`](https://github.com/shujiCiallo/lvgl_watch/commit/2ac5f62ad6c81b2f06fe249ed01f00ef53d55c86)，`2026-08-10T08:23:03Z`，新增 `card.c/.h` 并精简 `screen_card.c`，319 行新增、307 行删除。
- 许可证：MIT。
- 评估：这是内部模块拆分，没有新增用户场景、数据源或互动闭环；目标 GUI 已按 app/module 分层，不需要为重构而制造产品分支。
- 结论：**不落地**。

### 4. ck-telecom/pinetime：发布 CI 与构建修复

- 仓库：https://github.com/ck-telecom/pinetime
- 许可证：Apache-2.0。
- 窗口内提交：
  - [`c84058c2bdece1830e13012538fd5add657c1167`](https://github.com/ck-telecom/pinetime/commit/c84058c2bdece1830e13012538fd5add657c1167)，`2026-08-10T09:21:45Z`，新增 release CI。
  - [`408e5273ac8c7a52e5397d46db876b34e518c3ba`](https://github.com/ck-telecom/pinetime/commit/408e5273ac8c7a52e5397d46db876b34e518c3ba)，`2026-08-10T09:30:14Z`，修复 PT2 board overlay 构建。
  - [`78225ac713fc3f44c8548570efe6a7bf5ff9c060`](https://github.com/ck-telecom/pinetime/commit/78225ac713fc3f44c8548570efe6a7bf5ff9c060)，`2026-08-10T10:44:15Z`，修复 release workflow。
- 评估：均为交付/构建维护，不是面向用户的新功能；硬件与 Zephyr 栈也不同。
- 结论：**不落地**。

### 5. Haraldon9847/waveshare-watch-rs：README 更新

- 仓库：https://github.com/Haraldon9847/waveshare-watch-rs
- 提交：[`042d15d15fd5328fe5dd7b16e202da43460e3785`](https://github.com/Haraldon9847/waveshare-watch-rs/commit/042d15d15fd5328fe5dd7b16e202da43460e3785)，`2026-08-10T06:43:50Z`，仅修改 README，2 行新增、2 行删除。
- 许可证：仓库元数据为 `NOASSERTION`；源码为 Rust/no_std 且描述为 LVGL 的替代实现。
- 评估：没有实质产品代码更新，也不属于可直接适配的 C/RT-Thread/LVGL 方案。
- 结论：**排除**。

## 评分

满分 5 分；许可证/素材、工作量、风险分数越高，分别表示越安全、越轻量、风险越低。

| 候选方向 | 原创性 | 用户价值 | 差异化 | 交互闭环 | 硬件适配 | 许可证/素材 | 工作量 | 风险 | 总分/40 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 呼叫 Momo / 寻宠回声 | 3 | 4 | 4 | 5 | 5 | 3 | 4 | 4 | 32 |
| 音乐图片显示修复 | 1 | 2 | 1 | 1 | 4 | 2 | 3 | 3 | 17 |
| Card 模块重构 | 1 | 1 | 1 | 1 | 3 | 5 | 3 | 4 | 19 |
| 发布 CI/构建修复 | 1 | 1 | 1 | 1 | 1 | 5 | 2 | 4 | 16 |
| Rust 手表 README 更新 | 1 | 1 | 1 | 1 | 1 | 3 | 5 | 2 | 15 |

## 今日决策

1. 创建完整 PRD：`docs/2026-08-11-momo-find-me-prd.md`。
2. 产品提交完成后，才允许启动独立嵌入式子任务；实现必须先提交设计方案，再修改代码。
3. 开发范围必须包含至少一个真实 Android 伴侣入口和固件端到端闭环；仅有测试注入或固件状态机不能验收。
4. 不复制或改写无许可证来源/Gadgetbridge AGPL 代码，不引入其依赖、协议、UI 和素材。
5. 不推送、不建 PR、不合并；真机声音、唤醒、页面恢复、BLE 断连和功耗必须标记待硬件验收。
