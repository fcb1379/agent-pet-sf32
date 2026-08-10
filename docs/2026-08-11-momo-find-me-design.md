# “呼叫 Momo / 寻宠回声”嵌入式实现方案

## 1. 文档信息与阶段门禁

- 日期：2026-08-11。
- 产品提交：`018dff56001e5eccd92fde7a3381aa791aa90a27`。
- 开发分支：`codex/2026-08-11-momo-find-me`。
- 基线：`origin/master@75bcc5720a00c46efa924ba41b5b8c5c53a52622`。
- 本文必须独立提交后才能修改实现代码。
- 来源仓库与 Gadgetbridge 仅用于产品阶段确认“手机呼叫已连接穿戴设备”的需求事实；实现不读取、不复制其代码、协议、字段、界面、文案、素材或依赖。

## 2. 结论与产品偏差

端到端 MVP 可在现有架构内安全实现，但声音部分必须降级为纯视觉：当前 `local_music_player` 与每日闹钟、计时器共享播放队列和播放器，缺少“短提示音会话所有权”、可取消的 effect 句柄以及仅恢复本功能暂停内容的能力。强行播放或停止会破坏普通音乐队列，或误停正式闹钟。MVP 因此不调用 `local_music_play_file()`、`LOCALMUSIC_PlayEffect()`、`local_music_stop()`，不修改本地/蓝牙音量。Android、协议、60 秒状态机、屏幕唤醒、跨页面全局覆盖层、双端停止、状态恢复、正式闹钟抢占和图片传输 BUSY 均按 PRD 实现。

这是一项明确且可验证的 PRD 偏差：音量大于 0 时也只显示视觉提示。`watch_settings_get_local_volume()` 只用于上报 `audio=VISUAL`/`muted` 诊断，不改变设备配置。未来只有在音频服务提供可取消、带所有权、不会抢占正式提醒的 effect API 后才允许启用声音。

此外，本次只能作为内部验证 MVP，不能直接按量产安全等级发布。现有
`HWS1` characteristic 配置为 `NOAUTH | MULTI_LINK`，仓库协议文档也明确规定未来
特权操作应使用已认证/已绑定会话和新的 v2 capability。呼叫导致亮屏，属于可能被
附近设备滥用的骚扰面；在用户要求“不新增 UUID、复用 HWS1”的范围内无法同时完成
量产级认证迁移。本次通过纯视觉、单会话、60 秒硬上限、Android 防抖和设备端成功
会话结束后 1 秒冷却降低风险。正式发布或启用声音前必须单独设计认证的 HWS2/v2
能力，并拒绝未绑定连接；不能通过改用同样 NOAUTH 的 Agent Pet 服务规避。

## 3. 现有架构和真实接口

| 领域 | 现有模块/接口 | 本次用法 |
|---|---|---|
| HWS1 | `watch_protocol_handle_request()` | 扩展 `FIND|START`、`FIND|STOP`、`FIND|STATUS`；保留 63 字节控制帧上限 |
| BLE 控制 | `ble_link_gatts_set_cbk()`、`ble_link_notify_event()` | 写回调只解析、复制固定状态、返回短响应；异步结束事件由现有 `blelink` 线程发送 |
| BLE 工作线程 | `g_ble_link_env.mb_handle`、`ble_link_thread()` | 复用既有 mailbox/线程处理 GUI 唤醒和结束通知；不新增线程 |
| GUI 主线程 | `app_watch_entry()`、`lv_timer_handler()` | 在既有循环调用一次非阻塞 `MOMOFINDGUI_Poll()`；不新增常驻 LVGL timer |
| 顶层显示 | `lv_layer_top()`、`lv_disp_trig_activity()` | 创建本模块独占的全屏覆盖层并维持屏幕活动；不切换 app、不销毁底页 |
| 电源管理 | `gui_pm_fsm(GUI_PM_ACTION_WAKEUP)` | 仅由既有 `blelink` 线程调用；BLE 写回调只投递 mailbox 消息 |
| 正式提醒 | `watch_alarm_get_snapshot()` | 新增无阻塞 `watch_alarm_is_ringing()` 快照，覆盖 alarm/timer ringing |
| WFPUSH2 图片 | `badge_transfer` | 新增不访问文件系统的 `badge_transfer_is_busy()`；RECEIVING 时拒绝 START |
| Pet GIF/图片 | `AGENTPETIMAGE_QueueFrame()`、`AGENTPETIMAGE_GetStatus()` | 增加有界 pending 计数与 `AGENTPETIMAGE_IsTransferBusy()`，消除 BEGIN 已入队但 worker 尚未发布状态的竞态 |
| 设置 | `watch_settings_get_local_volume()` | 只判断静音诊断，不修改音量 |
| Android | 原生 Java `MainActivity`、`WatchBleClient`、`WatchProtocol` | 增加真实按钮、能力解析、START/STOP/STATUS 和异步结束事件；不加后台常驻服务 |
| PC 模拟器 | `pc_simulator_services.c` | 为新增非阻塞服务查询和 PM 请求补桩，复用同一状态机与覆盖层代码 |

