# Agent Quest Garden 实现方案

## 1. 文档信息与实现闸门

- 日期：2026-08-06
- 产品依据：`docs/2026-08-06-agent-quest-garden-prd.md`
- 产品提交：`7e8f4b2e4c96462c834f6671541ee192ff858935`
- 目标平台：SF32LB525、RT-Thread、SiFli SDK v2.4、LVGL v8
- 目标板：`sf32lb52-lchspi-ulp`
- 范围：只读消费现有 Agent Pet BLE 快照，增加本地任务种子、花园和持久化；不增加 BLE 写回，不增加素材和动态内存
- 实现闸门：本方案单独提交并取得 commit 后，才修改代码

## 2. 现状审查

### 2.1 数据与线程边界

`agent_pet_ble_service.c` 将协议层快照复制到服务状态；`app_pet.c` 的 100 ms `lv_timer` 通过 `AGENTPETBLE_GetStatus()` 读取副本。宠物页创建、刷新、点击和销毁均由 GUI/LVGL 线程执行。现有代码没有从 BLE 回调直接操作 LVGL 对象，新增逻辑延续该边界。

协议 `AGENTPET_SNAPSHOT` 最多包含 12 个 `AGENTPET_SESSION`，每个会话提供 `ucState`、`ulTaskHash`、flags 等字段。`ulGeneration` 用于跳过未变化的快照，但任务完成去重必须基于会话状态转换，不能只依赖 generation。

### 2.2 页面生命周期

`pet_on_start()` 创建页面根对象、3 个 timer 和动画；`pet_on_stop()` 先删除 timer，再关闭 `share_prefs`，最后删除根对象。PC 模拟器通过 `pet_simulator_run/stop()` 复用同一份硬件 UI 源码。新增对象必须挂在页面根对象下，新增状态不得持有页面销毁后的 LVGL 指针。

### 2.3 持久化与测试入口

现有功德计数使用 SDK `share_prefs` 的整数键值接口。SDK v2.4 的偏好名称存在固定长度读取约束，名称使用精确 31 字节。现有主机测试通过 PowerShell/WSL 编译纯 C 协议模块；模拟器的 `SConscript` 自动编译宠物目录全部 `.c` 文件；固件的宠物 `SConscript` 也会自动包含新增 `.c`。

## 3. 架构与文件规划

### 3.1 纯 C 状态机

新增：

- `work/watch_bt_audio_template/src/gui_apps/pet/agent_quest_garden.h`
- `work/watch_bt_audio_template/src/gui_apps/pet/agent_quest_garden.c`

模块不依赖 LVGL、RT-Thread、BLE service 或存储，输入仅为日期和 `AGENTPET_SNAPSHOT`。业务状态使用调用方提供的静态结构体，禁止 `malloc/free`。

公开接口拟定为：

- `QUESTGARDEN_Init(...)`：从已校验的持久化聚合值初始化；非法值回到安全默认值。
- `QUESTGARDEN_ProcessSnapshot(...)`：建立冷启动基线或处理每个任务的状态转换，返回新增种子数和是否需要刷新/保存。
- `QUESTGARDEN_Collect(...)`：最多领取 1 颗待领取种子，更新今日领取和展示状态。
- `QUESTGARDEN_Rollover(...)`：仅在日期有效且单调前进时结算；RTC 未同步或回拨不结算。
- `QUESTGARDEN_GetView(...)`：复制只读视图，供 UI 和持久化适配层使用。

所有指针入口先检查 `NULL`；计数使用饱和加法；数组索引严格小于 12；待领取上限为 8，超出后增加饱和诊断计数。

### 3.2 UI 与存储适配

修改 `app_pet.c`：

- 状态 timer 成功取得新 generation 后，把快照和当前日期交给状态机。
- 在现有宠物页根对象下用 LVGL 基本对象绘制盆、茎、最多 5 片叶、花、计数和 `Collect seed` 点击区；不新增图片、字体或声音。
- 仅 `pending > 0` 时显示可领取提示；一次短点击只调用一次 `QUESTGARDEN_Collect()`。
- 领取点击与现有木鱼点击使用不同对象/回调，不调用任何 BLE API。
- 完成事件触发短暂 `Seed ready` 文案；花园对象只创建一次，后续只改变隐藏状态和文本，避免对象泄漏。
- 页面停止时停止/删除 timer 和动画，再保存、关闭存储、删除根对象。

