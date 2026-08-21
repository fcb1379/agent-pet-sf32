# Momo 语音明信片实现方案

## 1. 文档信息与结论

- 日期：2026-08-21
- 目标分支：`codex/2026-08-21-momo-voice-postcard`
- 产品门禁提交：`06c76585954334d28d1497ccfc33032ae4f353bd`
- 基线：`origin/master@a2805abeb72f02a726dce5dc49857280ba9cdc78`
- 目标板：`sf32lb52-lchspi-ulp`
- 结论：**端到端方案可设计，但本轮开发门禁未通过，提交本方案后停止代码实现。**

未通过的不是代码结构可行性，而是 PRD 和任务明确要求的三项实施前证据尚未同时闭环：

| 硬门禁 | 当前结论 | 已有证据 | 缺失证据/解锁条件 |
|---|---|---|---|
| 文件系统余量与原子存储 | 条件不成立 | 固件启用 TF/FAT，已有 `dfs_statfs`、`fsync`、`tmp/bak/rename` 回滚模式 | 必须在目标真机、实际 TF 卡上证明 `/sdcard` 已挂载且启动传输时可用空间满足本文的动态预留公式；静态配置不能证明用户卡的实时余量 |
| Opus 下行解码与扬声器播放 | 成立 | `recorder_service` 注册 `ff_ogg_demuxer` 与 `ff_libopus_decoder`，支持 `.opus`，经 `RECORDER_Play`、FFmpeg 和 Audio Manager 播放；完整目标构建及 map 均证明符号实际链接 | 仍需真机播放 1/5/15 秒标准向量，验证音量、爆音、停止延迟和共存恢复，但不阻断方案提交 |
| BLE 吞吐和恢复 | 条件不成立 | Android 已请求 MTU 247，现有图片链路使用 180 B 分块、逐块响应、6/8 秒超时、断线失败和自动重连；固件记录协商 MTU，支持 20~244 B 音频通知 | 没有目标手机+真机的 MTU 23/247 实测吞吐、长传稳定性和断线恢复数据；不得用图片链路“能传”替代 45 KiB 音频时限证明 |

因此本轮不创建 GATT 假入口、不增加仅固件 UI、不提交 Android 空页面，也不生成“开发完成”提交。三个门禁完成后再按第 15 节顺序实施。

## 2. 现状审计

### 2.1 固件构建和资源基线

通过手动指定本机已安装的 SiFli Python 环境、SCons 和 ARM GCC，完成了基线全量构建：

```powershell
$env:SIFLI_SDK='D:\code\sf32\agent-pet-sf32-text-display-fix\sdk'
$env:RTT_CC='gcc'
$env:RTT_EXEC_PATH='C:\Users\woan\.sifli\tools\arm-none-eabi-gcc\14.2.1\bin'
$env:PYTHONPATH='D:\code\sf32\agent-pet-sf32-text-display-fix\sdk\tools\build;D:\code\sf32\agent-pet-sf32-text-display-fix\sdk\tools\build\default'
C:\Users\woan\.sifli\python_env\sifli-sdk2.4_py3.10_env\Scripts\scons.exe --board=sf32lb52-lchspi-ulp --board_search_path=../boards -j8
```

结果：退出码 0，生成 `main.elf`、`main.bin`、`main.map` 和下载文件。基线尺寸为：

```text
text=5,497,138  data=16,620  bss=4,864,640  dec=10,378,398
main.bin=5,513,956 bytes
```

板级分区为 HCPU Flash 代码区 8 MiB、内部 `FS_REGION` 4 MiB、PSRAM 8 MiB。`main.bin` 相对 8 MiB 代码区仍有约 2.74 MiB 物理余量；`size` 的 BSS 汇总包含放在 PSRAM 的录音工作区，不能直接当作 512 KiB HCPU SRAM 使用量。后续必须以 map 分区归属和真机高水位同时判断 RAM。

