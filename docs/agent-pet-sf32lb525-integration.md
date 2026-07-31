# SF32LB525 黄山派 Agent Pet 接入指南

本文描述双模 SF32LB525 黄山派如何作为 Agent Pet 外设接入。板端运行
RT-Thread、SiFli Sibles 和 LVGL；Agent 状态只走 BLE GATT，经典蓝牙保留给
A2DP 等功能。

协议的唯一规范来源是
[Agent Pet 硬件联动协议 v1.0](agent-pet-hardware-protocol-v1.md)。

## 1. 软件基线

| 项目 | 固定选择 |
|---|---|
| MCU | SF32LB525 / SF32LB52 系列 |
| 开发板 | 黄山派 |
| Board target | `sf32lb52-lchspi-ulp` |
| SDK | `sdk/` 子模块，SiFli-SDK `v2.4.0` |
| UI | LVGL v8 watch 工程 |
| BLE Host | HCPU Sibles |
| BLE Controller | SDK `AddLCPU(...)` 公共 LCPU 镜像 |

不要用 `sf32lb52-lcd_n16r8` 替代黄山派目标；该目标可能可编译和烧录，但板级
LCD 配置不同。

## 2. 仓库接入

```bash
git clone --recurse-submodules https://github.com/fcb1379/agent-pet-sf32.git
cd agent-pet-sf32
git submodule update --init --recursive
source sdk/export.sh
```

主工程：

```text
work/watch_bt_audio_template/
├── boards/sf32lb52-lchspi-ulp/
├── project/
└── src/
    ├── app_utils/
    │   ├── agent_pet_protocol.c
    │   ├── agent_pet_ble_service.c
    │   └── ble_watch_link.c
    └── gui_apps/pet/app_pet.c
```

## 3. 构建

```bash
source sdk/export.sh
cd work/watch_bt_audio_template/project
scons --board=sf32lb52-lchspi-ulp --board_search_path=../boards -j8
```

`project/SConstruct` 从 `SIFLI_SDK` 读取 SDK，并调用：

- `AddBootLoader(...)`
- `AddLCPU(...)`
- `AddFTAB(...)`
- `FileSystemBuild(...)`

蓝牙相关配置已经位于 `project/proj.conf`，包括：

```text
CONFIG_RT_USING_BLUETOOTH=y
CONFIG_BLUETOOTH=y
CONFIG_BSP_USING_DATA_SVC=y
CONFIG_BLE_CTKD_ENABLE=y
CONFIG_CFG_AV_SNK=y
```

Agent Pet v1.0 不依赖 MTU 大于 23；每个状态帧正好 20 字节。

## 4. GATT 实现

`agent_pet_ble_service.c` 注册独立 Primary Service：

```text
Service  7a1e0001-6b5f-4f5c-8c9d-3e2f1a0b1000
└── RX   7a1e0002-6b5f-4f5c-8c9d-3e2f1a0b1000
         Write Request + Write Command, max 20 bytes
```

SiFli 的 128 位 UUID 数组按 BLE 小端填写：

```c
/* Service */
00 10 0B 1A 2F 3E 9D 8C 5C 4F 5F 6B 01 00 1E 7A

/* Status RX */
00 10 0B 1A 2F 3E 9D 8C 5C 4F 5F 6B 02 00 1E 7A
```

用 nRF Connect 检查显示 UUID 是否与规范文本一致；倒序说明数组字节序错误。

## 5. 广播

桌面端按 Service UUID 过滤，所以 UUID 必须同时进入 Advertising Data，只有
GATT 数据库条目不够。`ble_watch_link.c` 使用静态存储设置：

- connectable advertising；
- `is_auto_restart = 1`；
- 名称 `AgentPet-HS52` 放入 Scan Response；
-完整 128 位 Service UUID 放入 `adv_data.completed_uuid`。

名称、UUID 广播结构和协议缓冲区均为静态存储，不在初始化或回调路径动态分配。

