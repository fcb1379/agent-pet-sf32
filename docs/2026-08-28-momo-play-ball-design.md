# Momo 玩球（触屏抛接球 MVP）实现方案

## 1. 方案状态与基线纠正

| 字段 | 内容 |
| --- | --- |
| 方案版本 | v1.0 |
| 目标分支 | `codex/2026-08-28-momo-play-ball` |
| 产品提交 | `5ce9b245213b77c2b2a2fff1860368edd9f2b657` |
| 固件基线 | `origin/master@a2805abeb72f02a726dce5dc49857280ba9cdc78` |
| 目标板 | `sf32lb52-lchspi-ulp` |
| GUI | LVGL v8，由 GUI app 生命周期和 LVGL timer 驱动 |

PRD 将目标内容区写为 240 x 240，但当前仓库的真实目标板配置不是 240 x 240：

- `work/watch_bt_audio_template/boards/sf32lb52-lchspi-ulp/hcpu/board.conf` 选择 `LCD_USING_TFT_CO5300`。
- SDK `Kconfig_lcd` 为 CO5300 定义 `LCD_HOR_RES_MAX=390`、`LCD_VER_RES_MAX=450`。
- 当前目标构建生成的 `rtconfig.h` 也实际包含 `390 x 450`。

因此实现不得把 240 写成运行时边界。所有布局和运动边界使用 `LV_HOR_RES_MAX`、`LV_VER_RES_MAX` 推导，390 x 450 是本分支的真实构建及真机验收基线。240 x 240 仅保留为未来其它屏幕配置的兼容性检查项，不能代替当前硬件验收。

## 2. 现有架构与约束

`app_pet.c` 在 `pet_on_start()` 中创建根对象、Momo stage、状态覆盖层和多个 timer，在 `pet_on_stop()` 中先删除 timer、关闭存储、释放自定义 GIF/JPEG，最后删除根对象。BLE/协议线程只发布固定内存快照；`PET_RefreshStatus()` 在 LVGL 线程读取快照并更新对象。

本功能沿用这些边界：

- 不新建 RT-Thread 线程、队列、信号量、协议字段或持久化记录。
- 纯 C 状态机不持有 LVGL 指针，不调用 RTOS/LVGL API，不动态分配内存。
- 球对象、反馈标签和 timer 只由宠物页 GUI/LVGL 线程创建、更新和删除。
- 使用一个 50 ms LVGL timer，目标更新率 20 Hz；没有游戏时 timer 暂停，避免无效唤醒。
- 球使用 LVGL 基础圆形样式绘制，零外部图片、GIF、字体或声音素材。

## 3. 模块与接口

### 3.1 纯状态机模块

新增：

- `src/gui_apps/pet/momo_play_ball.h`
- `src/gui_apps/pet/momo_play_ball.c`

模块公开有界数据类型和以下接口：

```c
void MOMOPLAYBALL_Init(MOMO_PLAY_BALL *pGame,
                       const MOMO_PLAY_BALL_BOUNDS *pBounds);
bool MOMOPLAYBALL_Press(MOMO_PLAY_BALL *pGame,
                        int16_t sTouchX, int16_t sTouchY,
                        uint32_t ulTickMs);
bool MOMOPLAYBALL_Drag(MOMO_PLAY_BALL *pGame,
                       int16_t sTouchX, int16_t sTouchY,
                       uint32_t ulTickMs);
bool MOMOPLAYBALL_Release(MOMO_PLAY_BALL *pGame,
                          int16_t sTouchX, int16_t sTouchY,
                          uint32_t ulTickMs);
void MOMOPLAYBALL_Cancel(MOMO_PLAY_BALL *pGame);
MOMO_PLAY_BALL_EVENT MOMOPLAYBALL_Update(MOMO_PLAY_BALL *pGame,
                                         uint16_t usDeltaMs);
```

`MOMO_PLAY_BALL` 只保存状态、球/Momo 中心、Q8 定点速度、最近一次触点、速度样本 tick、轮次/反馈累计时间与布尔锁。结构体由 `_Static_assert` 限制在 256 B 内。

### 3.2 LVGL 适配层

适配层位于现有 `app_pet.c`，在功能开关下向 `pet_ui_t` 增加：

