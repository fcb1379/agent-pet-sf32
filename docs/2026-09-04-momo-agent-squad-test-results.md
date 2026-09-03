# Momo Agent 小队雷达自测记录

## 1. 测试对象

| 项目 | 结果 |
|---|---|
| 分支 | `codex/2026-09-04-momo-agent-squad` |
| 基线 | `origin/master@a2805abeb72f02a726dce5dc49857280ba9cdc78` |
| PRD | `9e8d1d1b9f616a4463acbf2a2639dc0719bdf73a` |
| 实现方案 | `6e5aa67e161bd20d3792fbe9521918eb9ece7b95` |
| 目标板 | `sf32lb52-lchspi-ulp` |
| 固定 SDK | `90e575f7ec253b6a879d816460a16546ebd2582f` |
| 主机编译器 | Ubuntu GCC 11.4.0 |
| 目标编译器 | Arm GNU Toolchain 14.2.1 |

## 2. 主机测试

执行命令：

```powershell
powershell.exe -ExecutionPolicy Bypass -File tests\run_agent_pet_protocol_host_test.ps1
powershell.exe -ExecutionPolicy Bypass -File tests\run_agent_pet_audio_protocol_host_test.ps1
powershell.exe -ExecutionPolicy Bypass -File tests\run_agent_quest_garden_host_test.ps1
powershell.exe -ExecutionPolicy Bypass -File tests\run_momo_agent_squad_host_test.ps1
```

结果：全部通过。

```text
PASS agent_pet_protocol_host_test
agent_pet_audio_protocol_host_test: PASS
PASS agent_quest_garden_host_test
Momo Agent squad host tests passed (view=13 bytes, identity=8 bytes, random=1000).
```

新增测试覆盖：

- 0、1、5、6、12 条会话和 `+N` 计数；
- `ERROR > NEEDS_INPUT > COMPLETED > RUNNING > IDLE`；
- 同状态年龄升序、未知年龄排末和相同排序键的原始顺序稳定性；
- 自动轮播、旧选择保持和更高优先级到达时抢占；
- 空指针、非法状态、非法数量、非法索引和不足输出缓冲区；
- 1000 组确定性随机合法快照，检查索引范围、唯一性、排序和输入快照不变。

主机编译参数为 `-std=c11 -Wall -Wextra -Werror`，新增纯 C 模块没有警告。

## 3. 目标固件 clean A/B 构建

实际命令（在 `work/watch_bt_audio_template/project` 执行）：

```powershell
. 'D:\code\sf32\agent-pet-sf32-text-display-fix\sdk\export.ps1'
scons --board=sf32lb52-lchspi-ulp --board_search_path=../boards -c
scons --board=sf32lb52-lchspi-ulp --board_search_path=../boards -j8
arm-none-eabi-size.exe build_sf32lb52-lchspi-ulp_hcpu\main.elf
```

OFF 使用 `# CONFIG_MOMO_AGENT_SQUAD is not set`，ON 使用
`CONFIG_MOMO_AGENT_SQUAD=y`；两者均从 clean 状态完整构建成功，最后保留 ON 配置。

| 配置 | text | data | bss | dec | 结果 |
|---|---:|---:|---:|---:|---|
| OFF | 5,497,138 | 16,620 | 4,864,640 | 10,378,398 | 通过 |
| ON | 5,498,618 | 16,620 | 4,864,692 | 10,379,930 | 通过 |
| 增量 | +1,480 | 0 | +52 | +1,532 | 低于 6 KiB Flash 门槛 |

最终 ON ELF 中存在 `MOMOAGENTSQUAD_BuildView`、`Next`、`GetSession`、
`GetIdentity`、`FormatSummary` 和 `FormatDetail` 符号。OFF 构建日志不编译
`momo_agent_squad.c`，关闭开关可恢复原宠物页路径。

构建中仍可见 SDK/三方库既有告警，包括 LVGL 空指针静态告警、FFmpeg
数组读取/宏兼容告警、newlib 未实现 syscall、RWX LOAD 段和 ftab 无入口；
新增 `App_pet_motion` 源码组使用 `-Wall -Wextra -Werror` 并通过，没有新增
源码告警。

## 4. PC 模拟器门禁

执行命令：

```powershell
$env:SIFLI_SIM_SDK='D:\code\sf32\agent-pet-sf32-text-display-fix\sdk'
cmd.exe /c work\watch_bt_audio_template\simulator\build.bat
```

结果：未通过，构建在进入宠物功能源码前被固定 SDK 的 PC 配置阻断：
`external/CmBacktrace-v1.3.0/cmb_cfg.h:52` 报
`C1189: You must select a CPU platform on menuconfig`。同时脚本仍尝试旧
VS2017/Windows SDK 映射，但当前 MSVC 已能启动编译。该失败与新增模块无关，
本次未修改 SDK 或扩大范围绕过基线门禁，因此没有宣称模拟器通过或完成视觉验收。

## 5. 资源、并发和安全检查

| 项目 | 实测/检查结果 |
|---|---|
| 业务静态状态 | 视图 13 B + 临时身份 8 B + 轮播 tick 4 B，共 25 B |
| LVGL 对象 | 5 个状态点 + 1 个 `+N` 标签，共 6 个 |
| 新 timer / RTOS 任务 / 队列 / 锁 | 0 / 0 / 0 / 0 |
| 新协议字段 / Flash 存储 / 外部素材 | 0 / 0 / 0 |
| 输入事件 | 0；新增对象清除 CLICKABLE，未注册触摸回调 |
| 动态内存 | 纯 C 视图模块无 `malloc/free`；LVGL 对象仅在页面创建期创建 |
| 快照所有权 | 只读使用 `AGENTPET_BLE_GetStatus()` 返回的一致副本，不持有跨周期指针 |
| LVGL 线程 | 创建、刷新和销毁均沿用宠物页 LVGL 生命周期；算法模块不调用 LVGL/RTOS |
| 数组/指针边界 | 所有入口检查空指针；会话数、状态、可见位置和源索引均检查上限 |
| 刷新复杂度 | 最多 12 条的稳定插入排序；仅快照变化时重建，3 秒轮播复用现有 100 ms 状态 timer |

## 6. 待硬件验收

以下项目必须在真实设备上验证，当前状态均为“待硬件验收”：

1. 1、5、6、12 会话时状态点、`+N`、摘要和详情在 390×450 屏幕无遮挡；
2. ERROR/NEEDS_INPUT 高优先级提示层仍完整可见，状态到达后一个刷新周期内抢占；
3. 3 秒轮播节奏、Momo GIF、图片同步、木鱼、任务花园和摸摸交互无卡顿或抢触摸；
4. 页面反复进入/退出、BLE 断连重连、快照乱序和 12 会话压力下无崩溃或残留对象；
5. 连续运行至少 30 分钟，检查帧率、堆水位和功耗回归。

## 7. 结论

算法、错误路径、既有协议回归、目标 ON/OFF clean 构建和资源预算均通过。
PC 模拟器受固定 SDK 的既有 PC 配置错误阻断，视觉和真实硬件行为仍需验收；
在这些限制被明确保留的前提下，功能达到可提交并交付硬件验证的状态。
