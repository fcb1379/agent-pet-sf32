# Momo 天气时刻测试与资源记录

## 结论

2026-08-08 在 `codex/2026-08-08-lvgl-idea-scan` 分支完成主机测试、PC 模拟器构建/烟测以及 `sf32lb52-lchspi-ulp_hcpu` 目标固件完整构建。天气功能开启态可生成 `main.elf`、`main.bin`、Flash table 与下载脚本；新增代码未产生编译警告。

真实手表的显示效果、触控手感、长时间稳定性和功耗仍标记为“待硬件验收”，本记录不将模拟器或编译结果表述为硬件通过。

## 实际命令与结果

| 验证项 | 实际命令 | 结果 |
| --- | --- | --- |
| 天气状态模块 | `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\run_agent_pet_weather_host_test.ps1` | PASS；C11、`-Wall -Wextra -Werror` |
| Agent Pet 协议回归 | `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\run_agent_pet_protocol_host_test.ps1` | PASS |
| 任务花园回归 | `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\run_agent_quest_garden_host_test.ps1` | PASS |
| 手表协议回归 | `wsl.exe bash -lc "cd /mnt/d/code/sf32/agent-pet-sf32-text-display-fix && bash tests/run_watch_protocol_host_test.sh"` | PASS |
| 构建/烧录工具回归 | `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\build-flash-utils.test.ps1` | 9 项 PASS |
| 手机端协议向量 | `node .\phone_app\test_protocol.mjs` | PASS |
| PC 模拟器构建 | `work\watch_bt_audio_template\simulator\build.bat` | PASS；完整宠物 UI 和天气注入场景完成编译/链接 |
| PC 模拟器烟测 | 隐藏启动 `work\watch_bt_audio_template\project\build_pc_hcpu\main.exe`，8 秒后检查进程 | PASS；进程仍存活，随后由测试主动结束 |
| SF32 目标固件 | `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1` | PASS；输出目录 `work\watch_bt_audio_template\project\build_sf32lb52-lchspi-ulp_hcpu` |

## 天气专项覆盖

- 六种天气枚举、温度/时间/TTL/flags 边界和非法长度/空指针。
- CRC、协议版本、单帧长度以及天气消息类型 5 的 AP 20 字节帧集成。
- 重复序号 100 次、旧序号、半区间歧义和 `65535 -> 0` 回绕。
- RTC 有效、RTC 回拨/不可用、TTL 到期，以及 RT tick 原始值回绕。
- 每序号一次互动、三小时冷却、天气条件资格和 20 组显示优先级组合。
- PC 注入依次覆盖晴、云、雨、雪、热、冷、过期、暴风和 Agent 高优先级屏蔽。

## Flash/RAM 实测

使用同一源码和工具链，仅切换 `CONFIG_AGENT_PET_WEATHER_MOMENTS` 后完整重建，并用：

`C:\Users\woan\.sifli\tools\arm-none-eabi-gcc\14.2.1\bin\arm-none-eabi-size.exe work\watch_bt_audio_template\project\build_sf32lb52-lchspi-ulp_hcpu\main.elf`

| 配置 | text | data | bss | 总计 |
| --- | ---: | ---: | ---: | ---: |
| 功能关闭 | 3,751,888 B | 12,052 B | 3,972,692 B | 7,736,632 B |
| 功能开启 | 3,753,808 B | 12,052 B | 3,972,836 B | 7,738,696 B |
| 净增 | **1,920 B** | **0 B** | **144 B** | **2,064 B** |

净增低于 PRD 的 Flash 64 KiB、常驻 RAM 2 KiB 预算。测量结束后已恢复 `CONFIG_AGENT_PET_WEATHER_MOMENTS=y`，并重新完成开启态目标构建。

## 已知构建警告

目标构建仍会报告 SDK/工具链原有警告，包括 FlashDB/LVGL/audio 的潜在空指针诊断、Bluetooth `bts2_init` 参数大小诊断、ANCS packed member、RWX LOAD segment、newlib `_close/_lseek/_read/_write` 未实现，以及 Flash table 缺省 entry。天气新增源文件没有产生警告；这些基线警告未在本功能分支扩大处理范围。

PC 构建仍有 SDK/LVGL 的 MSVC 枚举转换、FlashDB signedness、`LNK4098` 和旧 Visual Studio/Windows Kit 路径提示；模拟器链接成功。

## 待硬件验收

1. 刷入最终开启态镜像，验证启动、BLE 连接和现有表情/GIF/任务花园没有回归。
2. 依次发送晴、云、雨、雪、暴风、冷热 flags，确认氛围层在 1 秒内出现且不卡顿。
3. 在图片传输、Agent 非空闲、打字、远程非基础表情、任务反馈期间确认天气被正确让位。
4. 点击可互动天气，确认只触发一次关怀反馈；同序号及三小时内的新序号不重复触发。
5. 验证过期天气消失、重启后的单调时钟回退策略、RTC 同步/回拨，以及 RT tick 回绕附近行为。
6. 连续运行至少 30 分钟，观察 FPS、触控响应、堆内存、温升与功耗；这些指标目前没有真实硬件数据。
