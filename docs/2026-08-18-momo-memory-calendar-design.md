# Momo 回忆日历实现方案

## 1. 范围、基线与结论

- 目标分支：`codex/2026-08-18-momo-memory-calendar`
- 产品基线：`98067b5c80f10e95e5932b33670c35f98e91453d`
- 固件基线：`origin/master@a2805abeb72f02a726dce5dc49857280ba9cdc78`
- 目标板：`sf32lb52-lchspi-ulp`
- 结论：MVP 可在现有 Pet GUI、RTC、Quest Garden 和 `share_prefs` 上独立闭环。实现新增一个纯 C 日期/历史状态模块及一个 Pet GUI 适配层，不新增线程、定时器、素材、网络或协议字段。

本方案只实现最近 30 个本地自然日的三类布尔回忆：相见、玩耍、成果。不会保存 Agent 文本、任务标题、录音、次数、耗时或身份信息，也不会复制扫描来源的代码、结构、阈值、布局、颜色或文案。

## 2. 现状与复用点

### 2.1 GUI 与事件

- `app_pet.c` 的 `pet_on_start()`、`PET_RefreshStatus()`、`PET_PlayWoodenFishAnimation()`、`PET_CollectQuestSeed()` 和 `pet_on_stop()` 都运行在 LVGL/GUI 线程。
- `PET_PlayWoodenFishAnimation()` 同时被本机触摸、IMU 和 BLE 远端木鱼事件调用，不能作为回忆事件的统一入口。触摸仅在 `PET_PlayWoodenFish()` 的本机 `LV_EVENT_SHORT_CLICKED` 已确认且动画调用返回后记录；IMU 仅在 `PET_MotionDetectorUpdate()` 返回 true、现有动作确认并调用动画后记录。BLE `bHasWoodenFishEvent` 路径只播放既有动画，不计入本地“玩耍”，从而避免远端重放污染回忆。
- Quest Garden 只有 `QUESTGARDEN_Collect()` 返回 `bChanged=true` 时才代表成功领取，因此在该分支记录“成果”，不会把快照重放或重复领取当作新回忆。
- “相见”只在 Pet 页面对象、持久层与日历状态初始化完成后发送，重复进入由日期事件位幂等门禁消除。

### 2.2 RTC

- 手机时间同步把时区换算后的本地年月日/时分秒写入硬件 RTC；现有日功德模块使用 `time()` + `gmtime_r()` 取得本地 RTC 日历字段。
- 回忆模块沿用该真实 RTC 语义，但不直接比较 `YYYYMMDD` 数值；适配层读取年月日后交给纯 C 公历校验与自然日序号转换函数。
- 支持年份限定为 2020—2099。此范围覆盖当前产品 RTC 能力并避免无效初始值。超范围、`time()<86400`、转换失败均视为 RTC 无效。

### 2.3 持久化

- SDK `share_prefs` 提供 `get_block`/`set_block`，底层 FlashDB 单键写入，不提供多键事务。已核对真实实现：`set_block` 在 `fdb_kv_set_blob` 返回 `FDB_NO_ERR` 时返回 `RT_EOK`；`get_block` 在缓冲区足够时返回实际读取字节数，缓冲区不足时返回负差值。适配层因此只接受 `get_block == sizeof(persisted)`，其他返回值都按无效/读取失败处理。
- 采用两个完整槽 `mem_a`/`mem_b`，每个槽自身包含 magic、版本、长度、generation、锚点日、30×3-bit 历史和 CRC。保存写入非活动槽并立即回读校验；只有回读一致才更新 RAM 活动槽。启动同时校验两槽并选择 generation 更新的有效槽。
- 此策略不删除旧槽：掉电发生在新槽写入前或写入中时旧槽仍有效；掉电发生在新槽完整提交后时启动选择新槽。generation 使用 32 位串行号比较：`A-B` 位于 `1..0x7FFFFFFF` 时 A 更新，位于 `0x80000001..0xFFFFFFFF` 时 B 更新；相等或恰好相差 `0x80000000` 的歧义情况使用已校验的槽提示键作确定性选择。generation 自然回绕，不使用普通 `>` 比较；提示键写失败不影响非歧义恢复。

## 3. 模块与接口

### 3.1 纯 C 模块 `momo_memory_calendar.[ch]`

该模块不包含 RT-Thread、LVGL 或 `share_prefs` 头文件，可由宿主机直接编译测试。

主要常量：