基线构建已有 SDK/FFmpeg 警告，包括第三方宏重定义、潜在空指针诊断、FFmpeg 数组读取诊断以及链接 RWX 段提示。本功能新增代码的警告必须为 0；不得把现有告警误报为本功能引入，也不得宣称全工程“零警告”。

### 2.2 存储路径与掉电语义

- 录音服务固定使用 `/sdcard/recordings`；目标配置开启 `CONFIG_AGENT_PET_USING_TF_CARD` 和 FAT。
- 图片/GIF 使用 `/sdcard/AgentPet`，已有写入 worker、固定缓冲、`dfs_statfs`、临时文件 `fsync`、旧文件改名为备份、新文件改名为正式文件、失败回滚和启动清理。
- 内部 4 MiB `FS_REGION` 已承载资源镜像，语音 V1 不应把它当作消息容量兜底；语音路径固定在可移除 TF 卡，功能能力必须随挂载状态动态变化。
- FAT 上的同目录 `rename` 可提供目录项级替换基础，但“绝对掉电原子”仍需实际卡和挂载实现验证。V1 必须保留旧正式文件，只有新文件完整校验并 `fsync` 后才切换；启动恢复优先保留最后可校验的正式文件。

### 2.3 Opus 下行与音频仲裁

目标配置开启 `PKG_LIB_OPUS`、`PKG_USING_FFMPEG` 和音频播放。完整构建的 `main.map` 包含：

- `ff_libopus_decoder`；
- `libopus_decode_init`/`libopus_decode`；
- `opus_multistream_decode_float` 兼容入口；
- Ogg Opus 解析对象；
- `RECORDER_Play`、`ffmpeg_open` 与 Audio Manager 对象。

`RECORDER_Play` 已限制路径必须位于 `/sdcard/recordings/`，录音忙或 A2DP 正在流式播放时返回忙，播放前停止本地音乐，FFmpeg 通过固定 32,064 B PCM 环形缓冲进入音频服务。录音与本地播放共享 Opus/音频工作区并由状态机互斥，不能另建一套并发解码器。

现状不是统一的业务级音频所有权管理器：A2DP、录音、本地音乐、噪声服务和闹钟分别有自己的状态/回调。语音明信片应增加一层轻量仲裁适配器，只编排现有接口，不绕开 `audio_open` 或直接操作 codec。

### 2.4 BLE 和上位机

固件 Agent Pet 服务现有状态、图片、功德和音频特征。音频特征当前语义是“小控制帧下行 + Opus 包通知上行”，并不是语音文件下发通道。上传 worker 使用固定队列，把录音回调与 BLE 发送解耦；协商 MTU 后通知值长度限制为 20~244 B。

Android 当前只发现 HWS1 控制服务和 SiFli serial/watchface 服务，没有发现 Agent Pet 音频特征，也没有录音权限、Opus 编码、语音发送、回复接收或消息状态页面。现有图片上传为 180 B 分块，每块等待设备响应；BLE 写路径串行化、带超时，断线会令 waiter 失败并在 2.5 秒后尝试重连。

Android 基线命令 `gradlew.bat :app:assembleDebug` 因本机没有可用的 Java/JDK 且未配置 `JAVA_HOME` 而失败。解锁实施前必须配置 JDK 17，并先让未改动基线构建通过。

### 2.5 LVGL 线程边界

Pet 页面所有对象和定时器均在 GUI/LVGL 线程创建、刷新和销毁，周期性状态读取通过 `PET_RefreshStatus` 完成。图片接收在独立 worker 中提交，Pet 页只读取快照并切换资源。这一模式应直接复用于语音状态：BLE/GATT 回调只能校验头部并投递固定队列，文件 worker 发布不可变快照，Pet 页的 LVGL timer 在 GUI 线程渲染。

## 3. V1 架构