## 6. 生命周期

```text
BLE_POWER_ON_IND
  -> BLE 模块线程
  -> 注册原手表服务
  -> 注册 Agent Pet 服务
  -> 初始化串口传输服务
  -> 启动含 Agent Pet UUID 的广播

BLE_GAP_CONNECTED_IND
  -> 保存连接参数
  -> 标记 Agent Pet online

BLE_GAP_DISCONNECTED_IND
  -> 丢弃未完成重组
  -> 保留最后有效快照
  -> 标记 offline
  -> 自动恢复广播
```

## 7. 数据路径与线程边界

```text
桌面 Agent 状态
  -> BLE Write Request
  -> Sibles 写回调
  -> 固定 20 字节帧校验
  -> 静态 126 字节重组
  -> 完整快照校验并发布
  -> LVGL 250 ms 定时器读取副本
  -> 桌宠页刷新
```

写回调只做有界内存操作和计数。协议上下文读写位于短 RT-Thread 临界区，
不会调用 LVGL、Flash、音频或阻塞 IPC。

## 8. LVGL 显示

桌宠页显示：

- BLE online/offline；
- 聚合状态和任务数量；
- active flag 指定的活动会话，无 active 时显示第一条；
- Provider、任务哈希低 16 位、会话状态；
- `approval_pending` 以 `!` 标记。

颜色映射：idle 灰、running 蓝、needs_input 黄、completed 绿、error 红。
断线后状态文字带 `Offline`，避免把缓存快照误认为实时状态。

## 9. BLE + 经典蓝牙共存

- Agent Pet 服务只注册在 BLE ATT/GATT。
- 不通过 SPP 或 GATT over BR/EDR 传输 Agent 帧。
- A2DP sink/source、AVRCP、音频路由继续使用原模块。
- Agent Pet 的 UUID、连接标记、重组缓冲区和诊断计数与经典蓝牙隔离。
- 首版只保证状态链路和单一活动音频路由共存；多条同时活动音频流需单独压测。

## 10. 联调

### 10.1 主机测试

```powershell
.\tests\run_agent_pet_protocol_host_test.ps1
```

预期：

```text
PASS agent_pet_protocol_host_test
```

### 10.2 nRF Connect

1. 烧录后扫描 `AgentPet-HS52`。
2. 检查广播中存在 Service UUID。
3. 连接并发现 Status RX。
4. 向 RX 写入黄金空闲帧：

```text
4150010134120001060000000000000000000018
```

5. 打开桌宠页面，应显示在线、Idle、0 tasks。
6. 修改任一字节但不更新 CRC，UI 不应改变，拒绝计数应增加。

### 10.3 桌面端

1. 在 Agent Pet 设置中启用 BLE。
2. 选择广播含 Agent Pet Service UUID 的黄山派。
3. 连接后桌面端立即写入完整快照。
4. 分别验证 running、needs_input、completed、error。
5. 断开 BLE，板端应保留最后状态但显示 Offline，并重新广播。

## 11. 验收标准

- 递归克隆后无需外部绝对 SDK 路径即可配置工程。
- 固件能为 `sf32lb52-lchspi-ulp` 构建。
- 广播名称和完整 Service UUID 可被桌面端发现。
- 正确帧发布，CRC/长度/越界/枚举错误帧不改变 UI。
- 支持 0..12 个会话和最多 13 个乱序分片。
- 重复快照不重复发布。
- 断线清空半包并自动恢复广播。
- BLE 回调无动态内存、无 LVGL 调用、无 Flash 写入。
- 原经典蓝牙和音频模块仍可独立工作。

## 12. 后续版本

- USB 可直接承载同一 20 字节帧，并复用 `agent_pet_protocol.c`。
- 产品化时启用 BLE bonding/encryption 并设计可信设备重置入口。
- 双向控制若确有需求，必须定义新的鉴权、重放保护和用户确认机制，不能复用
  v1.0 单向状态写入特征。