- `MOMO_MEMORY_DAY_COUNT = 30`
- `MOMO_MEMORY_EVENT_MEET = 1 << 0`
- `MOMO_MEMORY_EVENT_PLAY = 1 << 1`
- `MOMO_MEMORY_EVENT_ACHIEVEMENT = 1 << 2`
- `MOMO_MEMORY_EVENT_ALL = 0x07`

RAM 状态：

- `aEvents[30]`：从旧到新，每项只允许低 3 bit；30 B。
- `ulAnchorDay`：最后可信且不回拨的自然日序号；4 B。
- `ulGeneration`：当前持久化代号；4 B。
- `ucPendingEvents`：RTC 无效或回拨期间只驻留 RAM 的事件位；1 B。
- `ucActiveSlot`、`bRtcInvalid`、`bRollback`、`bDirty`：运行状态。

外部接口：

- `MOMOMEMORY_DateToDay(year, month, day, *day)`：严格公历校验并转换为单调自然日序号。
- `MOMOMEMORY_DayToDate(day, *date)`：供 UI 显示，覆盖月末、年末和闰日。
- `MOMOMEMORY_Init(context, persistedA, persistedB, preferredSlot)`：校验 magic/version/size/保留位/事件位/锚点/CRC，按 32 位串行号规则选择最新有效槽；两槽均坏则安全回空。
- `MOMOMEMORY_ProcessDay(context, currentDay, *result)`：处理首次可信日期、前进一天、多日跳跃、30 天窗口淘汰、回拨及待处理事件合并。
- `MOMOMEMORY_Record(context, currentDay, event, *result)`：只接受单个内部事件枚举；首次 0→1 才置 `saveRequired`，重复事件幂等。
- `MOMOMEMORY_Clear(context, currentDay, *result)`：生成空快照候选；持久化成功后才替换可见 RAM 状态。
- `MOMOMEMORY_Export(context, generation, *persisted)` 与 `MOMOMEMORY_Validate()`：固定布局编解码和 CRC。
- `MOMOMEMORY_GetDay(context, offset, *events)`：只读 0—29 索引；越界返回 false。

操作结果固定包含 `bChanged`、`bSaveRequired`、`bRtcInvalid`、`bRollback` 和错误枚举。适配层只根据结果进行存储与 UI 刷新。

### 3.2 Pet 适配层

初版适配直接放在 `app_pet.c` 的 `#if defined(AGENT_PET_USING_MEMORY_CALENDAR)` 区域，以减少跨层指针和生命周期复杂度；算法和持久化布局仍由独立模块拥有。

适配职责：

1. `PET_MemoryCurrentDay()`：从 RTC 读取本地年月日，并调用纯 C 转换；失败返回 0。
2. `PET_LoadMemoryCalendar()`：打开专用 private prefs，读取双槽和提示键，初始化上下文；存储不可用则保留 RAM 功能并禁用保存。
3. `PET_RecordMemory(event)`：调用纯 C 状态机；只有 `bSaveRequired` 才执行一次双槽保存，不在每个 100 ms tick 写 Flash。
4. `PET_SaveMemoryCalendar()`：导出下一 generation 到非活动槽、`set_block`、`get_block` 回读及逐字节/CRC校验，成功后提交上下文活动槽；失败保留原可见状态和旧槽并记录错误。
5. `PET_UpdateMemoryCalendarDay()`：复用 `PET_RefreshStatus()` 的现有 100 ms LVGL timer，只执行一次 RTC 日读取和整数比较；同日无写操作。跨日或待处理事件合并时才保存/刷新。
6. `PET_CreateMemoryCalendar()` / `PET_DestroyMemoryCalendar()`：创建与回收入口、面板、30 个固定点、摘要和清空确认对象。全部调用只发生在 GUI 线程，持久层不保存 LVGL 指针。

## 4. 数据布局、完整性与原子性

持久化结构采用显式定宽字段和字节数组，不依赖编译器位域顺序。预期有效载荷不超过 40 B：

| 字段 | 大小 | 说明 |
|---|---:|---|
| magic | 4 B | 本项目独立常量 |
| version | 1 B | 初版为 1 |
| structure size | 1 B | 截断/未知布局门禁 |
| reserved | 2 B | 必须为 0 |
| generation | 4 B | 双槽新旧判定 |
| anchor day | 4 B | 最近可信自然日序号，0 表示空/未知 |
| packed events | 12 B | 30×3 bit=90 bit，末尾未用 bit 必须为 0 |
| CRC16-CCITT | 2 B | 覆盖 CRC 字段前所有字节 |