## 4. 模块架构

### 4.1 `momo_find_me.c/.h`：固定内存状态机

新增纯状态机模块，放在 `src/app_utils`，目标与 PC 均编译。模块只保存一个固定会话：

- 状态：`IDLE`、`ACTIVE`、`STOPPING`。
- 会话 ID：接受 START 时的 HWS1 request-id。
- 当前 ACTIVE START ID 和上一个已完成 START ID，用于精确识别重复与迟到包；
  HWS1 request-id 只是 outstanding correlation ID，不把它误当全局有序序列。
  断连/重连不终止已接受会话。
- 原始 `rt_tick_t` 开始 tick 和固定 60 秒 deadline，不使用 RTC。
- 上一次成功会话结束 tick；未满 1 秒的新 START 返回 `RATE_LIMIT`，防止未认证
  控制面被高频触发。主机连续 50 次测试会显式推进单调 tick。
- 结束原因：`USER_STOP`、`PHONE_STOP`、`TIMEOUT`、`PREEMPTED_ALARM`。
- GUI 是否已创建/清理、唤醒请求、待发送结束事件。
- 饱和诊断计数器：接受、重复、乱序、BUSY、超时和抢占次数。

状态读写只在极短的 `rt_hw_interrupt_disable()` 临界区复制小结构，不等待 mutex、不访问 Flash/文件系统、不调用 LVGL/音频/BLE。主机测试提供等价的临界区桩。

### 4.2 `momo_find_me_ui.c/.h`：GUI 线程适配

该模块只被 `app_watch_entry()` 的 GUI 线程调用：

1. 每次循环先以 tick 节流到最多 10 Hz。
2. 读取正式闹钟/计时器的无阻塞 ringing 快照，并驱动状态机超时/抢占。
3. ACTIVE 且 UI 未创建时，在 `lv_layer_top()` 创建全屏覆盖层；覆盖层使用仓库已有 `agent_pet_mascot`，以及简单色块、呼吸动画和短中文提示，不增加素材、不操作 GIF 槽。
4. ACTIVE 时调用 `lv_disp_trig_activity(NULL)`，维持当前 GUI 会话亮屏。
5. STOPPING 时只删除本模块持有且仍有效的根对象，停止本模块动画，然后确认清理；底层应用对象、页面栈、Pet GIF/Agent/天气/自主行为数据从未被修改，因此自然恢复。
6. 点击宠物或停止热区只调用状态机本地 STOP；事件被顶层对象消费，不落到底页。

不缓存底页 `lv_obj_t *`，不调用 `gui_app_run()` 或 `gui_app_goback()`，因此不存在失效页面引用恢复问题。`lv_layer_top()` 跨 app 页面存在；若系统清理了本模块对象，轮询检测无效后可在 ACTIVE 状态重建一次。

### 4.3 BLE 线程适配

当前 `ble_link_gatts_set_cbk()` 在 GATT 回调的 481 行同步调用
`ble_link_handle_command()`；现有控制命令并没有异步投递。本次不改变所有 HWS1
命令的调度模型，而是只为 FIND 扩展既有 `blelink` mailbox 消息类型：

- START 状态机暂时接受后，`watch_protocol` 在形成 ACCEPTED 响应前调用
  `ble_link_request_find_wakeup()`，以 `rt_mb_send()` 非阻塞投递
  `BLE_LINK_MSG_FIND_WAKE`。只有投递成功才确认会话和返回 ACCEPTED；投递失败会
  回滚尚未显示的会话并返回既有 `ERR|4`，不能出现“手机显示已接受、设备不会醒”的
  不一致。
- GUI 完成结束清理：以固定事件槽记录 session/reason，再投递 `BLE_LINK_MSG_FIND_NOTIFY`。

