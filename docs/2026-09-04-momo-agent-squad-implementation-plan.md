# Momo Agent 小队雷达实现方案

## 1. 文档信息

| 字段 | 内容 |
| --- | --- |
| 日期 | 2026-09-04 |
| 对应 PRD | `docs/2026-09-04-momo-agent-squad-prd.md` |
| 产品提交 | `9e8d1d1b9f616a4463acbf2a2639dc0719bdf73a` |
| 基线 | `origin/master@a2805abeb72f02a726dce5dc49857280ba9cdc78` |
| 目标板 | `sf32lb52-lchspi-ulp` |
| 结论 | 可实施受控 MVP；首版只读、自动轮播、零新增触摸所有权 |

## 2. 约束与设计决策

1. 只消费 `AGENTPETBLE_GetStatus()` 在临界区内复制的 `AGENTPET_SNAPSHOT`，不直接访问 BLE 发布区。
2. 不修改 Agent Pet v1 帧格式，不新增上位机要求、任务、队列、锁、协议字段、持久化或外部素材。
3. 视图模型使用固定数组和有界插入排序；禁止 `malloc/free`、不稳定 `qsort` 和递归。
4. 所有 LVGL 对象只在宠物页的 LVGL 线程创建、更新和随根对象删除；BLE 回调不排序、不格式化、不调用 LVGL。
5. 自动轮播复用现有 `status_timer`，不创建新 timer。首版不注册任何新增触摸事件，避免与四边返回、木鱼、未来抚摸和玩球竞争输入。
6. 功能由 `CONFIG_MOMO_AGENT_SQUAD` 单一开关控制。关闭后编译器不包含视图模型与新增对象，恢复原两行状态展示和 `PET_SelectSession()`。

## 3. 模块与接口

新增纯 C 模块：

- `src/gui_apps/pet/momo_agent_squad.h`
- `src/gui_apps/pet/momo_agent_squad.c`

核心结构：

- `MOMO_AGENT_SQUAD_VIEW`
  - 五种状态计数，各 `uint8_t`；
  - 最多 5 个已排序的原快照索引；
  - 可见数量、隐藏数量和默认选中位置；
  - 总业务状态预计不超过 16 B。
- `MOMO_AGENT_SQUAD_IDENTITY`
  - `task_hash + provider + source`，仅用于刷新时尽量保持本地浏览位置；
  - 明确不作为权限、安全身份或回传字段。

接口职责：

1. `MOMOAGENTSQUAD_BuildView()`：校验空指针、`session_count<=12` 和每条状态范围，统计状态，以固定上限稳定排序，计算 5 个代表项和 `+N`，并在不被更高优先级抢占时保持原选择。
2. `MOMOAGENTSQUAD_Next()`：在可见项内循环推进，空视图安全返回 0。
3. `MOMOAGENTSQUAD_GetSession()`：检查视图位置、原索引和快照边界后返回只读会话。
4. `MOMOAGENTSQUAD_FormatSummary()`：使用调用方固定缓冲区与 `snprintf` 输出 `R/N/D/E/I` 计数及离线标记。
5. `MOMOAGENTSQUAD_FormatDetail()`：只输出 Provider、低 16 位哈希、状态、审批提示和有限年龄；不输出任务正文、路径或命令。

所有接口均返回 `bool` 或显式安全值，输入异常不产生部分可用视图。

## 4. 排序与选中规则

每条会话生成一个原始索引，并按以下键稳定插入：

1. 状态优先级：`ERROR > NEEDS_INPUT > COMPLETED > RUNNING > IDLE`。
2. 同状态下有效 `age_seconds` 升序，即越近期越靠前。
3. `65535` 为未知年龄，排在有效年龄之后。
4. 键完全相同时不交换，保留原快照顺序。

只保留排序后的前 5 个索引，隐藏数量为 `session_count-visible_count`，所有减法在大小关系验证后执行。

快照更新时：

- 如果旧选择的 `task_hash + provider + source` 仍在代表项中，且代表首项没有比它更高的状态优先级，则保持选择；
- 如果出现更高优先级项，默认选择位置归零，在一次现有刷新周期内抢占；
- 找不到旧选择时归零；
- 自动轮播只遍历代表项，不修改原快照，不写 Flash，不发 BLE。

## 5. UI 对象树与布局

复用现有：

- `status_label`：改为紧凑计数摘要；
- `task_label`：改为当前代表会话详情。

新增并在页面生命周期内复用：

- 5 个不可点击圆点对象，颜色复用 `PET_StateColor()`；
- 1 个 `+N` 标签，只有隐藏项大于 0 时显示。

共新增 6 个常驻 LVGL 对象，低于 8 个预算；没有容器、命中区或新 timer。对象位于屏幕底部三行区域，使用 `LV_VER_RES_MAX` 相对坐标，Momo、attention panel、图片进度层和打字场景仍保持原层级。新增对象不可点击、不可滚动；attention panel 和图片进度层继续位于其后创建并覆盖前景。

