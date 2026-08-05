# Agent Pet SF32

Agent Pet 的 SF32LB525 黄山派硬件端仓库。工程基于
[`fcb1379/sf32`](https://github.com/fcb1379/sf32)，保留其 LVGL 手表、BLE、
经典蓝牙和音频能力，并新增桌面 Agent 状态的 BLE GATT 接收、协议校验、
固定内存重组和 LVGL 桌宠状态显示。

首版只使用 BLE 传输 Agent 状态。SF32LB525 的经典蓝牙仍可用于 A2DP 等原有
功能，但不承载 Agent Pet 协议。

## 已实现

- Agent Pet GATT Service 和 20 字节 Status RX Characteristic。
- CRC-8/ATM、分片乱序/重复处理、严格边界校验。
- 最多 12 个 Agent 会话，全部使用静态内存。
- BLE 断线保留最后有效快照并标记离线。
- LVGL 桌宠页展示聚合状态、任务数、活动 Agent 和待授权标记。
- SiFli SDK 以 `sdk/` 子模块锁定，工程不依赖开发机上的隐式 SDK 目录。
- 可在 Windows + WSL 或 Linux 上运行的纯 C 协议主机测试。

## 克隆

```bash
git clone --recurse-submodules https://github.com/fcb1379/agent-pet-sf32.git
cd agent-pet-sf32
git submodule update --init --recursive
```

已有克隆缺少 SDK 时：

```bash
git submodule sync --recursive
git submodule update --init --recursive
```

## 构建黄山派固件

目标板固定为 `sf32lb52-lchspi-ulp`：

Windows CMD 在仓库根目录直接执行：

```bat
build
```

PowerShell 执行：

```powershell
.\build
```

脚本会自动初始化仓库内的 SiFli SDK 环境，并使用 GCC、项目本地 board overlay
和 8 路并行任务进行构建。额外的 SCons 参数可以直接附加，例如 `build -c` 清理构建产物。

等价的手动构建命令为：

```bash
source sdk/export.sh
cd work/watch_bt_audio_template/project
scons --board=sf32lb52-lchspi-ulp --board_search_path=../boards -j8
```

Windows 环境先执行 SDK 的 `set_env.bat`，再进入同一 `project` 目录执行
`scons`。工程通过 `SIFLI_SDK` 环境变量定位仓库内 `sdk/`，并沿用 SDK 的
`AddLCPU(...)` 蓝牙控制器镜像。

## 协议测试

Windows PowerShell：

```powershell
.\tests\run_agent_pet_protocol_host_test.ps1
```

脚本优先使用本机 `clang`/`gcc`，不可用时自动使用 WSL 的 `cc`。

## 关键目录

| 路径 | 内容 |
|---|---|
| `sdk/` | OpenSiFli/SiFli-SDK 子模块 |
| `work/watch_bt_audio_template/` | SF32LB525 + LVGL 产品工程 |
| `work/watch_bt_audio_template/src/app_utils/agent_pet_protocol.*` | 纯 C 协议解析器 |
| `work/watch_bt_audio_template/src/app_utils/agent_pet_ble_service.*` | Sibles GATT 接入 |
| `work/watch_bt_audio_template/src/gui_apps/pet/app_pet.c` | Agent 状态 UI |
| `tests/agent_pet_protocol_host_test.c` | 协议主机测试 |
| `docs/agent-pet-hardware-protocol-v1.md` | 冻结的 v1.0 协议 |
| `docs/agent-pet-sf32lb525-integration.md` | 外设接入与联调说明 |

## 文档

- [Agent Pet 硬件协议 v1.0](docs/agent-pet-hardware-protocol-v1.md)
- [SF32LB525 黄山派接入指南](docs/agent-pet-sf32lb525-integration.md)

## 安全边界

v1.0 是桌面端到外设的只读状态同步协议。硬件端不能借此修改 Agent 状态、
批准操作、拒绝操作或执行命令。任务正文、路径、命令和用户输入不会通过 BLE
发送。