`ble_link_thread()` 收到 WAKE 后，在非 BLE 回调上下文调用
`gui_pm_fsm(GUI_PM_ACTION_WAKEUP)`；无 PM 配置时为空操作。该 PM API 内部用
`s_gui_ctx.lock` 串行状态转换，并通过 semaphore 唤醒 GUI；SDK 自身既在物理按键
回调中调用它，也在 `sys_power_on()` 的系统电源管理上下文调用它，因此它不是 LVGL
对象 API，允许从非 GUI 线程调用。所有 `lv_obj_*` 操作仍严格留在 `app_watch` GUI
线程。收到 NOTIFY 后，`blelink` 线程发送异步 HWS1 结束事件。

结束事件 mailbox 满时不阻塞，固定事件槽保持 pending，由 GUI 后续轮询重试；断连
后允许明确丢弃。这样既不在 GUI 线程等待 BLE，也不会因暂时满队列重复结束事件。
START 唤醒消息可能在 ACCEPTED notification 真正到达手机前被 worker 调度，这是
BLE notification 与 RTOS worker 的正常竞态；协议只保证响应由同一请求生成、结束事件
一定在 GUI 清理之后，不宣称屏幕视觉严格晚于手机收到 ACK。

## 5. HWS1 协议

### 5.1 能力协商与兼容性

不直接在 HELLO 的 `cap` 末尾追加 `FIND`。现有最坏 HELLO 响应长度为 60 字节，追加后为 65 字节，会违反 63 字节限制。

改为在现有 `STATE` 响应追加 `find=1`：

```text
HWS1|<id>|OK|time=YYYYMMDDTHHMMSS;tz=<minutes>;img=<0|1>;find=1
```

最坏响应为 55 字节。旧 Android 会忽略未知键；新 Android 在旧固件收不到 `find=1`，按钮保持隐藏/禁用。新 Android 每次连接初始化和重连都先查询 STATE，再查询 FIND STATUS。

### 5.2 请求

```text
HWS1|<id>|FIND|START
HWS1|<id>|FIND|STOP
HWS1|<id>|FIND|STATUS
```

响应全部沿用 HWS1 OK/ERR 外壳，业务结果用短键值表达：

```text
HWS1|<id>|OK|r=ACCEPTED;s=ACTIVE;id=<session>;left=60
HWS1|<id>|OK|r=ALREADY;s=ACTIVE;id=<session>;left=<0..60>
HWS1|<id>|OK|r=BUSY_ALARM;s=IDLE
HWS1|<id>|OK|r=BUSY_TRANSFER;s=IDLE
HWS1|<id>|OK|r=RATE_LIMIT;s=IDLE
HWS1|<id>|OK|r=STOPPING;s=STOPPING;id=<session>
HWS1|<id>|OK|r=IDLE;s=IDLE
HWS1|<id>|OK|r=STALE;s=<state>
HWS1|<id>|OK|r=STATUS;s=<state>;id=<session>;left=<0..60>;audio=VISUAL
```

格式/参数错误继续使用 `ERR|1`/`ERR|3`；未知操作不改变状态。所有响应在最大 request-id 下也必须小于 64 字节，并有边界单测。

异步结束事件：

```text
HWS1|0|FIND|END,<session>,USER_STOP
HWS1|0|FIND|END,<session>,PHONE_STOP
HWS1|0|FIND|END,<session>,TIMEOUT
HWS1|0|FIND|END,<session>,PREEMPTED_ALARM
```

事件只在 GUI 已清理覆盖层后产生。断连时允许丢失；重连不补发旧事件。

### 5.3 幂等与乱序

- 相同 request-id + START 重传返回同一会话，100 次不重置开始 tick、不重复唤醒、
  不重复建 UI。
- ACTIVE 期间新 START 返回 ALREADY，不延长 60 秒。
- STOP 在 IDLE/ACTIVE/STOPPING 均成功，重复 STOP 不重播、不重复结束事件。
- 会话完成后缓存 last completed START ID；同 ID 的迟到 START 返回 STALE，不重启。
- 不对不同 request-id 做大小/半区间比较。Android 重启后从 1 开始、`65535 -> 1`
  回绕或非 FIND 请求穿插都不会被错误拒绝；ACTIVE 时不同 ID 统一返回 ALREADY。

## 6. 状态机