```text
Android UI/录音
    │  有界 PCM/Opus、消息状态
    ▼
MomoVoiceRepository ── VoiceBleClient ── Agent Pet Voice GATT v1
                                      │
                                      ▼
                           GATT 回调：只校验/复制/投递
                                      │ 固定队列
                                      ▼
                      voice_postcard_service worker
                         ├─ 协议状态机/CRC/超时
                         ├─ /sdcard/AgentPetVoice 原子文件
                         ├─ 能力/进度/错误快照
                         └─ RECORDER_Play/StartOpusStream
                                      │
                                      ▼
                           Audio Manager / FFmpeg

Pet LVGL timer ──只读快照──> 未读/播放/录音/待上传 UI
```

新增固件模块建议：

- `momo_voice_protocol.[ch]`：无 RTOS、无文件系统的纯 C 帧编解码和边界验证；
- `momo_voice_postcard.[ch]`：接收、存储、恢复、回复和只读快照；
- `momo_voice_audio.[ch]`：对 RECORDER、本地音乐、A2DP、噪声和闹钟状态做统一仲裁；
- `agent_pet_ble_service.c`：仅注册新特征、复制写入固定队列、发送状态通知；
- `app_pet.c`：在 GUI 线程渲染徽标和手势。

新增 Android 模块建议：

- `VoiceProtocol.java`：与纯 C 相同的协议向量；
- `VoiceBleClient.java`：能力协商、发送、ACK、恢复和上行接收；
- `VoiceRecorder.java`：录音、时限、编码和本地试听；
- `VoicePostcardController.java`：UI 状态机与前后台生命周期；
- `MainActivity.java`：真实可达的“语音明信片”入口及状态区域。

不新增固件常驻线程：复用现有 Agent Pet 图片/传输 worker 的模式，优先把语音文件事件纳入一个固定静态 worker；若代码隔离需要独立 worker，必须用静态栈且先测高水位，栈上限 2 KiB，不允许复用远端长度创建数组。

## 4. 能力协商

不得根据“特征存在”直接启用 UI。连接完成后 Android 读取 `VOICE_CAP`：

| 字段 | 大小 | 说明 |
|---|---:|---|
| magic `MV` | 2 B | 固定魔数 |
| protocol_version | 1 B | V1 为 1 |
| feature_flags | 2 B | 下发、上行、播放、回复、恢复、TF 已挂载、空间可用 |
| max_message_bytes | 4 B | 固件实际接受上限，V1 不超过 65,536 |
| max_duration_ms | 4 B | V1 不超过 15,000 |
| max_value_bytes | 2 B | 当前 ATT 值上限 |
| supported_codec | 1 B | 仅 Opus |
| sample_rate/channels/frame_ms | 4 B | 16 kHz/1/20 ms |
| generation | 4 B | 能力或存储状态变化计数 |
| CRC-8/ATM | 1 B | 头部保护 |

以下任一情况 Android 必须隐藏发送/回复入口并显示可诊断原因：协议版本不支持、TF 未挂载、可用空间门禁未通过、Opus 播放自检不可用、固件忙于不可抢占音频、Android 没有录音权限或 Android 编码器不满足固定参数。旧固件没有 `VOICE_CAP` 时静默降级，不循环报错。

## 5. 下行协议

### 5.1 特征拆分

- `VOICE_CAP`：读/通知，能力变化和服务代际；
- `VOICE_RX`：Android 写请求，承载 START/DATA/END/CANCEL/QUERY；
- `VOICE_STATUS`：读/通知，ACK、进度和终态；
- 现有 `AGENTPET_AUDIO_VALUE` 继续承载回复录音控制及上行通知，V1 不改变其既有帧语义。

拆分可避免下行大文件写与上行实时通知共享一个隐式状态，并允许旧 Android 保持原功能。

### 5.2 公共帧头

所有多字节整数均为小端。公共头固定 18 B：