- 球对象一个；用透明 padding 扩大命中区，不创建全屏透明层。
- 反馈标签一个，创建后复用。
- 游戏 timer 一个，50 ms 周期，空闲时暂停。
- 一份 `MOMO_PLAY_BALL` 静态业务状态。

新增对象数为 2，新增 timer 数为 1。球只分别注册 `LV_EVENT_PRESSED`、`LV_EVENT_PRESSING`、`LV_EVENT_RELEASED` 和 `LV_EVENT_PRESS_LOST`，不注册 `LV_EVENT_ALL`；回调在读取输入设备前再次过滤事件码，避免位置/样式变化等非输入事件在无 indev 时误取消或嵌套。坐标从当前活动输入设备读取；合法输入事件读不到输入设备时取消本轮。

## 4. 坐标、边界和手势

### 4.1 自适应内容区

运行时边界由屏幕宏推导：

- 左右保留 24 px 边缘手势带。球半径 13 px、扩展点击区 11 px，因此球心边界为 `24 + 13 + 11` 到 `LV_HOR_RES_MAX - 24 - 13 - 11`；视觉边缘离屏幕 35 px，扩展命中边缘仍严格离屏幕 24 px，左右对称。
- 顶部避开标题和系统状态区域，安全上边界取 48 px。
- 底部避开状态/任务文字，安全下边界取 `LV_VER_RES_MAX - 84`。
- 若小屏配置无法容纳上述固定留白，则在初始化时验证边界；边界宽/高不足以容纳球与 Momo 时功能创建失败并保持关闭，不进行无符号回绕计算。

球中心坐标始终钳制到安全区。球半径和 Momo 碰撞半径在本项目独立取值，不使用来源项目参数。

### 4.2 边缘返回手势

球初始位置和活动区按视觉半径及 11 px 扩展点击区共同排除左右 24 px。球对象只在自身命中区接收触摸，不添加覆盖根对象的大面积点击层，也不向输入设备调用 `lv_indev_wait_release()`，因此根页面/框架仍可识别边缘返回手势。

如果拖动中触点进入边缘保留区，状态机只钳制球的位置，不扩大球命中范围。真机必须验证左右边缘返回、快速拖动到四边及拖动中退出；如果框架手势仍被球事件抢占，回退为更大保留区或关闭 Kconfig。

## 5. 状态机与物理

### 5.1 状态

`DISABLED/IDLE -> DRAGGING -> MOVING -> FEEDBACK -> IDLE`。

- `DISABLED`：边界无效或页面正在销毁；拒绝输入。
- `IDLE`：球停在安全位置，timer 暂停。
- `DRAGGING`：球跟随触点，保存最近一个有效采样以估算释放速度。
- `MOVING`：球运动、衰减和反弹；Momo 向球追逐。
- `FEEDBACK`：球停下，反馈只触发一次；到期复位。
- 任意活动状态收到高优先级状态或页面退出时执行 `Cancel`，清零速度并回到安全空闲布局。

### 5.2 整数/定点物理

- 位置使用有符号 16 位像素中心坐标；速度使用有符号 32 位 Q8 像素/秒。
- 释放速度使用最后一个 8–200 ms 的非零拖动样本，时间差通过无符号减法处理 32 位 tick 回绕。间隔达到 8 ms 且零位移，或间隔超过 200 ms，会立即清除旧速度；release 再按样本 tick 复核新鲜度。release 紧随最近有效 `PRESSING` 且间隔小于 8 ms 时保留该最新样本。
- timer 回调以 `rt_tick_get_millisecond()` 的无符号 tick 差作为实际 `delta`，零值提升为 1 ms，收窄到 `uint16_t` 前饱和；状态机再钳制到 1–100 ms。乘法先提升到 64 位，再除以 `1000*256`，避免溢出与 GUI 调度抖动造成物理时间漂移。
- 速度绝对值限制为 520 px/s；每 50 ms 乘以 235/256 做确定性衰减。
- 越界时先把位置钳回边界，再将对应速度反向并乘以 3/4；每轴每 tick 最多处理一次反弹，不使用循环追边。
- 两轴均低于停止阈值或运动达到 5 s 时进入有限复位。
- Momo 每 tick 沿两轴分别靠近球，步长由 `delta` 和上限速度计算；坐标钳制到自身安全边界。
- 碰撞采用中心坐标轴对齐包围盒近似，所有差值转成 32 位后取绝对值。进入反馈后设置一次性锁，不重复触发。

