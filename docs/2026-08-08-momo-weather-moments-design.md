# Momo Weather Moments 固件实现方案

## 1. 文档与基线

- 日期：2026-08-08
- 分支：`codex/2026-08-08-lvgl-idea-scan`
- 产品提交：`9c5748dac1ee4e834de0d7b4acd10b1f5621fbb6`
- 代码基线：`origin/master@692a9fa7cb82aa627c57c08f8a5ddbd1d974349f`
- 目标：只实现固件侧最小闭环；不实现天气 provider、位置权限、API key 或固件公网访问。
- 版权边界：候选仓库未声明许可证。本实现仅采用 PRD 描述的产品问题，协议、状态机、代码、布局、色彩和参数均在本仓独立设计，不下载、复制或改写候选实现。

## 2. 总体架构

```text
手机/桌面伴侣
  │ 现有 Agent Pet 状态特征，固定 20 B、CRC-8/ATM
  ▼
GATT 写回调
  │ RT-Thread 极短临界区；只校验并发布固定快照
  ▼
agent_pet_protocol ──► agent_pet_weather（纯 C 状态与纯逻辑）
  │                         │
  │                         ├─ 序列去重/回绕
  │                         ├─ UTC/单调时钟 TTL
  │                         └─ 显示优先级判定
  ▼
AGENTPETBLE_GetStatus（临界区内复制）
  │
  ▼
宠物页现有 100 ms LVGL timer（唯一 UI 消费者）
  ├─ 高优先级状态：隐藏并暂停天气层
  └─ 可显示：基本对象氛围 + 一次点击照顾反馈
```

不新增 GATT service、characteristic、UUID、后台线程、动态内存、Flash 持久化或天气专用 LVGL timer。天气层挂在宠物页根对象下，跟随页面统一销毁；动画只在现有 100 ms 刷新周期更新，目标 10 FPS。

## 3. 协议与版本

复用现有 `AP` 20 字节帧和状态特征。帧头版本继续为 `1`，新增消息类型 `5` 表示 Weather Snapshot v1。天气帧必须是单分片：

| 偏移 | 长度 | 字段 | 规则 |
|---:|---:|---|---|
| 0 | 2 | magic | 固定 `0x41, 0x50` |
| 2 | 1 | schema/version | 固定 `1` |
| 3 | 1 | message_type | 固定 `5` |
| 4 | 2 | sequence | little-endian；16 位串行号 |
| 6 | 1 | chunk_index | 固定 `0` |
| 7 | 1 | chunk_count | 固定 `1` |
| 8 | 1 | payload_length | 固定 `10` |
| 9 | 1 | condition | `0..5`：UNKNOWN/CLEAR/CLOUDY/RAIN/SNOW/STORM |
| 10 | 2 | temperature_deci_c | 有符号 little-endian，`-500..600` |
| 12 | 4 | observed_utc | little-endian Unix 秒，`2020-01-01..2037-12-31` |
| 16 | 2 | ttl_minutes | little-endian，`15..360` |
| 18 | 1 | flags | bit0 HOT、bit1 COLD；未知位或 HOT+COLD 同时置位拒绝 |
| 19 | 1 | crc8 | 对字节 `0..18` 做 CRC-8/ATM |

帧头中的版本和序列即天气 schema 版本与幂等序列，不在 10 字节 payload 重复传输。这样单个快照仍为 20 B，不改变现有 MTU 假设，也不占用或重定义 5 个 GIF 槽位。

关闭编译期开关时，消息类型 5 继续按未知头拒绝；原有消息类型 1..4、图片传输与 GIF 槽位二进制路径不变。

## 4. 模块与接口

### 4.1 `agent_pet_weather.h/.c`

纯 C、固定内存、无 RT-Thread/LVGL/文件系统依赖，供固件与主机测试共用：