实现通过逐字节打包/解包，不把结构体直接强转为外部数据；添加编译期大小门禁。CRC、未知版本、长度、日期范围、事件高位或未用 bit 任一不合法都判为坏槽。

保存流程：

1. 从当前状态构造候选上下文；不立即覆盖当前可见状态。
2. generation 按 32 位无符号数加一并允许自然回绕，导出到非活动槽；启动端按上述串行号规则比较。
3. `share_prefs_set_block()` 写完整槽。
4. `share_prefs_get_block()` 回读到固定栈对象，验证长度、CRC、字段和内容一致。
5. 回读成功后更新活动槽/当前上下文；再写提示键。提示键失败只记诊断。
6. 任一步失败都保留旧槽和原可见状态；当天事件可保留在 `ucPendingEvents`，后续离散事件或页面关闭时重试，但不在 timer 中持续刷写。

清空也写入一个新的“空快照”，不调用 `share_prefs_clear/remove`，因此失败时旧记录仍可见且可重启恢复。

## 5. 日期、回拨与状态机

### 5.1 日期算法

- 使用公历规则：能被 400 整除为闰年；能被 100 整除但不能被 400 整除不是闰年；其余能被 4 整除为闰年。
- 自然日序号由 2020-01-01 起累计，转换用有界循环和无符号溢出检查；不依赖字符串排序或主机时区。
- 月/年/闰日转换由主机测试覆盖 2020-02-29、2021-02-28/03-01、跨年和 2099 边界。

### 5.2 历史滚动

- `current == anchor`：只处理尚未置位的事件。
- `current > anchor` 且差值 1—29：历史左移差值，尾部补空；不会为跳过的日期伪造事件。
- 差值 >=30：清空窗口，将新日作为最新一格。
- `current < anchor`：进入回拨只读状态，不移动锚点、不覆盖或清空未来记录、不写 Flash；新事件只合并到 RAM pending。日期恢复到 anchor 或更晚时再关联到可信当天。
- RTC 年份越界、非法月日、`time()<86400` 或转换失败：不创建 1970/零日期，不移动锚点、不清历史、不写 Flash，事件只驻留 pending；当前页面显示“时间同步后保存”。恢复为合法且不早于 anchor 的日期后再处理。

### 5.3 运行状态

`UNAVAILABLE -> READY -> DIRTY -> SAVING -> READY`，错误进入 `RAM_ONLY`。RTC 维度附加 `RTC_INVALID`/`ROLLBACK` 标志，不阻断 Pet 主功能。

- 初始化存储失败：RAM_ONLY，UI 可显示当前会话，不写 Flash。
- 两槽损坏/未知版本：以空历史启动，打印日期/错误码诊断；首次有效事件写出新版本。
- 写入失败：恢复提交前上下文，旧槽仍有效；不阻塞 Agent、BLE、GIF 或音频。
- UI 对象创建失败：删除已创建的面板子树或隐藏入口，数据模块继续工作。

## 6. UI、交互与优先级

- 在 Pet 根页面的独立角落创建“回忆”小按钮，不复用宠物主体、木鱼、IMU、Quest Garden 领取或系统返回手势命中区。
- 面板为 Pet 根对象子树：标题、状态提示、30 个固定回忆点、选中日期摘要、关闭按钮和“清空”按钮；清空按钮第一次点击进入确认态，第二次确认才保存空快照，关闭/超时取消确认。
- 30 个点使用形状+明暗组合：空白、仅相见、含玩耍、含成果、三类齐全可由边框/填充/小标记区分，不单靠红绿。
- 面板对象数控制在 40 个以内：容器 1、标题/摘要/提示 3、入口/关闭/清空 3、30 个点，共约 37 个。
- 面板打开时如图片接收、明确 PLAY/RESTORE、typing、Agent `NEEDS_INPUT`/`ERROR` 等高优先级状态到来，则关闭面板且不补播；数据记录仍继续。
- 不新增动画 timer。新增事件只更新当天点和短文案；不改变 stage/GIF、木鱼动画或音频仲裁。
- 页面停止先关闭 prefs，再删除根对象；因为没有异步工作线程或独立 timer，不会有回调访问已删除对象。

## 7. 编译开关与构建隔离

