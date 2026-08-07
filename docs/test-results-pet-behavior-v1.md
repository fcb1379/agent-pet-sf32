# Agent Pet 多状态自主行为测试记录 v1

日期：2026-08-07
分支：`codex/2026-08-07-pet-behavior-engine`

## 自动化结果

| 项目 | 实际命令 | 结果 |
|---|---|---|
| 行为模型与素材映射主机测试 | `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\run_pet_behavior_host_test.ps1` | PASS，MSVC `/W4 /WX` |
| 原 Agent Pet 协议回归 | MSVC `cl /std:c11 /utf-8 /W4 /WX` 编译并运行 `agent_pet_protocol_host_test` | PASS |
| 原任务花园回归 | MSVC `cl /std:c11 /utf-8 /W4 /WX` 编译并运行 `agent_quest_garden_host_test` | PASS |
| 烧录工具回归 | `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\build-flash-utils.test.ps1` | PASS |
| SF32LB52 完整固件 | `powershell.exe -NoProfile -ExecutionPolicy Bypass -File build.ps1` | PASS，目标 `sf32lb52-lchspi-ulp` |
| PC 完整手表模拟器 | `work\watch_bt_audio_template\simulator\build.bat` | PASS |
| 模拟器运行冒烟 | 启动 `project\build_pc_hcpu\main.exe`，观察 5 秒后自动关闭 | PASS，进程持续存活 |
| 差异与安全扫描 | `git diff --check`；扫描新增纯 C 模块中的禁用函数 | PASS |

行为主机测试覆盖：首次点击、三击/六击、点击/长按/动作冷却、反应到期、Agent
覆盖与恢复、Sleepy/Lonely、损坏持久化数据、RTC 延迟同步、60 秒唤醒度累计、
30 分钟精力累计、32 位 tick 回绕、相同种子确定性，以及 11 个状态素材映射完整性。

## 资源结果

`arm-none-eabi-size main.elf`：

| 版本 | text | data | bss |
|---|---:|---:|---:|
| 修改前同分支构建 | 3,740,848 B | 12,044 B | 3,967,860 B |
| 当前构建 | 3,744,980 B | 12,044 B | 3,967,940 B |
| 增量 | +4,132 B | 0 B | +80 B |

行为模型自身由测试约束为不超过 256 B，且不使用动态内存。状态 GIF 仍沿用现有
PSRAM 解码路径；任一时刻只保留一个 GIF decoder 和一套显示帧。

## 已知构建提示

- 固件链接仍报告仓库原有的 RWX segment、newlib syscall stub、ftab entry 和缺失
  `dfu` image 提示；本功能对应源文件均编译成功，没有新增编译错误。
- PC 构建脚本仍尝试旧 VS2017/Windows SDK 的盘符映射，随后使用 VS2022 正常完成；
  LVGL 枚举与 CRT 配置警告为模拟器现有警告。

## 待硬件验收

本轮没有连接和烧录真实设备，以下项目必须标记为“待硬件验收”：

1. 烧录后进入 Pet 页面，确认默认 `Calm` 标签、背景色和缓慢动作正常。
2. 单击、三连击、六连击和长按，确认 Happy/Celebrate/Annoyed 及冷却符合 PRD。
3. 开启 Motion 后轻摇与撞击，分别确认 Curious/Startled，并检查日常佩戴误触发。
4. 通过串口执行 `petmood status` 查看当前模型；可用 `petmood tap`、
   `petmood comfort`、`petmood motion`、`petmood impact` 注入事件。
5. 可选地把状态 GIF 放到设备文件系统 `/pet/<state>.gif`，确认切换；删除或放入
   损坏文件后确认安全回退 `/pet.img` 或内置 mascot。
6. 页面反复进入/退出 100 次，检查 timer、PSRAM 和持久化；随后执行 4 小时稳定性、
   1000 次状态切换和功耗对比。