- `AGENTPETWEATHER_Init()`：清空 RAM 快照、交互与诊断；重启后不恢复旧天气。
- `AGENTPETWEATHER_ProcessPayload(sequence, payload, length, received_monotonic_tick)`：严格校验、去重、旧序列拒绝并原子替换候选快照。
- `AGENTPETWEATHER_GetSnapshot()`：复制已发布快照、代数和诊断。
- `AGENTPETWEATHER_EvaluateFreshness(snapshot, now_utc, now_tick, ticks_per_second, rtc_valid)`：纯逻辑判断有效/过期及时间来源。
- `AGENTPETWEATHER_IsSequenceNewer(candidate, reference)`：RFC 1982 风格半区间比较。
- `AGENTPETWEATHER_SelectPresentation(input)`：纯逻辑优先级，返回 HIDDEN/AMBIENCE/INTERACTION。
- `AGENTPETWEATHER_ClaimInteraction(sequence, now_tick, ticks_per_second)`：同一序列最多一次，并执行 3 小时单调时钟冷却；只更新本地天气交互，不触碰 Agent、任务花园或 merit。

### 4.2 `agent_pet_protocol`

- 新增消息类型 5 和结果/错误码。
- 新增带接收时刻的 `AGENTPET_ProcessFrameAt(..., received_monotonic_tick)`；原 `AGENTPET_ProcessFrame()` 保持 ABI，并以 0 作为确定性时间调用新入口。
- `AGENTPET_ProtocolInit()` 同时初始化天气模块。
- 所有天气处理沿用 GATT 写回调的 RT-Thread 临界区，不在 BLE 上下文操作 LVGL、网络或 Flash。

### 4.3 `agent_pet_ble_service`

- GATT 写入时传入原始 `rt_tick_get()`；所有间隔先按 `rt_tick_t` 模数做无符号减法，再结合 `RT_TICK_PER_SECOND` 比较，禁止先除法后相减。
- `AGENTPET_BLE_STATUS` 增加天气只读副本；`AGENTPETBLE_GetStatus()` 在同一临界区复制，避免 GUI 看到撕裂结构。
- 不新增 worker。现有时间同步 worker只负责 RTC；天气帧无需阻塞操作。

### 4.4 宠物页与 PC simulator

- 宠物页创建最多 7 个基本对象：天气容器、3 个粒子、光晕/云/遮罩主体、短提示标签；全部为固定对象，按 condition 改样式和位置。
- 复用 `PET_STATUS_REFRESH_MS=100 ms`，不新增 timer；粒子每次最多移动一次，约 10 FPS。
- 点击先经天气交互判定；仅当当前天气展示层可交互且领取成功时播放一次本地反馈，否则沿用原木鱼点击路径。
- PC simulator 由服务桩按固定脚本轮换 CLEAR/CLOUDY/RAIN/SNOW/HOT/COLD/过期/抢占，用于人工冒烟；协议、TTL 和优先级的确定性验证放在主机单元测试，不依赖墙钟。

## 5. RT-Thread、BLE 与 LVGL 约束

1. GATT 回调处于 BLE 服务上下文：只做 20 B 校验、固定结构复制和计数；不得调用 LVGL、文件系统、网络、天气 provider 或持久化。
2. 协议状态只在调用者持有 `rt_enter_critical()` 时写入；`AGENTPETBLE_GetStatus()` 在同一保护下复制。临界区内无阻塞、无日志格式化、无动态分配。
3. LVGL 对象创建、样式、动画、隐藏、删除只在 GUI 线程中的页面 start/refresh/stop 执行。
4. 页面退出时先停止现有 status timer，再删除 root；天气对象是 root 子对象，不单独残留 timer 或动画。
5. 远端 GIF/打字/Agent 抢占只隐藏天气层并停止位置更新，RAM 快照仍保留；恢复时按最新时间重新求值，不补播错过的交互。
6. 保存原始 RT tick，先用无符号减法计算间隔，再换算时间；TTL 与 3 小时冷却均短于 32 位 tick 模数窗口，支持 tick 回绕且不比较绝对 tick 大小。