- 新增 `AGENT_PET_USING_MEMORY_CALENDAR`，硬件默认 `y`，可在 `proj.conf` 显式关闭。
- `SConscript` 在开关关闭时从 `Glob('*.c')` 排除 `momo_memory_calendar.c`；`app_pet.c` 的 include、字段、函数、事件接入和 UI 全部由同一宏保护。
- 关闭时不编译/链接模块，不打开 prefs、不创建对象、不改变 Pet、Quest Garden、BLE、音频、GIF 或 IMU 行为。
- A/B 使用同一分支、同一工具链、同一构建参数，只切换该 Kconfig；比较 `arm-none-eabi-size` 的 text/data/bss。

## 8. 资源、性能与功耗预算

| 项目 | 设计值/门禁 |
|---|---|
| 持久化单槽 | 约 30 B；双槽仍远低于 512 B |
| 日历算法常驻 RAM | 约 48 B |
| Pet 指针/状态增量 | 预计 <200 B，不含面板打开时 LVGL 堆对象 |
| 常驻 RAM 总增量 | 必须 <1 KiB，以 A/B size 复核 |
| Flash 代码增量 | 必须 <10 KiB，以 A/B size 复核 |
| 线程/timer/素材 | 0/0/0 |
| 同日刷新 | 100 ms 路径仅 RTC/整数门禁；无 Flash 写、无全量重绘 |
| Flash 写入 | 每日每类首次最多 3 次，跨日最多 1 次；重复事件 0 次 |

读取 RTC/转换日期若实测影响功耗，可在现有 100 ms timer 上增加 1 秒软件分频；不新增定时器。面板关闭时没有动画或额外刷新。

## 9. 测试矩阵

### 9.1 纯 C 主机测试

- NULL、非法事件、非法索引和无效日期/月日/年份。
- 2020/2024 闰日有效，2021/2100（范围外）及 2021-02-29 无效。
- 月末、年末、闰年双向转换。
- 首日、同日三类任意顺序、重复 20/100 次幂等和每日写门禁。
- 前进 1 天、多日跳跃、中间空白、35 天/30 天滚动、差值 >=30。
- RTC 无效事件暂存、回拨不覆盖、恢复后合并、回拨期间重复幂等。
- 3-bit 跨字节边界（索引 2/5/10/29）及未用 bit 门禁。
- A/B 一槽损坏、双槽损坏、未知版本、错误长度、CRC 单字节损坏、事件高位、generation 选择与相等提示。
- 清空候选成功/失败语义；保存失败时旧上下文不变。

主机命令使用仓库脚本，优先 clang/gcc，回退 WSL `cc`，新增模块以 `-std=c11 -Wall -Wextra -Werror` 编译。

### 9.2 集成与回归

- 目标固件全量构建：`scons --board=sf32lb52-lchspi-ulp --board_search_path=../boards -j8`。
- 运行现有 Agent 协议、Quest Garden、日功德、音频等可用主机回归脚本。
- 开关 ON/OFF 两次完整构建并比较 ELF size；检查新增源警告、数组/指针边界、整数转换、RTOS 临界区和 LVGL 单线程。
- PC 模拟器若能按现有环境构建则执行；若因仓库基线工具链失败，记录真实命令和阻塞，不把目标固件成功冒充模拟器成功。

### 9.3 待硬件验收

- 入口与宠物短按、系统返回、Quest 领取及 IMU 无冲突。
- 断电发生在槽写入前/中/后均至少恢复一个完整版本。
- RTC 未同步后同步、手工回拨再恢复、跨日/跨月/跨年待机。
- 触摸和 IMU 只有现有有效木鱼动作记“玩耍”，噪声不记。
- 反复打开/关闭 Pet 与面板各 200 次，对象数量回到基线。
- 连续 24 小时观察写入次数、卡顿、功耗和 Agent/GIF/音频抢占。

在完成这些真机项目之前统一标记“待硬件验收”，不以主机或模拟器结果替代。

## 10. 回退与完成门禁

- 首选回退：关闭 `AGENT_PET_USING_MEMORY_CALENDAR` 后重新构建；双槽数据保留但不读取、不展示。
- 代码回退：回退开发提交即可，产品与方案文档可保留；不修改默认分支。
- 运行降级：RTC 无效、存储损坏、写入失败或 UI 分配失败只禁用保存/面板，不影响现有 Pet 功能。
- 完成必须同时具备：产品提交、本文档提交、开发提交、实际构建命令与退出码、回归结果、A/B 资源增量和明确硬件验收项。
- 不推送远端、不创建 PR、不合并。