持久化继续使用 `share_prefs`，采用新的精确 31 字节 namespace，保存版本、日期、今日完成数、今日领取数、待领取数、连续天数和溢出诊断。状态值先写、版本最后写；打开失败或任一写入失败只记录日志并继续 RAM 模式，不影响宠物/BLE 主功能。

## 4. 事件来源与状态机

### 4.1 任务槽

固定 12 个槽，每槽保存 `task_hash`、最近状态、占用标志；另有固定轮换替换索引。规则：

1. 冷启动/页面首次有效快照只建立所有会话基线，不产生种子。
2. 已知 hash 从非 `COMPLETED` 转为 `COMPLETED`：今日完成数饱和加 1；待领取未满 8 时加 1，否则溢出诊断加 1。
3. 重复 `COMPLETED` 幂等，不重复产生种子。
4. 同一 hash 的 `COMPLETED -> RUNNING/IDLE/NEEDS_INPUT/ERROR -> COMPLETED` 可再次产生种子。
5. 新 hash 使用空槽；无空槽时按固定轮换索引替换。被替换任务后来以 `COMPLETED` 返回时仅建立新基线，不奖励，选择保守地少计而不是误计。
6. `ERROR`、`NEEDS_INPUT`、approval flag、断连或无快照不改变花园聚合计数。

`task_hash` 只做本地短期去重，不作为身份或安全依据。哈希碰撞会保守合并状态，记录为已知风险。

### 4.2 日期状态

日期使用 UTC epoch day（`time(NULL) / 86400`），0 表示未知：

- 当前日期为 0：不跨日，仍允许当前 RAM 会话的完成/领取。
- 当前日期等于保存日期：继续当天。
- 当前日期比保存日期大 1：昨日领取数大于 0 时 streak 饱和加 1，否则归零；清空今日完成/领取，保留 pending。
- 向前跳过超过 1 天：streak 归零，清空今日计数，保留 pending。
- 当前日期小于保存日期：视为 RTC 回拨，不重复结算、不清空，输出诊断日志。

pending 跨日保留；叶片和花只代表“今日已领取”，最多展示 5 片叶，超过 5 的领取仍计数但不增加对象。

## 5. LVGL 生命周期与 RT-Thread 约束

- 所有 `lv_obj_*`、`lv_label_*`、`lv_anim_*`、`lv_timer_*` 调用只发生在 LVGL timer、LVGL event 或 GUI app 生命周期回调中。
- BLE/协议线程只发布快照；状态机不在 BLE 回调中运行，无需新增 mutex、队列或 ISR 逻辑。
- timer 回调先验证状态/指针；页面 stop 先删除 timer，防止回调访问已删除对象。
- 动画只绑定页面根对象的子对象；stop 时对相关对象调用 `lv_anim_del()` 或随根对象销毁，不保留跨页面指针。
- 不在 ISR 中执行任何新增逻辑，不增加阻塞、浮点运算或 Flash 帧级写入。

## 6. 固定内存与持久化布局

### 6.1 RAM 预算

预计纯业务状态：

- 12 个去重槽：约 72～96 B（取决于编译器对齐）
- 聚合计数、日期、标志和替换索引：约 32～48 B
- 临时视图/结果：栈上约 32 B
- 合计静态业务 RAM 目标小于 192 B，低于 PRD 的 512 B 上限

LVGL 新增常驻对象上限 11 个：盆 1、茎 1、叶片 5、花 1、统计 label 1、领取按钮 1、按钮 label 1；不新增 timer，复用现有 100 ms 状态 timer 和现有摘要 timer。

### 6.2 Flash 与写放大