| 偏移 | 字段 | 大小 | 约束 |
|---:|---|---:|---|
| 0 | magic `MV` | 2 B | 不匹配立即拒绝 |
| 2 | version | 1 B | 必须为 1 |
| 3 | type | 1 B | START/DATA/END/CANCEL/QUERY |
| 4 | message_id | 4 B | 非 0，Android 随机生成并持久化 |
| 8 | sequence | 3 B | 0~0xFFFFFF，DATA 单调递增 |
| 11 | payload_len | 2 B | 不超过协商值减去头尾 |
| 13 | offset | 4 B | DATA 必须等于设备已提交连续偏移 |
| 17 | header_crc8 | 1 B | CRC-8/ATM |

DATA 在载荷后增加 CRC-16/CCITT；START 和 END 使用各自固定元数据并在整帧末尾带 CRC-32/MPEG-2。任何长度相加先使用 64 位并验证不超过 65,536 B，再转换到窄类型。

START 至少包含：总字节数、总 Opus 包数、总 CRC32、采样率、声道、帧时长、时长毫秒、pre-skip、关联 GIF 槽位和行为标志。只接受 16 kHz、单声道、20 ms 帧、时长 1~15 秒、总长 1~65,536 B、有效槽位或“无关联”。

### 5.3 传输、ACK 和流控

- 使用 Write Request，GATT 成功只表示协议帧送达接收回调，不表示已落盘。
- GATT 回调完成固定上限检查后复制到深度有界的静态队列；队列满返回 ATT 错误，不覆盖旧数据。
- worker 顺序写临时文件，并通过 `VOICE_STATUS` 通知 `ACK(message_id, next_sequence, committed_offset, status)`。
- Android 初始采用窗口 1；只有真机测量稳定后才允许协商窗口 2~4。未收到 ACK 不发送下一窗口，避免无界积压。
- 重复 DATA 若 `sequence/offset/CRC` 与最后 ACK 一致，返回相同 ACK；内容不同则终止会话。
- 乱序和跳洞不做内存重组，返回 `EXPECTED_OFFSET`；Android 从最后连续偏移重发。
- 单帧 ACK 超时 3 秒，整个会话无进展 10 秒即取消临时会话；重试上限 3 次并指数退避。
- `QUERY(message_id)` 返回设备当前临时会话的连续偏移。重连后 Android 先查询再续传；会话不匹配则从 START 重建。

### 5.4 终态

`END` 仅在字节数、包数、逐包长度、总 CRC、Opus/Ogg 基础结构均通过后进入提交。状态终态包括：

- `COMMITTED`：正式文件已替换，返回新 generation；
- `DUPLICATE_COMMITTED`：同一 message_id 和 CRC 已完成，幂等成功；
- `CANCELLED`、`TIMEOUT`、`DISCONNECTED`；
- `NO_STORAGE`、`INVALID_LENGTH`、`INVALID_CODEC`、`BAD_SEQUENCE`、`BAD_CRC`、`BUSY`、`IO_ERROR`、`UNSUPPORTED_VERSION`。

所有失败只清理当前临时文件；上一条正式语音和未读状态保持不变。

## 6. 文件布局、原子保存和掉电恢复

固定目录：`/sdcard/AgentPetVoice`。远端字段绝不参与路径拼接。

```text
inbox.opus       当前可播放正式语音
inbox.meta       正式元数据（双副本或带 generation+CRC）
inbox.tmp        当前下发临时文件
inbox.tmp.meta   可恢复偏移、message_id、总长、CRC
inbox.bak        提交窗口中的上一条正式文件
reply.opus       待上行回复
reply.meta       回复状态、message_id、CRC
```

传输开始前必须同时满足：

1. `TF_CARD_EnsureMounted()==RT_EOK`；
2. `dfs_statfs` 成功；
3. `available_bytes >= incoming_total + existing_inbox_size + existing_reply_size + 256 KiB`；
4. 所有乘法使用 64 位且无溢出；
5. 路径所在文件系统与正式文件相同，禁止跨挂载点 rename。

这里的 256 KiB 是独立安全预留，不是消息大小。任何一项失败都不进入 RECEIVING，Android 收到 `NO_STORAGE`。这也是当前必须由真机闭环的硬门禁。