| 当前状态 | 事件 | 条件 | 新状态/动作 |
|---|---|---|---|
| IDLE | START | alarm/timer ringing | IDLE，BUSY_ALARM |
| IDLE | START | badge 或 Agent GIF/image busy | IDLE，BUSY_TRANSFER |
| IDLE | START | 距上次成功会话结束不足 1 秒 | IDLE，RATE_LIMIT |
| IDLE | START | 空闲 | ACTIVE，保存 session/start tick，投递唤醒 |
| ACTIVE | 重复 START | 同一请求 | ACTIVE，ACCEPTED，deadline 不变 |
| ACTIVE | 新 START | 任意 | ACTIVE，ALREADY，deadline 不变 |
| ACTIVE | STOP | 手机 | STOPPING，原因 PHONE_STOP |
| ACTIVE | 本地点击 | 覆盖层有效 | STOPPING，原因 USER_STOP |
| ACTIVE | poll | 无符号 tick 差达到 60 秒 | STOPPING，原因 TIMEOUT |
| ACTIVE | alarm worker | alarm/timer 开始 ringing | STOPPING，原因 PREEMPTED_ALARM |
| STOPPING | STOP | 任意 | STOPPING/IDLE，幂等 |
| STOPPING | GUI 清理完成 | 任意 | IDLE，排队一个结束事件 |
| 任意 | STATUS | 任意 | 不改变状态，返回一致快照 |

tick 判断使用 `(rt_tick_t)(now - start) >= rt_tick_from_millisecond(60000)`，先做无符号差再换算，覆盖回绕。重复 START 不写 start tick。

## 7. 并发、生命周期与优先级

### 7.1 线程约束

- BLE GATT 回调：仅边界校验、状态机短临界区、非阻塞 mailbox send 和已有短通知；禁止 LVGL、文件系统、Flash、音频、等待 mutex。
- `blelink` 线程：执行 PM 唤醒和异步 BLE 通知。
- GUI `app_watch` 线程：唯一允许创建/更新/删除覆盖层的上下文。
- alarm 线程和 image worker：继续拥有原服务；本功能只读它们发布的无阻塞快照。

`watch_alarm_notify()` 在开始正式提醒通知/音频前直接调用
`MOMOFIND_PreemptAlarm()`，避免只依赖 GUI 轮询发现抢占。覆盖层由 GUI 下一次轮询
（目标不超过 100 ms）删除；因为 FIND MVP 完全不占音频资源，正式闹钟声音无需等待
LVGL 清理。正式提醒 UI 与覆盖层视觉切换的严格先后只可真机验收，不在主机测试中
宣称保证。

### 7.2 优先级

1. 每日闹钟/计时器 ringing：START 返回 BUSY；ACTIVE 立即 PREEMPTED_ALARM，并在正式提醒 UI 刷新前清理覆盖层。
2. WFPUSH2 badge 与 Agent Pet GIF/image 传输：START 硬 BUSY；ACTIVE 后若传输开始，不主动终止已接受 FIND，但 Android 在本地 transfer flag 下禁用发起。设备端协议仍保护 START 竞态。
3. FIND 覆盖层：仅视觉遮盖普通页面、音乐页和 Pet 状态。
4. Agent、远端 GIF PLAY/RESTORE、打字、任务花园、天气、自主行为：状态所有权不变，覆盖层结束后按原逻辑呈现。

### 7.3 页面和对象生命周期

- 覆盖层父对象固定为 `lv_layer_top()`，跨 Pet/时钟/音乐/文件等页面。
- 不保存或恢复当前 app ID；不触发页面导航，避免页面栈变化。
- 创建前检查 owned root 为空/无效；删除时先清事件/动画，再 `lv_obj_del()` 并置空。
- GUI 线程之外不读取或写入任何 `lv_obj_t *`。
- 50 次 START/STOP 后 owned root 必须为空，状态机 IDLE，无新增 task/timer。

## 8. 断连、重连和超时

- START 在协议返回 ACCEPTED 前不会创建 UI；写入未到达设备即无会话。
- ACCEPTED 后 BLE 断连不停止 FIND；设备继续到本地点击或 60 秒单调超时。
- 断连只更新链路代次、清请求排序窗口、丢弃不可发送的结束事件；不改 ACTIVE deadline。
- 重连收到 STATE `find=1` 后 Android 发送 FIND STATUS；ACTIVE 恢复停止按钮与剩余时间，IDLE 恢复呼叫按钮。
- Android 切后台不启动 Service；回前台调用 STATUS 对账。
- 60 秒结束由设备 GUI 轮询驱动。屏幕休眠时 START 的 mailbox 唤醒会恢复 GUI；
  mailbox 投递失败会在返回响应前回滚 START，因此不会留下已接受却永不显示的会话。

