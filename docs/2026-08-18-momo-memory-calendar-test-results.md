# Momo 回忆日历测试记录（2026-08-18）

## 版本与环境

- 分支：`codex/2026-08-18-momo-memory-calendar`
- 产品提交：`98067b5c80f10e95e5932b33670c35f98e91453d`
- 实现方案提交：`6f7a3400a834aaa8ad9920ea08d8f2de15a451c9`
- SiFli SDK：`90e575f...`（仓库固定子模块）
- 工具链：GNU Arm Embedded GCC 14.2.1、SCons（SiFli Python 环境）
- 目标板：`sf32lb52-lchspi-ulp`

## 纯 C 边界与回归测试

实际命令：

```powershell
& .\tests\run_momo_memory_calendar_host_test.ps1
& .\tests\run_agent_pet_protocol_host_test.ps1
& .\tests\run_agent_quest_garden_host_test.ps1
& .\tests\run_agent_pet_audio_protocol_host_test.ps1
```

结果：四条命令均 `exit 0`。回忆日历测试使用
`cc -std=c11 -Wall -Wextra -Werror`，输出
`momo_memory_calendar_host_test: PASS`；其余三项既有回归均输出 `PASS`。

回忆日历用例覆盖：月份边界、2020/2000/2100 闰年规则、非法日期、RTC
无效与恢复、RTC 回拨与恢复、多日跳跃、30/35 天滚动、`delta=29/30`、
最大日期 `2099-12-31`（自然日编号 29220）、3-bit 跨字节边界、30 字节块
逐字节损坏、CRC 恢复、事件幂等、写入门禁、清空候选/提交，以及 32 位
generation 回绕、相等和半周歧义的确定性选择。

## 目标固件完整构建

环境变量按仓库 README 设置 `SIFLI_SDK`、`RTT_CC=gcc`、
`RTT_EXEC_PATH`、`PATH` 和 `PYTHONPATH`。实际命令：

```powershell
scons --board=sf32lb52-lchspi-ulp --board_search_path=../boards -c
scons --board=sf32lb52-lchspi-ulp --board_search_path=../boards -j8
```

结果：clean 和 build 均 `exit 0`，完整启用构建耗时 138.3 秒，生成
`project/build_sf32lb52-lchspi-ulp_hcpu/main.elf` 与固件产物。新增 pet
编译组启用了 `-Wall -Wextra -Werror`，回忆日历模块和适配代码无新增警告。

日志仍有 ON/OFF 均存在的仓库基线警告：FFmpeg/CMSIS 宏重复定义、
media const 限定、Opus `-Wstringop-overread`、ELF RWX LOAD、新库
`_close/_lseek/_read/_write` 桩、entry symbol 和缺少 dfu 镜像提示；本次功能
未扩大这些警告集合。

## Kconfig A/B 资源量化

在同一工作树上仅临时切换
`CONFIG_AGENT_PET_USING_MEMORY_CALENDAR`，每次执行上述目标完整 build，
两次均 `exit 0`；测量后已恢复默认启用配置。

| 配置 | text | data | bss | dec |
|---|---:|---:|---:|---:|
| 关闭 | 5,497,138 B | 16,620 B | 4,864,640 B | 10,378,398 B |
| 启用 | 5,501,882 B | 16,620 B | 4,864,844 B | 10,383,346 B |
| 增量 | +4,744 B | +0 B | +204 B | +4,948 B |

持久化预算为两个固定 30 字节槽加 1 字节活动槽提示；只有日期前移、首次
事件、清空或脏数据恢复时写入，不在 100 ms GUI 定时器中周期写 Flash。
面板 LVGL 对象按需创建并在关闭/页面停止时删除，因此 A/B 的 `bss` 不含
面板运行时堆占用。

## PC 完整模拟器

实际命令：

```powershell
cmd /d /c work\watch_bt_audio_template\simulator\build.bat
```

结果：`exit 2`，在本功能源码编译前被既有 SDK/模拟器环境阻塞。SiFli SDK
PC board 脚本仍尝试映射不存在的 VS2017 14.16 与 Windows SDK
10.0.17763 路径，随后 CmBacktrace 在 `cmb_cfg.h:52` 报
`You must select a CPU platform on menuconfig`。该失败不能作为本功能 GUI
通过证据；目标 MCU 完整构建和纯 C GUI 依赖逻辑已完成编译验证。

## 静态检查

- `git diff --check`：通过。
- 新增核心与测试未使用 `malloc/free/strcpy/sprintf/scanf`。
- 日期字段在窄化转换前验证，数组索引和 3-bit 位移由边界测试覆盖。
- RTC 无效/回拨不写 NVM、不清历史；恢复到合法且不早于 anchor 后再处理。
- 双槽写入后按精确长度读回，校验 CRC/结构并逐字节比较，再提交 RAM 状态；
  active hint 最后更新且失败不破坏有效槽。
- LVGL 调用只在现有 GUI 生命周期/事件/定时器线程；没有新增线程、定时器、
  云端接口或素材。BLE 远端木鱼事件不计入“玩耍”。

## 待真机验收

以下项目未连接真实硬件，明确标记为“待硬件验收”：

1. 连续冷启动/掉电注入，确认双槽回退与 CRC 损坏恢复。
2. RTC 未同步、同步恢复、手工回拨、闰日和跨午夜行为。
3. 本机触摸与 IMU 只各记一次“玩耍”，BLE 远端木鱼不计数；任务领取只在
   首次完成时记“成果”。
4. 打开/关闭日历 100 次观察 LVGL 内存回收；验证 30 格显示、清空二次确认、
   5 秒超时取消及 UI 分配失败降级。
5. 测量频繁事件、日期切换和待机时的 Flash 写次数、刷新开销、功耗与触摸
   响应；确认高优先级输入/错误态可安全关闭面板。