提交顺序：关闭并 `fsync(inbox.tmp)` → 完整解析和总 CRC → 写入并 `fsync(inbox.tmp.meta)` → 删除过期 bak → 正式文件改名为 bak → tmp 改名为正式文件 → 临时元数据改名/写双副本正式元数据 → 更新内存 generation → 删除 bak。每一步检查返回值；失败尝试恢复 bak，并保留可诊断错误。

启动恢复：

- 正式文件+元数据校验通过：以正式文件为准；
- 正式文件无效、bak 有效：恢复 bak；
- 正式和 bak 都有效：generation 更高者为准，另一份清理；
- tmp 元数据有效且完整：可完成校验和提交；
- tmp 仅部分完成：保留到限定期限供 QUERY 续传，超时清理；
- 任意元数据长度、路径、CRC 不合法：拒绝使用，不尝试猜测。

## 7. 状态机

### 7.1 接收

```text
EMPTY/READY_READ/READY_UNREAD
  └─START(valid+space)─> RECEIVING
RECEIVING
  ├─DATA(expected)────> RECEIVING + ACK
  ├─duplicate─────────> RECEIVING + same ACK
  ├─QUERY─────────────> RECEIVING + current ACK
  ├─END(valid)────────> VALIDATING -> COMMITTING -> READY_UNREAD
  └─cancel/error──────> previous READY state or EMPTY
```

非法事件不创建隐式状态。`message_id + generation` 区分旧会话，旧 ACK 不能推进新会话。

### 7.2 播放

```text
IDLE -> REQUESTING_AUDIO -> PLAYING -> IDLE
  └──────── busy ───────> BLOCKED -> IDLE
PLAYING --tap/exit/error--> STOPPING -> IDLE/ERROR
```

只有 `RECORDER_Play` 成功才显示 PLAYING；`RECORDER_PlaybackStop` 返回完成后才释放音频所有权。播放完成把未读改成已读，但不删除文件。

### 7.3 回复

```text
NONE -> REQUESTING_AUDIO -> RECORDING -> FINALIZING -> PENDING_UPLOAD
PENDING_UPLOAD -> UPLOADING -> WAITING_CONFIRM -> SENT -> NONE
      └──────── error/disconnect ────────> PENDING_UPLOAD
```

录音最长 15 秒；沿用 `RECORDER_StartOpusStream` 的实时 Opus 包。为断线保留，V1 应同步落盘 Ogg Opus，再由既有上行协议重放；在 Android 完整确认 message_id、总包数和 CRC 前不得删除 `reply.opus`。现有上行 END 只有包数和丢包数，实施时必须增加兼容扩展或单独确认帧，不能把“通知已发出”视为送达。

## 8. 音频仲裁

业务适配层维护唯一 owner：`NONE`、`ALARM/CALL`、`VOICE_RECORD`、`VOICE_PLAY`、`LOCAL_MUSIC`、`A2DP`、`PROMPT`、`NOISE_MONITOR`。优先级按 PRD 执行，但 V1 对未知或无法安全暂停的 owner 一律返回 BUSY。

| 当前音频 | 播放明信片 | 录回复 | 退出后的恢复 |
|---|---|---|---|
| 闹钟/通话类 | 阻止 | 阻止 | 不干预 |
| A2DP | 阻止 | 阻止 | 不干预 |
| 本地音乐 | 经验证后暂停，否则阻止 | 经验证后暂停，否则阻止 | 仅恢复由本次暂停且仍有效的会话 |
| Recorder 播放/录音 | 阻止 | 阻止 | 不干预 |
| 噪声监测 | 请求现有 Audio Manager 挂起 | 请求挂起 | release 后允许其原状态恢复 |
| 空闲 | 允许 | 允许 | 正常释放 |

GATT 回调、Audio Manager 回调和录音回调都不得调用 LVGL，不得等待文件 IO，不得持有 GUI 对象。音频 owner 和状态快照用互斥锁/短临界区保护；阻塞关闭只在 worker 中执行且有超时。