## 9. Android 原生入口

现有伴侣是 Java 原生 `Activity`，不是 WebView、JavaScript bridge 或 Kotlin。改动如下：

- 修复当前端到端连接前置缺口：Android 扫描目前只接受
  `Huangshan-Watch` 前缀，而目标固件广播名是 `AgentPet-HS52`。扫描过滤同时接受
  `AgentPet-` 与旧 `Huangshan-Watch`，不改变 GATT UUID。
- `WatchBleClient.Listener` 增加 FIND 状态回调。
- 连接默认 `findSupported=false`；解析 STATE 的 `find=1` 后才显示/启用按钮。
- `WatchBleClient` 单线程 worker 串行执行 `START`、`STOP`、`STATUS`，本地 `findRequestPending` 防抖。
- 图片上传设置 `transferInProgress`；期间按钮禁用。设备仍以 BUSY_TRANSFER 作为最终权威判断。
- ACTIVE 时按钮文案为“停止呼叫”，IDLE 时为“呼叫 Momo”；BUSY/断连/超时均显示明确状态。
- 处理 request-id 0 的 FIND END 异步事件；回前台和重连用 STATUS 恢复，不依赖旧事件。
- 不新增后台服务、定位、网络、账号、权限或外部 UI 素材。

将协议业务解析/按钮状态归约提取为无 Android SDK 依赖的 Java 类，供 JVM 单元测试覆盖旧固件能力隐藏、重复点击、防抖、断连、状态恢复和异步结束事件。

## 10. 错误处理和回退

- NULL、过长帧、未知 FIND 操作、非法 request-id：返回既有 HWS1 错误，不改变状态。
- alarm/image busy 快照不可用：保守返回对应 BUSY，避免错误抢占。
- START 的 PM mailbox 满：不阻塞 BLE，回滚尚未显示的会话并返回 `ERR|4`；不返回
  虚假的 ACCEPTED。结束通知 mailbox 满：保留 pending 并在 GUI 轮询重试，断连后
  允许丢弃并由 STATUS 对账。
- LVGL 创建失败：状态机结束为本地运行错误并清除任何已创建对象；Android通过 STATUS看到 IDLE。MVP不新增运行错误事件枚举，日志记录原因。
- 通知不可用/断连：结束事件丢失，STATUS 为权威。
- 音量 0、普通本地音乐、A2DP streaming、无可证明安全的提示音：统一纯视觉，不修改音量和播放队列。
- feature Kconfig 关闭时：STATE 不含 `find=1`，FIND 返回 unsupported，Android入口隐藏。
- 未认证控制面：MVP 仅内部验证，不允许启用声音或宣称量产；正式发布门槛是独立的
  HWS2/v2 已绑定能力与安全评审。

## 11. 资源与性能预算

| 项目 | 目标 |
|---|---|
| 新素材 | 0 B |
| 固件 `.text + .rodata` | 净增 ≤ 20 KiB |
| `.data + .bss` | 净增 ≤ 1 KiB |
| 状态机静态内存 | 目标 ≤ 160 B |
| 临时栈 | BLE/GUI 单次调用目标 ≤ 256 B |
| LVGL heap | ACTIVE 峰值目标 ≤ 8 KiB，STOP 后回到基线波动 |
| 新线程/队列 | 0；复用 `blelink` mailbox，多两个固定消息类型 |
| 新常驻 timer | 0；复用 GUI loop，状态机保存一个 deadline |
| GUI 频率 | 最多 10 Hz；仅 ACTIVE 刷新轻量呼吸样式 |
| BLE | 每次请求/响应短于 64 B；无新 UUID/GATT service |
| Flash 写入 | 0 |

构建后用相同工具链对基线与功能配置的 ELF 执行 size，记录 text/data/bss 实测差值。LVGL heap 峰值和功耗只能真机量化，标记待硬件验收。

## 12. 测试矩阵

### 12.1 纯 C/协议/状态机主机测试

- START 正常、同请求重复 100 次、ACTIVE 新 START、STOP 幂等。
- 1 秒设备端冷却和 Android 防抖，确认不延长期限且连续 50 次测试不泄漏。
- request-id 重复、不同 ID 乱序、完成后迟到 START、`65535 -> 1` 回绕，以及非 FIND
  请求穿插；不使用未定义的全局排序假设。