## 6. 刷新、轮播与生命周期

### 6.1 刷新路径

`PET_RefreshStatus()` 已由 LVGL `status_timer` 每 100 ms 调用：

1. 调用 `AGENTPETBLE_GetStatus()` 获得一致只读副本。
2. 只有代数或连接状态变化时重建 `MOMO_AGENT_SQUAD_VIEW`、统计摘要、点颜色和 `+N`。
3. 复用 `rt_tick_get_millisecond()` 累计 3000 ms 轮播节奏；达到期限时只推进可见位置并更新详情。
4. 使用无符号 tick 差值比较处理回绕，不依赖绝对时间。
5. 空快照显示原有等待/断连文案并隐藏全部状态点。

固定 12 条输入的构建复杂度上限为 66 次比较，100 ms 回调中只在新代数时执行；普通 tick 仅做常数次判断。

### 6.2 页面退出

- 不新增 timer，因此沿用 `status_timer` 先删除、再删除根对象的顺序。
- 新对象均是 `root` 子对象，由 `lv_obj_del(root)` 统一回收。
- `rt_memset(&g_pet_ui, 0, ...)` 清除对象指针、轮播 tick 和视图状态，重新进入不恢复瞬时选中位置。
- 视图模型不保存 LVGL 指针，也无异步回调，页面退出后不会访问已删除对象。

## 7. 并发、RTOS 与错误处理

- BLE/协议层继续在既有临界区内复制快照；本功能不增加共享写状态。
- 排序和字符串格式化只发生在 GUI 线程，不拉长 BLE 临界区。
- 不创建 RTOS 任务、队列、信号量或锁，不改变优先级和栈配置。
- 对快照数量、状态、索引、输出缓冲区和会话指针逐层检查；非法输入使视图清零并返回失败。
- `snprintf` 返回负值或结果超出缓冲区时返回失败，并保证末尾 NUL；UI 使用固定回退文案。
- 未知 Provider/来源只显示通用 `Agent`，未知状态在视图模型入口被拒绝。

## 8. 资源预算

| 资源 | 方案预算 | 验证方式 |
| --- | ---: | --- |
| 业务状态 | `< 128 B`，目标约 32 B | 主机 `_Static_assert`/运行时 `sizeof` 与 map/size 对照 |
| LVGL 对象 | 6 个 | 代码审查与模拟器/真机 |
| 新增 timer | 0 | 搜索 `lv_timer_create` 与代码审查 |
| 新增任务/队列/锁 | 0 | 代码审查 |
| Flash | `< 6 KiB` | 同工具链开关 OFF/ON clean 构建 size 差值 |
| RAM/BSS | 记录实测，业务结构 `<128 B` | OFF/ON ELF size 差值 |
| 持久化/协议/素材 | 0 | Git diff 与协议测试 |

## 9. 构建与测试矩阵

### 9.1 主机纯 C 测试

新增 `tests/momo_agent_squad_host_test.c` 和运行脚本，编译参数至少为：

```text
-std=c11 -Wall -Wextra -Werror
```

覆盖：

- 0、1、5、6、12 条会话；
- 五状态优先级、年龄未知、同键稳定性；
- `+N`、自动轮播和保持/失效选择；
- 高优先级到达抢占；
- 空指针、超范围数量、非法状态、非法索引和小缓冲区；
- 1000 组确定性伪随机合法快照，核对计数、排序、索引唯一性和边界；
- 原快照在构建和格式化后逐字节不变。

同时回归既有 Agent Pet 协议、音频协议和任务花园主机测试。

### 9.2 目标构建

在项目真实构建环境中分别执行：

```text
scons --board=sf32lb52-lchspi-ulp --board_search_path=../boards -c
scons --board=sf32lb52-lchspi-ulp --board_search_path=../boards -j8
```

- OFF：关闭 `CONFIG_MOMO_AGENT_SQUAD`，完整 clean 构建；
- ON：开启该开关，完整 clean 构建；
- 保存实际命令、编译结果、警告和 ELF text/data/bss 差值。

### 9.3 模拟器与硬件

- 按 `README_SIMULATOR.md` 执行 `simulator/build.bat`；若现有 SDK、MSVC 或平台桩阻断，记录真实首个阻断，不把静态检查当作通过。
- 模拟器核对 390 x 490 下对象创建/删除、0/5/12 项和最长固定文案。
- 390 x 450 真机核对信息密度、GIF/JPEG/打字/attention 前景、断连保留与重连替换、1000 次快照刷新、200 次页面进出及体感/木鱼返回手势。
- 无真实硬件时上述项目统一标为“待硬件验收”，不得虚报。

## 10. 回退策略

发现以下任一情况时关闭 `CONFIG_MOMO_AGENT_SQUAD`：Flash 增量达到 6 KiB、业务状态达到 128 B、布局遮挡、刷新卡顿、生命周期异常或与输入/高优先级层冲突。关闭后不创建新增对象、不编译排序与轮播路径，原协议、快照和既有宠物页数据不迁移、不丢失。