## 9. LVGL 生命周期和手势

- Pet 页面加载时创建一个语音徽标、状态标签和现有周期刷新 timer；无能力或无消息时隐藏。
- worker 只更新 POD 快照和 generation；`PET_RefreshStatus` 检测 generation 后在 GUI 线程更新对象。
- 点击徽标：READY 时播放，PLAYING 时请求停止；长按只在 READY/IDLE 且能力允许时请求录音。
- 录音中短击停止；页面退出时只请求停止，不在销毁回调中等待音频线程。
- 手势事件明确消费：徽标命中后停止冒泡；徽标区域避开现有木鱼、GIF 切换和全屏返回热区。
- 传输进度最多 5 Hz 刷新，不因每个 BLE 分片触发全屏刷新；播放动效目标 20 FPS，空闲沿用当前 timer 周期。
- UI 状态只来自服务快照，不使用多个无约束布尔量拼接。

## 10. Android 端完整闭环

### 10.1 录制和编码

- Manifest 增加 `RECORD_AUDIO`，运行时授权被拒绝时不创建文件、不发送。
- 录音必须由用户明确点击开始，界面持续显示红色录音状态和剩余秒数；应用后台立即停止并进入可预览状态，不后台偷录。
- 固定 16 kHz、单声道、20 ms Opus、目标 24 kbps，最大 15 秒；发送前离线解析所有 Opus 包并验证时长和总长。
- 优先使用 Android 平台可验证的 Opus 编码路径；若选用第三方 libopus/封装库，必须固定版本、许可证和 NOTICE，并在 ABI 构建中验证。不能仅更改扩展名或把 AAC 当 Opus。
- 本地试听成功不等于设备可播放，仍需发送后设备最终 COMMITTED ACK。

### 10.2 BLE 会话

- 发现现有 HWS1/serial 服务后再发现 Agent Pet Voice 服务并读能力；MTU 回调结果必须保存，不能无视协商失败。
- 所有 GATT 操作仍经单一串行队列，禁止同时发图片和语音；连接 epoch 变化会使旧回调失效。
- Android 持久化待发 message_id、源文件 CRC、总长和最后设备 ACK；重连先读能力、QUERY 对账，再续传或明确重启。
- 进度以设备已提交 offset 计算，不以 `writeCharacteristic` 调用成功计算。
- 页面提供录制、试听、发送/取消、接收回复、保存/删除；失败显示稳定错误分类和“重试”，不暴露内部路径。

### 10.3 回复接收

- 订阅现有音频通知，按 session/sequence/fragment 重组单个 Opus 包；固定上限，不因对端字段动态分配无界数组。
- START 参数与能力不一致即拒绝；帧 CRC、序号、fragment_count、包长度、总包数和 END 必须全部验证。
- Android 将收到的包写入临时 Ogg Opus，完成校验后原子改名；向固件发送显式 CONFIRM。
- 重复 CONFIRM 幂等；固件收到匹配确认后才清理待发送回复。

## 11. 内存、Flash、文件系统与性能预算

| 项目 | 设计上限 | 说明 |
|---|---:|---|
| 单条下行文件 | 64 KiB | 超限 START 立即拒绝 |
| 正式收件+临时+回复 | 192 KiB 业务文件 | 另保留至少 256 KiB 自由空间；旧正式+新临时并存 |
| GATT RX 队列 | 4 × 244 B + 元数据，约 1.2 KiB | 固定深度；队列满返回错误 |
| 文件写缓冲 | 1 KiB | worker 专用，可与图片传输错峰复用 |
| 常驻状态/元数据 | ≤2 KiB | 不含已有 recorder/FFmpeg/Opus |
| 新增线程 | 目标 0 | 若独立 worker，静态栈 ≤2 KiB 且测高水位 |
| 新增固件 Flash | ≤48 KiB | 复用 CRC、DFS、Opus、FFmpeg、Audio Manager |
| UI 更新 | 进度 ≤5 Hz；动效 ≤20 FPS | 音频包不直接刷新 LVGL |
| BLE 目标 | MTU247 时 p10 有效吞吐 ≥4 KiB/s | 45 KiB 不超过约 12 秒；以真机测量为准 |