## 6. 天气状态机与优先级

天气数据状态：

```text
EMPTY --合法新序列--> FRESH --TTL 到期/单调上限到期--> EXPIRED
  ^                         │
  └---- 重启/功能关闭 -------┘
FRESH --合法更新序列--> FRESH（替换，不排队）
重复序列：DUPLICATE；旧序列/半区间歧义：STALE；均不改变快照
```

显示状态按以下输入求值，优先级从高到低：

1. 功能关闭、无快照或过期：`HIDDEN`。
2. 图片正在传输：`HIDDEN`。
3. Agent `NEEDS_INPUT/ERROR`：`HIDDEN`。
4. 打字动画或远端 `PLAY(slot)`：`HIDDEN`。
5. Agent `RUNNING/COMPLETED` 或任务花园待领取提示：`HIDDEN`。
6. 已领取且反馈窗口尚未结束：`INTERACTION_FEEDBACK`。
7. 其余有效天气：`AMBIENCE`。

天气不会覆盖状态/任务文字，不改变 `ucRequestedImageSlot`，不调用 `PLAY/RESTORE`。

## 7. 校验、序列、TTL 与错误处理

- 所有字段先解析到栈上候选结构，全部通过后才复制到发布区；错误不改变旧状态。
- 长度、版本、消息类型、分片、CRC、枚举、温度、UTC、TTL、flags 任一失败均返回明确错误并增加拒绝计数。
- 序列比较：`delta=(uint16_t)(candidate-reference)`；`delta==0` 为重复，`0<delta<0x8000` 为新序列，其他为旧/歧义。`65535→0` 合法，跨度正好 `0x8000` 拒绝。
- RTC 有效：设备 UTC=`time(NULL)-timezone_offset`；若当前 UTC 不早于观测时间，则有效期为 `observed_utc + ttl_minutes*60`。加法用 64 位防溢出。
- RTC 未同步、读取失败或回拨到观测时间之前：使用接收单调时钟，寿命为 `min(ttl_minutes,60)*60` 秒，绝不因 RTC 回拨延长到 60 分钟以上。
- 快照一旦求值为过期即隐藏；100 ms 刷新保证远小于 PRD 的 10 分钟回退要求。
- 重启不持久化；BLE 断开不清除最后快照，但仍按 TTL 到期。伴侣无数据时不伪造 UNKNOWN。

## 8. 交互闭环

- 只有 CLEAR/CLOUDY/RAIN/SNOW 以及 HOT/COLD flags 可领取一次照顾；STORM/UNKNOWN 只显示中性或隐藏，不制造惊吓。
- 同一 sequence 领取一次后永久标记为已领取，重复点击走原有木鱼行为，不重复天气反馈。
- 全局 3 小时冷却由原始单调 tick 差控制；冷却期间新天气仍可显示氛围，但不给新的照顾提示。
- 天气反馈只改变天气对象颜色/透明度/短提示，约 1.5 秒后恢复氛围；不调用 `QUESTGARDEN_*`、`AGENTPETMERIT_*`、Agent 协议或远端通知。

## 9. 固定内存与性能预算

| 项目 | 设计上限 |
|---|---:|
| 天气协议/发布状态 | 128 B BSS 以内 |
| UI 天气字段 | 96 B BSS 以内（不含 LVGL 内部对象元数据） |
| 新增 LVGL 对象 | 最多 7 个 |
| 新增 timer/thread/mailbox | 0 |
| 新增图片/GIF/字体/音频 | 0 B |
| 新增堆调用 | 0 次（业务代码不调用 malloc/free） |
| 新增 Flash 目标 | 小于 20 KiB，以最终 `main.elf` 差值实测 |
| 新增 RAM 峰值目标 | 小于 2 KiB，以 `.data+.bss` 差值和对象计数复核 |
| 天气刷新 | 10 FPS，复用 100 ms timer；抢占时 0 次天气位置更新 |