- 原始 tick 正常、恰好 60 秒、未满 60 秒、tick 回绕。
- ACTIVE 断连后继续、重连代次重置请求窗口、STATUS 恢复。
- alarm/timer 预忙与运行中抢占。
- badge WFPUSH2 busy、Agent image queued/receiving busy、空闲恢复。
- 音量 0、音乐 PLAYING/A2DP streaming 下状态不变且 audio=VISUAL。
- GUI create/stop/重建意图以及结束事件只产生一次。
- 至少 20 组优先级交错，覆盖 alarm、两类传输、STOP、TIMEOUT、断连和页面变化顺序。
- 所有响应长度上限、NULL、边界和未知操作。

### 12.2 Android JVM 单测

- 旧 STATE 无 `find=1` 时隐藏/禁用，新固件启用。
- START 防抖、ACCEPTED/ALREADY/BUSY_ALARM/BUSY_TRANSFER/STALE 归约。
- STOP/IDLE 幂等、END 四种原因。
- 断连清 UI pending、重连 STATUS 恢复 ACTIVE/IDLE。
- 图片传输期间禁用；异常响应和超时不遗留 pending。

### 12.3 仓库回归和构建

计划实际执行并记录退出码/警告：

```powershell
.\tests\run_momo_find_me_host_test.ps1
.\tests\run_watch_protocol_host_test.ps1
.\tests\run_agent_pet_protocol_host_test.ps1
.\tests\run_agent_pet_image_transfer_host_test.ps1
.\tests\run_agent_pet_weather_host_test.ps1
.\tests\run_agent_pet_behavior_host_test.ps1
.\tests\run_agent_quest_garden_host_test.ps1
.\tests\build-flash-utils.test.ps1
cd phone_app\android
.\gradlew.bat testDebugUnitTest assembleDebug
cd ..\..
work\watch_bt_audio_template\simulator\build.bat
powershell -File .\build.ps1
```

若某脚本在当前基线不存在，测试记录必须写明“未提供”，不能伪报通过。PC 完整构建后运行模拟器烟测，至少验证 overlay 创建、点击停止、跨 Pet/时钟/音乐页面覆盖与清理。真实目标构建检查新增警告、数组/字符串边界、空指针、RTOS 并发与 LVGL 线程归属。

### 12.4 A/B 资源测量

1. 在 `origin/master@75bcc572...` 用同一 `build.ps1` 生成基线 ELF。
2. 在功能分支用相同命令生成功能 ELF。
3. 使用同一 `arm-none-eabi-size` 对两个 `main.elf` 记录 text/data/bss，并计算净差。
4. 不以旧构建目录或不同配置产物代替 A/B。

## 13. 真机验收（待硬件验收）

以下项目不能由主机测试或模拟器宣称通过：

1. SF32LB525 烧录同分支固件，安装同分支 Android debug APK。
2. 灭屏状态呼叫，验证 BLE 接受后屏幕真实唤醒、视觉反馈 P95 ≤ 1 秒。
3. 分别在 Pet、时钟、音乐、文件页呼叫并设备点击停止，确认原页面、原 GIF/Agent/天气/自主行为状态未改变。
4. 音量 0 与非 0、普通音乐/A2DP 播放场景均确认不发声、不改音量、不破坏队列。
5. 呼叫前/中触发每日闹钟和计时器，确认 BUSY 或 PREEMPTED_ALARM，正式响铃不丢失。
6. WFPUSH2 图片和 Agent 多 GIF 传输的入队、接收、校验、切换阶段分别尝试 START，确认 BUSY 且文件/槽位摘要不变。
7. 呼叫中断开 BLE、Android 切后台、重连查询 STATUS，验证 60 秒本地闭环。
8. 连续 50 次 START/STOP，采集串口、可用堆、LVGL 对象、任务/定时器数量，无持续下降或泄漏。
9. 测量待机与 60 秒纯视觉呼叫的平均/峰值电流，并检查呼叫结束后的休眠恢复。
10. 安全验收确认测试包仅用于受控环境；量产版本在未完成 HWS2/v2 认证前不开放
    FIND capability。

## 14. 提交与完成门槛

1. 本方案先独立中文 commit。
2. 再实现固件、Android、测试和协议文档。
3. 开发提交必须同时包含 `docs/2026-08-11-momo-find-me-test-results.md`。
4. 完成回报必须列出产品 commit、方案 commit、开发 commit、实际命令/exit code/警告、A/B text/data/bss、已知风险与真机步骤。
5. 工作树必须干净；不推送、不建 PR、不合并。