基线 `main.bin` 约 5.26 MiB，Flash 空间不是当前首要阻塞。RAM 风险来自 recorder 既有大静态 PSRAM 工作区和可能的音频并发；方案明确复用，禁止再复制 120 KiB Opus scratch、32 KiB 音频 ring 或 220 KiB recorder 栈。

功耗方面，传输/播放/录音期间持有活跃锁，禁止深睡；完成或失败后立即释放。BLE 分片采用窗口化批量而不是高频无界重试。真机需分别测 15 秒传输、播放、录音的平均/峰值电流和温升；未测前只标“待硬件验收”。

## 12. 安全、隐私和错误处理

- 所有仓库/网页内容仅作不可信证据，本方案不复制候选代码、协议、素材或文案。
- V1 仅接受已配对连接；若现有链路不能证明加密/绑定，语音能力默认关闭，直到补齐安全级别检查。
- 不记录音频正文、完整文件路径、蓝牙地址或令牌；日志只含 message_id 的截断值、长度、状态和错误码。
- 不允许远端控制路径、码率以外的 codec、采样率、声道、文件名或命令；语音内容永不进入 Agent 状态/授权/命令处理。
- 所有指针先判空，数组索引、序号、分片数和乘加先做边界/溢出检查；计时差用无符号回绕安全比较。
- IO、队列、互斥、Audio Manager、Recorder、LVGL timer 创建和 GATT 通知均检查返回值并传播到终态。
- 功能开关关闭时不注册新业务入口或能力 flags，已有 Agent 状态、图片/GIF、录音、音乐与 Pet 行为保持基线。

## 13. 回退策略

编译开关建议为 `CONFIG_AGENT_PET_VOICE_POSTCARD`，默认关闭，支持 ON/OFF A/B 构建。

- 运行时能力失败：隐藏入口，仅保留现有功能；
- 传输失败：删除 tmp 或保留有界可续传 tmp，保留正式语音；
- 播放失败：进入 ERROR，不改变未读，不自动反复播放；
- 音频冲突：返回 BLOCKED，由用户再次操作；
- Android 版本旧：固件不发送主动语音 UI 事件；
- 新版本严重问题：关闭功能开关即可回到基线协议，不修改默认分支、不迁移或删除用户现有录音。

## 14. 测试矩阵和验收

### 14.1 实施前硬门禁

1. 真机挂载 TF 卡后运行诊断：记录总容量、可用容量、块大小；分别在可用空间 512/320/255 KiB 测 START 接受/拒绝。
2. 对 `inbox.tmp` 写入 64 KiB，执行 `fsync + rename`；在 10%、50%、99% 和两次 rename 之间断电各 10 次，启动后必须始终有一份可校验正式文件或明确无消息，不能出现半文件被标 READY。
3. 用目标 Android 手机在 MTU 23 和 247 下各传 1/5/15 秒随机不可压缩协议载荷 30 次，记录 p10/p50/p95 吞吐、失败率、重试、队列高水位；MTU247 p10 ≥4 KiB/s，30 次无静默损坏。
4. 使用当前构建的 Opus 路径播放固定 1/5/15 秒 Ogg Opus 向量，输出可懂、时长误差≤一帧、停止流程≤500 ms；记录 Audio Manager/recorder 状态。
5. 配置 JDK 17 后，未改 Android 基线 `:app:assembleDebug` 必须通过。

以上五项记录齐全，才允许开始第 15 节的代码阶段。

### 14.2 协议自动化

