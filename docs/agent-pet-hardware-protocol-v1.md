# Agent Pet 硬件联动协议 v1.0

状态：**维护中**
协议版本：`1`
传输方向：桌面端 BLE Central → 外设 BLE Peripheral

## 1. 范围与安全边界

本协议同步设备时间、聚合状态和有限的 Agent 会话元数据。它不传输任务正文、工作目录、
命令、文件名、授权内容或用户输入。外设只能显示状态，不能修改任务状态、执行
批准/拒绝或控制 Agent。

未来 USB 传输可以复用同一 20 字节帧。双向控制或能力协商不属于 v1.0，若引入
必须定义新消息类型或增加主版本并单独进行安全评审。

## 2. GATT

| 项目 | UUID | 属性 |
|---|---|---|
| Agent Pet Service | `7a1e0001-6b5f-4f5c-8c9d-3e2f1a0b1000` | Primary Service，必须进入广播数据 |
| Status RX | `7a1e0002-6b5f-4f5c-8c9d-3e2f1a0b1000` | Write Request，允许同时支持 Write Command |

要求：

- 每次写入严格为 20 字节。
- v1.0 不需要 Read、Notify、Indicate 或 CCCD。
- 开发阶段可使用 `NO_AUTH`，产品可启用链路加密，但不能改变帧格式。
- 设备名称使用 `AgentPet-<短标识>`，本工程默认为 `AgentPet-HS52`。
- 所有多字节整数均为小端序。

## 3. 固定帧

| 偏移 | 长度 | 字段 | 规则 |
|---:|---:|---|---|
| 0 | 2 | `magic` | ASCII `AP`，即 `41 50` |
| 2 | 1 | `protocol_version` | 固定 `1` |
| 3 | 1 | `message_type` | `1`：完整状态快照；`2`：木鱼事件；`3`：时间同步 |
| 4 | 2 | `sequence` | 16 位循环消息序号 |
| 6 | 1 | `chunk_index` | 从 `0` 开始 |
| 7 | 1 | `chunk_count` | `1..13` |
| 8 | 1 | `payload_length` | `1..10` |
| 9 | 10 | `payload` | 有效数据后必须补零 |
| 19 | 1 | `crc8` | 覆盖字节 `0..18` |

除最后一片外，`payload_length` 必须为 10。同一快照所有帧的版本、类型、
`sequence` 和 `chunk_count` 必须一致。

### CRC-8/ATM

| 参数 | 值 |
|---|---|
| Polynomial | `0x07` |
| Init | `0x00` |
| RefIn / RefOut | `false / false` |
| XorOut | `0x00` |
| `123456789` 检查值 | `0xF4` |

```text
crc = 0
for byte in data:
    crc ^= byte
    repeat 8:
        crc = ((crc << 1) XOR 0x07) if bit7 else (crc << 1)
        crc &= 0xFF
```

## 4. 快照载荷

```text
snapshot_length = 6 + session_count * 10
```

最大 126 字节、12 个会话、13 个帧。

### 4.1 快照头

| 偏移 | 长度 | 字段 |
|---:|---:|---|
| 0 | 1 | `aggregate_state` |
| 1 | 1 | `session_count`，`0..12` |
| 2 | 4 | `generated_at`，Unix 秒 |

连接有效性必须使用本地 BLE 连接事件判断，不能依赖 `generated_at`。

### 4.2 会话记录

每条记录固定 10 字节：

| 相对偏移 | 长度 | 字段 |
|---:|---:|---|
| 0 | 1 | `state` |
| 1 | 1 | `provider` |
| 2 | 1 | `source` |
| 3 | 1 | `flags` |
| 4 | 4 | `task_hash`，会话 ID 的 FNV-1a 32 位摘要 |
| 8 | 2 | `age_seconds`，`65535` 表示未知或溢出 |

状态：

| 值 | 含义 | 推荐颜色 |
|---:|---|---|
| 0 | idle | 灰 |
| 1 | running | 蓝 |
| 2 | needs_input | 黄 |
| 3 | completed | 绿 |
| 4 | error | 红 |

Provider：`0=other`、`1=Codex`、`2=Claude Code`。
Source：`0=unknown`、`1=Windows`、`2=WSL`、`3=Linux`。
Flags：bit0=`approval_pending`、bit1=`aggregate_active`，bit2..7 保留并忽略。

`task_hash` 只用于同屏稳定区分任务，不是安全标识，不得用于授权。

## 5. 时间同步载荷

时间同步使用 `message_type=3`，固定为单帧：`chunk_index=0`、`chunk_count=1`、
`payload_length=6`。

| 相对偏移 | 长度 | 字段 |
|---:|---:|---|
| 0 | 4 | `utc_epoch`，UTC Unix 秒，范围 `1577836800..2145916800` |
| 4 | 2 | `timezone_offset_minutes`，有符号 16 位整数，范围 `-840..840` |

设备本地时间按 `utc_epoch + timezone_offset_minutes * 60` 计算。桌面端在每次 GATT
连接或自动重连成功后首先发送当前时间；固件在协议层完成 CRC、补零和范围校验，
随后通过静态工作线程更新 RTC 并持久化时区，GATT 回调本身不写 Flash、不阻塞。
## 6. 接收状态机

1. 校验固定长度、头部、CRC、分片范围和补零。
2. 新 `sequence` 到来时清空未完成重组并开始新快照。
3. 将载荷写到 `chunk_index * 10`，以 16 位位图记录分片。
4. 允许乱序和重复分片；重复分片覆盖但不能重复发布。
5. 已发布的相同 `sequence` 直接忽略。
6. 全部分片到齐后校验长度必须等于 `6 + session_count * 10`。
7. 校验会话数、聚合状态、会话状态、provider 和 source 枚举。
8. 全部成功后原子替换当前快照；失败时保留上一份有效快照。
9. BLE 断开时清空未完成重组，已发布快照保留并标记离线。

接收端必须使用固定容量缓冲区；BLE 写回调中不得调用 LVGL、写 Flash、阻塞
等待或打印整帧日志。

## 7. 同步行为

- 桌面端只扫描广播中包含 Agent Pet Service UUID 的设备。
- 连接成功后先发送时间同步帧，再发送完整快照和宠物图片。
- 状态变化时发送新的完整快照，不发送增量。
- 分片默认用串行 Write Request。
- 外设断线后自动恢复可连接广播。
- 外设可以显示最后快照，但必须明确标记离线。

## 8. 黄金向量

空闲、0 会话、`sequence=0x1234`、`generated_at=0`：

```text
41 50 01 01 34 12 00 01 06 00 00 00 00 00 00 00 00 00 00 18
```
时间同步、`sequence=0x2468`、`utc_epoch=1785812521`、`timezone_offset_minutes=480`：

```text
41 50 01 03 68 24 00 01 06 29 56 71 6A E0 01 00 00 00 00 27
```

本仓库的 `tests/agent_pet_protocol_host_test.c` 还覆盖 CRC、乱序分片、重复
快照和错误 CRC。协议语义与桌面端仓库的 `src/hardware-protocol.js` 保持一致。