- 新代码与常量目标小于 16 KiB，不新增资源 Flash。
- 逻辑记录仅含 7 个 `int32_t`（28 B 有效载荷）；FlashDB 元数据开销由 SDK 决定。
- 仅在新完成事件、领取、跨日和页面退出时保存；动画/100 ms 刷新不写 Flash。
- 多键写不是事务：加载时逐项范围校验；版本缺失/错误则全部使用默认值。部分写入最坏造成保守计数偏差，不允许影响 BLE 或页面启动。

## 7. 错误、溢出与回退

- `AGENTPETBLE_GetStatus()` 失败：保持现有画面和状态，下次 timer 重试。
- 快照会话数大于 12：只处理前 12 项并返回参数/截断诊断；绝不越界。
- 计数达到 `UINT32_MAX`：保持最大值；pending 固定 8。
- `share_prefs_open/set/close` 失败：打印一次或事件级错误，继续 RAM 模式。
- LVGL 创建返回 `NULL`：后续更新函数逐指针检查；花园局部不可用时保留原宠物页。
- RTC 未同步/回拨：不结算，避免重复奖励或清空。
- 编译期提供单一 `PET_QUEST_GARDEN_ENABLED` 开关；关闭后保留现有宠物状态与木鱼体验。若资源、生命周期或协议兼容问题超出预算，回退为只显示今日完成数，不启用持久化/领取 UI。

## 8. 验证矩阵

### 8.1 主机确定性测试

新增 `tests/agent_quest_garden_host_test.c` 和仓库脚本，使用 `-std=c11 -Wall -Wextra -Werror` 编译纯 C 模块，覆盖：

- 冷启动首帧 completed 不计数
- running -> completed 产生 1 颗；重复 completed 幂等
- completed -> running -> completed 再产生 1 颗
- 12 槽边界、13 个新任务的保守替换和哈希碰撞行为
- pending 8 上限与溢出饱和
- 单击一次领取 1 颗、无 pending 点击无变化、显示叶片最多 5
- error/input/approval/断连等上层不调用或不产生奖励
- 日期相同、相邻跨日、跳日、未知日期、RTC 回拨、计数饱和
- 无效版本/越界持久化输入恢复安全默认值

### 8.2 PC 模拟器

为 mock BLE 增加确定性状态序列或测试入口，验证运行、完成、重复、重新运行、离线的 UI 变化；反复启动/停止页面以及 200 次状态变化，确认对象数量稳定、无崩溃、timer 在退出后不运行。若自动化模拟器不能可靠注入触摸，领取动作保留为人工模拟器验收并明确记录。

### 8.3 固件构建

在 `work/watch_bt_audio_template/project` 使用仓库 README 对应命令：

`scons --board=sf32lb52-lchspi-ulp --board_search_path=../boards -j8`

环境固定为用户给定的 SiFli SDK v2.4 Python/SCons/GCC 路径。记录退出码、全部 warning、ELF/BIN/HEX 等产物和构建报告中的 Flash/RAM/size。若缺少同工具链的功能前基线，只报告当前绝对值和可由 map/size 得到的新增对象文件大小，不虚构精确增量。

### 8.4 真机待验收

本次无已声明的已连接硬件，因此以下必须标记“待硬件验收”：

1. 烧录 `sf32lb52-lchspi-ulp` 产物，冷启动、重启和 RTC 未同步启动均可进入宠物页。
2. 手机/桌面端发送 running -> completed，确认只产生一颗种子；重复 completed 不增加。
3. 240 x 240 实际 LCD 检查文字不截断、花园无遮挡、返回手势区域不被领取按钮覆盖。
4. 快速点击、动画中退出、连续进入/退出 200 次，检查无残影、无崩溃、无明显内存下降。
5. 跨 UTC 日、RTC 回拨、断连/重连、掉电恢复 pending 与今日计数。
6. 观察 Flash 写入频率、GUI 帧率、CPU 占用和栈余量；必要时用 RT-Thread heap/stack 诊断和示波/功耗工具复核。

## 9. 提交顺序

1. 本实现方案独立 commit。
2. 纯 C 状态机和主机测试。
3. UI、持久化与模拟器注入。
4. 运行全部仓库明确主机测试、模拟器构建（若环境可用）及真实固件构建。
5. 代码、测试和构建记录合并为一个开发 commit；不 push、不建 PR、不 merge。