- START：零长度、超限、非法采样率/声道/帧长/时长、未知版本、整数溢出；
- DATA：最小/最大载荷、序号 0/0xFFFFFF/越界、重复相同/重复冲突、跳号、旧 message_id、CRC16 错；
- END：缺包、短写、总 CRC 错、总包数错、重复 END、CANCEL 后 END；
- 恢复：10/50/99% 断线、重连 QUERY、旧连接 epoch 的迟到 ACK、应用重启；
- 存储：未挂载、statfs 失败、空间不足、open/write/short-write/fsync/rename/unlink 错误、残留 tmp/bak；
- 模糊测试：随机长度、枚举、分片和事件序列，确保无越界、死锁、断言和永久 BUSY。

纯 C 协议测试必须在主机运行，Android 使用 JUnit 做同一组黄金向量和错误终态。

### 14.3 音频与 UI

- A2DP、本地音乐、闹钟、Recorder、噪声监测分别活动时播放和录音；结果严格符合仲裁表且原路径恢复；
- 页面进入/退出、快速点击、长按取消、播放中停止、录音 15 秒自动停止、后台/前台切换；
- 240×240 真机上徽标不覆盖返回和木鱼命中区；无能力/无消息时与基线一致；
- 连续 100 次收、播、录、回、取消，无句柄/对象/线程泄漏，记录堆、PSRAM、队列和栈高水位；
- 恶意长度、频繁 START/CANCEL、乱序/重复不会饥饿 GUI、看门狗或现有图片传输。

### 14.4 构建和 A/B 资源

必须执行并记录：

```text
固件 OFF 全量构建
固件 ON 全量构建
固件纯 C 协议主机测试
Android :app:testDebugUnitTest
Android :app:assembleDebug
```

比较 OFF/ON 的 `main.bin`、map Flash/RAM/PSRAM 段、线程/队列静态字节和构建警告。本轮只完成 OFF 基线固件构建；Android 因缺 JDK 未通过，功能 ON 尚不存在，不能形成开发完成标志。

### 14.5 待硬件验收

- TF 卡热插拔、不同容量/文件系统和实际掉电恢复；
- MTU/连接间隔/PHY 下吞吐和手机兼容性；
- 真机扬声器音量、爆音、停止延迟、麦克风可懂度；
- A2DP/本地音乐/闹钟/噪声服务的暂停恢复；
- 15 秒传输、播放、录音的峰值 RAM、CPU、功耗和温升；
- 100 次完整循环及 24 小时待机回归。

未完成这些项目时只能写“待硬件验收”，不得写“通过”。

## 15. 门禁解锁后的实施顺序

1. 配置 JDK 17，完成 Android 基线构建；在真机完成第 14.1 节五项门禁并提交原始记录。
2. 冻结 UUID、帧布局、错误码和黄金向量；先实现纯 C/Java 协议测试。
3. 实现固件固定队列、运行时存储门禁、临时文件和原子恢复；不接 UI。
4. 实现 Android 能力读取、离线录制/编码、传输状态机和真实入口；完成虚拟/host 断线测试。
5. 接入固件 `RECORDER_Play` 与音频仲裁；真机验证后再接 Pet 未读/播放 UI。
6. 扩展现有 Opus 上行确认语义，实现回复落盘、重连对账和 Android 收件。
7. 执行 ON/OFF 完整构建、协议/Android 测试、静态检查、资源差分和真机矩阵。
8. 只在 PRD commit、方案 commit、开发 commit、测试记录和实际构建结果同时存在时通知验收。

## 16. 本轮停止原因与下一步授权

本轮无需额外远端写权限，也不会推送、建 PR 或合并。继续开发前需要：

- 一块目标 SF32LB525 黄山派硬件及可写 TF 卡；
- 一台目标 Android 手机和 BLE 调试权限；
- 本机 JDK 17/`JAVA_HOME`；
- 允许烧录、串口日志和断电测试；
- 第 14.1 节门禁原始数据满足阈值。

在这些条件满足前，按任务要求，本方案提交即为子任务 2 的安全停止点；不存在开发 commit，也不能标记整个功能完成。