LVGL 本身创建对象会使用框架内存池；业务层不自行动态分配。对象数量和页面销毁路径通过模拟器反复进入/退出与 30 分钟运行验证。

## 10. 编译期开关与回退

- 新增 Kconfig：`AGENT_PET_WEATHER_MOMENTS`，默认开启；在 `proj.conf` 明确设置。
- `SConscript` 仅在开关开启时编译天气模块；协议和 UI 代码用同一宏隔离。
- PC simulator 默认开启以提供确定性演示；可在 `pc_hcpu/proj.conf` 关闭验证旧行为。
- 关闭时不接受天气消息、不创建天气对象、不保存天气状态，现有 Agent/GIF/木鱼/任务花园路径保持不变。

## 11. 测试矩阵

### 主机纯 C

- 6 个枚举逐一接受；枚举越界拒绝。
- 温度 `-500/600` 接受，`-501/601` 拒绝；TTL `15/360` 接受，`14/361` 拒绝。
- NULL、长度错误、未知版本、错误消息结构、CRC 损坏、未知 flags、HOT+COLD 拒绝且旧快照不变。
- 同一序列连续 100 次仅发布一次；旧序列拒绝。
- `65534→65535→0→1` 接受；`delta=0x8000` 和反向旧序列拒绝。
- RTC 正常、刚好到期、RTC 未同步、RTC 回拨、单调计数回绕、重启无状态。
- 天气展示优先级至少 20 组组合，覆盖 PLAY/RESTORE、typing、NEEDS_INPUT、ERROR、RUNNING、COMPLETED、图片传输和默认待机。
- 同序列交互一次、3 小时冷却边界、冷却后新序列可领取。

### 现有回归

- Agent Pet 协议主机测试（包括动画、CRC、快照、时间同步）。
- GIF 5 槽上传、选择、摘要和 v1 slot 0 测试。
- 任务花园、watch protocol、行为相关测试（仓库存在时全部执行）。
- `build-flash-utils.test.ps1`。

### 构建与模拟器

- PC 完整模拟器：`simulator/build.bat`，启动 `project/build_pc_hcpu/main.exe` 冒烟；确定性天气轮换和抢占可见。
- 目标板完整构建：仓库根目录 `powershell -File .\build.ps1`，板型 `sf32lb52-lchspi-ulp`。
- 记录实际 `arm-none-eabi-size main.elf` 的 text/data/bss，以及相对基线差值、全部新增/已有警告。

## 12. 真机验证项

以下项目必须标记“待硬件验收”，未连接真机不得宣称通过：

1. BLE 写入合法/损坏/重复/乱序天气帧，抓包确认 20 B 且无位置、城市、账号、SSID、key 或 provider 文案。
2. BLE 断线/重连及 RTC 同步前后 TTL 行为；RTC 回拨不延长陈旧天气。
3. 宠物页反复进入退出 100 次，对象、timer、heap 和 stack 水位无持续下降。
4. CLEAR/CLOUDY/RAIN/SNOW/HOT/COLD 的触摸命中、一次领取和反馈时长。
5. 与 PLAY/RESTORE、typing、NEEDS_INPUT、ERROR、RUNNING、COMPLETED 交错至少 20 组，抢占即时且恢复正确。
6. GIF 五槽上传/播放时天气暂停，不出现花屏、残影或刷新率明显下降。
7. 连续运行 30 分钟，目标天气层约 10 FPS，无看门狗复位、崩溃或明显功耗异常。

## 13. 实施顺序

1. 先提交本文作为实现门禁。
2. 实现纯 C 天气模块与协议接入，完成主机测试。
3. 接入 BLE 线程安全复制和 PC 确定性注入。
4. 接入宠物页氛围、优先级和一次照顾反馈。
5. 执行全部回归、模拟器和目标固件构建，记录资源数据与待硬件项。
6. 单独提交代码、测试与验证记录；不推送、不建 PR、不合并默认分支。