### 5.3 tick 回绕与异常输入

`uint32_t` tick 只做 `(uint32_t)(now - previous)` 差值；从不比较绝对 tick 大小。拖拽采样和 LVGL timer 分别保存自己的上一 tick，Cancel 时同步清零。异常大触点在进入计算前钳制到边界，重复 release、空指针、无效状态和零/过大 delta 均返回安全结果。

## 6. Momo 展示与动画仲裁

追逐通过移动现有 `stage` 的有限偏移表达，不改变当前 GIF/JPEG 源，不创建新的 Momo 对象。状态机保存并输出逻辑 Momo 中心，适配层把中心转换为 stage 的左上角，且只在玩球处于活动状态时写入。

仲裁优先级从高到低：

1. 页面退出/对象销毁。
2. 图片正在接收、外部动画 `PLAY/RESTORE` 代际变化、打字开始/停止。
3. Agent `NEEDS_INPUT`、`ERROR`、`RUNNING`、`COMPLETED` 等非空闲关键状态。
4. Momo 玩球。
5. 原有空闲动画。

`PET_RefreshStatus()` 在刷新图片、外部动画和 Agent 聚合状态时计算“是否允许玩球”。如果不允许，则同步取消游戏、暂停游戏 timer、隐藏球/反馈、把球恢复为橙色并把 stage 恢复为 `PET_MASCOT_X/Y`，随后继续原有动画流程，避免反馈期外部取消后绿色样式泄漏。玩球不修改 Agent 快照、图片槽、GIF timer 或表达事件代际。

仅在以下条件同时满足时显示和响应球：页面存活、图片非接收态、没有 typing、请求基础图片槽、Agent 聚合状态为 `IDLE`。首次从高优先级状态恢复为空闲时只恢复球到初始位置，不恢复旧轨迹。

玩球活动时暂停 stage 的既有 LVGL animation；反馈/取消后将 `ucRenderedState` 置为无效值，让原逻辑重新应用当前 Agent 动画。自定义 GIF 仍由自己的 timer 解码，游戏只移动其共同父 stage。

## 7. LVGL 与页面生命周期

### 7.1 创建

`pet_on_start()` 完成 stage 和现有覆盖层创建后：

1. 验证 `LV_HOR_RES_MAX/LV_VER_RES_MAX` 推导边界。
2. 初始化纯状态机。
3. 创建球和复用反馈标签；任一步失败即删除已创建对象并保持功能不可用。
4. 注册球事件。
5. 创建唯一游戏 timer；创建失败时隐藏球并禁用状态机。
6. timer 初始暂停，仅 `MOVING/FEEDBACK` 时恢复。

所有操作都发生在 GUI app 的 LVGL 启动回调中。

### 7.2 运行

- LVGL 事件回调只更新纯状态机并同步球位置，不阻塞、不分配内存、不访问 BLE。
- 50 ms timer 是调度目标；每次回调按实际无符号 tick 差调用纯状态机 `Update`，再更新球、stage 和反馈样式。
- timer 参数不保存页面外部指针；回调先检查页面根对象、球对象和功能启用标志。
- `PET_RefreshStatus()` 和游戏 timer 同属 LVGL 线程，无需新增锁。BLE 快照仍由现有临界区/服务接口负责同步。

### 7.3 销毁

`pet_on_stop()` 的首要顺序：

1. 设置游戏不可用并调用 `Cancel`。
2. 删除游戏 timer 并立即置 `NULL`。
3. 清除球/反馈指针；对象由根对象统一删除，不单独重复删除。
4. 再执行原有 motion/status/wooden/daily/GIF/根对象清理。
5. 最后清零 `g_pet_ui`。

这样不会在根对象删除后留下 timer 回调或悬空对象引用。

## 8. 功能开关与回退

在 `project/Kconfig.proj` 增加 `AGENT_PET_MOMO_PLAY_BALL`，Kconfig 默认 `n`。代码和新增源文件由该配置条件编译；关闭时：

- 不创建球、反馈或 timer。
- 不注册玩球触摸事件。
- 不改变 `pet_ui_t` 的运行时初始化和现有宠物页行为。
- 不增加协议、存储或外部素材依赖。

产品工程的 `proj.conf` 可显式设为 `y` 供本分支真机验证。若帧率、手势、布局或功耗门禁不通过，删除该显式配置即可回退，不涉及用户数据迁移。

## 9. 资源、性能和功耗预算

| 项目 | 设计值/门禁 |
| --- | --- |
| 纯业务结构体 | `< 256 B`，编译期断言 |
| 新增 LVGL 对象 | 2 个：球、反馈标签 |
| 新增 timer | 1 个，50 ms，空闲暂停 |
| 新增线程/队列/锁 | 0 |
| 外部素材 | 0 |
| 持久化/协议 | 0 |
| Flash 增量 | `< 8 KiB`，同工具链 OFF/ON 实测 |
| RAM 增量 | 记录 `.data/.bss` A/B；业务状态 `<256 B` |
| 刷新目标 | 活动时 20 Hz；空闲时零游戏 tick |

功耗影响集中在用户主动玩球的数秒内：活动时新增 20 Hz 局部对象位置更新，停止/中止/页面退出后 timer 暂停或删除。无新外设、无线事务和后台线程。真机应测量连续玩球和空闲两种电流；未测量前不声明功耗达标。

## 10. 错误处理

- 空指针、无效边界、创建失败：禁用功能，保持原宠物页可用。
- 无活动输入设备、`PRESS_LOST`、异常坐标：取消本轮并复位。
- 重复事件或非法状态：不改变现有安全状态。
- timer 回调发现页面/对象无效：暂停 timer，不再访问对象。
- 高优先级状态：同步中止，不恢复旧轨迹。
- 状态机默认分支：清零速度和计时，回到 `IDLE`。

## 11. 验证矩阵

### 11.1 纯 C 主机测试

- 初始化/无效边界/空指针。
- 球外按压、按压、拖动、释放、零位移和重复 release。
- 旧速度样本在静止 8 ms 或超过 200 ms 后失效，以及有效样本后小于 8 ms 松手继续生效。
- 四边精确反弹、速度上限、衰减停止和精确 5 s 超时。
- Momo 追逐、`CAUGHT` 仅一次、700 ms 反馈结束精确复位。
- tick 在 `UINT32_MAX` 附近回绕、delta 过大、异常坐标。
- Cancel/重新开始、连续至少 200 轮有界伪随机输入。
- `-std=c11 -Wall -Wextra -Werror -pedantic` 编译并运行。

### 11.2 固件构建与静态检查

- 同一 GCC/SCons 工具链分别构建 Kconfig OFF/ON。
- ON/OFF 都进行目标全量链接，比较相同 ELF 的 `text/data/bss`。
- 检查新增 warning 为零，检查数组/指针、整数边界、对象与 timer 生命周期。
- 搜索确认无新线程、协议、存储和外部素材。
- 尝试 PC simulator；环境或基线阻断必须记录真实错误。

### 11.3 真机（待硬件验收）

- 当前实际 390 x 450 CO5300：触点坐标、四边/四角、左右边缘返回手势。
- 有效/无效拖动、快速反向、轻点、拖动中返回、运动中退出、反馈中退出。
- Agent `RUNNING/NEEDS_INPUT/ERROR`、外部 GIF/JPEG `PLAY/RESTORE`、typing、图片接收中止。
- 运动/追逐 >=20 FPS，无残影、撕裂或明显跳点。
- 反复进出页面和连续 200 轮，无崩溃、退出后回调或持续内存下降。
- 若未来提供真实 240 x 240 配置，额外验证边界初始化不会失败、标签无遮挡、手势带可用；它不是本目标板的替代验收。

## 12. 完成门禁

只有同时满足以下条件才可标记固件开发完成：

1. 本方案先于代码独立提交。
2. 主机状态机测试严格警告通过。
3. 目标板 Kconfig OFF/ON 均完整构建成功，并记录实际命令和资源增量。
4. 测试报告记录 PC simulator 真实结果与所有未完成真机项。
5. 代码提交不包含来源项目代码、素材、参数标识或协议变更。
6. 工作区干净，PRD、方案、开发与测试提交哈希完整可追溯。
