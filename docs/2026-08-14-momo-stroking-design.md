# “摸摸 Momo”连续抚摸互动实现方案

## 1. 文档门禁与实现边界

- 产品提交：`34be90107b63bbd3c9524844d0449c922274c105`。
- 开发基线：`origin/master@a2805abeb72f02a726dce5dc49857280ba9cdc78`。
- 固定 SDK：`sdk@90e575f7ec253b6a879d816460a16546ebd2582f`；递归蓝牙子模块：`b0a2cb714c383a407d08c43e7fbecf39f732185c`。
- 方案只实现目标项目原创定义的连续触摸模型，不复制候选仓库的代码、参数、素材、布局或音频。
- MVP 不新增素材、RTOS 线程、GIF decoder、动态轨迹数组、音频、震动、BLE 协议或手机端能力。
- 本文档先于任何代码修改提交；只有方案提交成功后才能进入实现。

## 2. 现状审计

### 2.1 Pet 页面生命周期与线程

`pet_app_main()` 只注册 GUI app 消息处理器。`GUI_APP_MSG_ONSTART` 在 GUI/LVGL 线程调用 `pet_on_start()`，一次性创建 `root`、`stage`、mascot、GIF、任务花园和 LVGL timers；`GUI_APP_MSG_ONSTOP` 在同一线程删除 timers、GIF decoder、图片缓存和 root，最后清零 `g_pet_ui`。

`PET_RefreshStatus()` 是现有 100 ms LVGL timer 回调，它在 GUI 线程读取 BLE 的只读快照，按顺序处理图传进度、上位机 `PLAY/RESTORE/TYPING`、Agent 聚合状态、任务花园和木鱼事件。新增视觉对象、动画、样式、显示/隐藏和删除全部留在这条线程，不从 BLE、文件、ISR 或工作线程调用 LVGL。

### 2.2 当前触摸真实路径

- 内置 mascot 在 `pet_on_start()` 中注册 `LV_EVENT_SHORT_CLICKED -> PET_PlayWoodenFish()`。
- 动态创建的自定义 GIF 对象在 `PET_LoadCustomGif()` 中注册同一回调。
- `PET_PlayWoodenFish()` 用 `lv_indev_get_act()`/`lv_indev_get_point()`取得屏幕坐标，随后直接进入木鱼视觉、短音效、功德计数和 BLE 通知。
- 当前默认分支没有 Pet 的 `PRESSED/PRESSING/RELEASED/LONG_PRESSED` 回调；也没有长按或连续轨迹识别。
- 图片/GIF 切换只在 `PET_RefreshExpressionAnimation()` 与 `PET_RefreshMascotImage()` 的 GUI 路径进行；已有 GIF 使用单 decoder 与双显示缓冲。

### 2.3 LVGL v8 事件证据与消费策略

锁定 SDK 的 `sdk/external/lvgl_v8/src/core/lv_indev.c` 表明：

1. 指针保持按下时，每次输入驱动采样都会在滚动、手势和长按处理之前向活动对象发送 `LV_EVENT_PRESSING`。
2. `lv_indev_get_point()` 返回当前 pointer 的 `act_point`，可在回调中可靠获取当前坐标；集成层再减去 `lv_obj_get_coords(stage)` 得到 stage 局部坐标。
3. 松手顺序固定为 `LV_EVENT_RELEASED`，然后在未滚动且未发长按时发送 `LV_EVENT_SHORT_CLICKED`，最后发送 `LV_EVENT_CLICKED`。
4. `lv_event_stop_processing()` 只停止当前事件余下的对象回调，不能取消未来事件。

因此采用以下方式，不手工重复触发短击：

- 抚摸回调先于现有木鱼 `SHORT_CLICKED` 回调注册。
- `CANDIDATE` 的每次 `PRESSING` 调用 `lv_indev_reset_long_press()`，把长按判定推迟到候选结束，保证抚摸候选优先。
- 候选失败时不调用 `PET_PlayWoodenFish()`；释放后由 LVGL 原生发送一次 `SHORT_CLICKED`，现有木鱼回调保持唯一执行者。
- 识别成功后，抚摸回调在 `SHORT_CLICKED`、`LONG_PRESSED` 和 `LONG_PRESSED_REPEAT` 上调用 `lv_event_stop_processing()`，抑制同一序列的其他语义。
- 编译开关关闭时不注册抚摸回调，原有注册顺序和事件路径完全不变。

## 3. 总体架构

```text
LVGL pointer events（GUI线程）
        |
        v
PET_StrokeInputEvent -- stage局部坐标 --> MOMOSTROKE_* 纯C状态机
        |                                   |
        | START / END                       | 固定内存摘要
        v                                   v
PET_Start/EndStrokeVisual          主机边界/回绕/压力测试
        |
        v
已有 stage + 1个光晕 + 1个标签（GUI线程）

PET_RefreshStatus（100 ms GUI timer）
        |
        +-- 最新 BLE/Agent/PLAY/typing/图传/木鱼状态
        +-- MOMOSTROKE_Poll（释放防抖、tick回绕）
        +-- 抢占抚摸并调用统一恢复出口
```

模块划分：

| 模块 | 职责 | 线程/内存 |
|---|---|---|
| `momo_stroking.h/.c` | 纯 C 坐标、距离、采样比例、状态转换和 tick 回绕 | 无 LVGL/RTOS/文件接口；调用者提供固定结构体 |
| `app_pet.c` | LVGL 事件适配、优先级快照、视觉对象、持久化开关和恢复 | 仅 GUI 线程 |
| `tests/momo_stroking_host_test.c` | 边界、误触、回绕、100/1000 次确定性序列 | 主机进程 |
| `tests/run_momo_stroking_host_test.ps1` | `-std=c11 -Wall -Wextra -Werror` 构建和运行 | clang/gcc/WSL 回退 |

`momo_stroking` 不保存 LVGL 指针，不调用动态分配，不创建 timer 或线程。页面对象仍由 `g_pet_ui` 统一拥有。

## 4. 模块接口与固定内存

### 4.1 公共类型

`MOMO_STROKE_CONFIG` 包含 stage 尺寸、头部热区边界、10 Hz 采样间隔、停留/轨迹/释放/离区门槛、单窗口最大位移和热区采样比例。产品数值只在目标项目中以具名常量初始化，候选项目参数不进入代码。

`MOMO_STROKE_CONTEXT` 只保存：

- `IDLE/CANDIDATE/STROKING/RELEASING` 状态；
- 起点和上一个采样点；
- 开始、上次采样、离开热区和释放 tick；
- 饱和累计曼哈顿距离；
- 总采样数、热区采样数；
- 当前序列是否已消费、是否正在热区外。

预计上下文小于 64 B，编译期 `_Static_assert(sizeof(MOMO_STROKE_CONTEXT) <= 256U)` 固定上限。没有坐标数组，也没有结构体内指针。

### 4.2 纯 C 接口

- `MOMOSTROKE_Init()`：清零上下文并进入 `IDLE`。
- `MOMOSTROKE_Press()`：只在 stage 内且起点位于头部热区时进入 `CANDIDATE`。
- `MOMOSTROKE_Sample()`：内部执行采样间隔门限，更新距离与热区比例；可能返回 `STARTED` 或因越界返回 `ENDED/CANCELLED`。
- `MOMOSTROKE_Release()`：候选失败直接回到 `IDLE`；已识别序列进入 `RELEASING`。
- `MOMOSTROKE_Poll()`：用无符号差值处理 tick 回绕，完成停留识别、离区超时和 150 ms 释放防抖。
- `MOMOSTROKE_Cancel()`：页面退出/高优先级抢占时结束状态，不访问 UI。
- `MOMOSTROKE_IsConsumed()`：供 `SHORT_CLICKED/LONG_PRESSED` 仲裁；消费标志到下一次 `Press` 才清除。

所有入口检查 NULL。坐标差先扩展到 32 位再取绝对值，累计距离饱和，避免整数溢出。tick 统一用 `(uint32_t)(now - then)`，不比较绝对大小。

## 5. 状态机

### 5.1 状态转换

| 当前状态 | 事件/条件 | 下一状态 | 输出 |
|---|---|---|---|
| IDLE | 热区内按下且运行时开关开启、视觉可用 | CANDIDATE | 无 |
| IDLE | 热区外按下或高优先级忙 | IDLE | 无，保留原交互 |
| CANDIDATE | 停留达到 650 ms、累计距离不大于 12 px、当前在热区 | STROKING | STARTED |
| CANDIDATE | 轨迹达到 350 ms、距离 12～160 px、单采样不大于 80 px、热区比例不低于 70% | STROKING | STARTED |
| CANDIDATE | 离开 stage、超速或距离越界 | IDLE | CANCELLED，不消费短击 |
| CANDIDATE | 释放未达门槛 | IDLE | 无；LVGL 原生短击可继续 |
| STROKING | 释放 | RELEASING | 无，保持视觉至防抖完成 |
| STROKING | 离开热区连续 250 ms、离开 stage或高优先级抢占 | IDLE | ENDED |
| RELEASING | 150 ms 到期 | IDLE | ENDED |
| 任意 | 页面停止/运行时开关关闭 | IDLE | 必要时 ENDED并清理 |

一次序列最多输出一个 STARTED 和一个 ENDED。进入 STROKING 后不重复累计奖励；MVP 不增加亲密度持久化。

### 5.2 采样

- 输入驱动可以高于 10 Hz 发送 `PRESSING`，状态机只在距上次采样至少 100 ms 时提交一次摘要。
- 小于采样周期的快速划过无法满足 350/650 ms 时间门槛。
- 每个有效样本计算与上一个有效样本的曼哈顿距离；大于 80 px 的窗口拒绝候选或结束当前反馈。
- STROKING 后不再用整个会话的 160 px 上限结束自然往返；继续检查单窗口速度和离开热区时长，避免长时间正常抚摸因累计距离必然溢出。

## 6. LVGL 集成和视觉生命周期

### 6.1 事件注册

在 `AGENT_PET_USING_STROKE` 开启时，对内置 mascot 和每次创建的自定义 GIF 注册同一 `PET_StrokeInputEvent`，监听 `PRESSED/PRESSING/RELEASED/PRESS_LOST/SHORT_CLICKED/LONG_PRESSED/LONG_PRESSED_REPEAT`。抚摸回调在原有木鱼回调之前注册；关闭时只保留原有 `SHORT_CLICKED` 注册。

不把 root 设为可滚动，也不扩大页面导航触摸范围。事件坐标必须落在 stage 矩形内；由屏幕坐标减 stage 当前坐标得到局部坐标，以适配已有上下浮动动画。

### 6.2 视觉对象

页面启动时固定创建并隐藏：

- 一个暖色半透明圆角光晕对象；
- 一个短标签对象。

对象挂在 Pet root 下，关闭 click/scroll 标志，不遮挡 pointer target。识别开始后取消 stage 的已有位置动画，显示光晕和标签，并只对 stage 做不超过 3 px 的低频缓动；不改变 mascot/GIF source、不创建第二 decoder、不修改图片槽位、不操作音频或行为模型。

页面停止时先取消状态机与动画，再由 root 级联删除对象，随后清零 `g_pet_ui`。不存在跨页面 LVGL 指针。

### 6.3 统一恢复出口

抚摸结束或被抢占时：

1. 删除仅针对 stage/光晕的抚摸动画并隐藏光晕/标签；
2. 将已有 `ucRenderedState` 置为无效哨兵；
3. 不设置 GIF 槽位、不恢复旧快照；
4. 由当前/下一次 `PET_RefreshStatus()`重新读取最新 `requested slot`、typing、Agent 和图传状态，再走既有 `PET_RefreshExpressionAnimation()`与 `PET_ApplyStateAnimation()`。

这保证恢复的是最新有效状态，不固定回 0 号槽位或抚摸前的陈旧状态。

## 7. 优先级与抢占

`PET_RefreshStatus()`保存本周期最新只读状态，并按下列条件拒绝开始或抢占已开始的抚摸：

1. 页面停止或运行时开关关闭；
2. 图片/GIF `AGENTPET_IMAGE_RECEIVING`；
3. typing active 或 `ucRequestedImageSlot != AGENTPET_IMAGE_BASE_SLOT`，表示上位机显式覆盖；
4. Agent 聚合状态为 RUNNING、NEEDS_INPUT、COMPLETED 或 ERROR；
5. 木鱼对象当前可见/木鱼 timer 活跃；
6. 任务花园的显式领取反馈或其他已有明确交互动画。

Agent IDLE 和无快照状态允许抚摸；任何非 IDLE Agent 状态优先。图传检查使用已有 BLE 状态快照，不访问文件系统。若图传在两个 100 ms 周期之间开始，最迟下一周期抢占；抚摸视觉不切 decoder、不读写图片文件，因此窗口内不会形成存储竞争。

未来合并多状态自主行为分支时，抚摸层只产生一次 STROKE 业务事件；匿名 0～4 GIF 槽位不获得隐含语义。行为层的 Agent/远端所有权判断必须仍高于本反馈。

## 8. 运行时开关与编译开关

- `Kconfig.proj`增加 `AGENT_PET_USING_STROKE`，默认开启；关闭后不注册回调、不创建对象/开关、不执行状态机。
- Pet 页面提供独立 `Stroke On/Off` switch，不与 Motion switch 复用。
- 硬件使用独立版本化 share_prefs 命名空间和 `enabled` 键，默认 1；切换时立即写入，失败只记录错误并保持本次 RAM 状态。
- PC 模拟器默认开启且不访问硬件持久化；可通过 UI 切换以验证关闭路径。
- 关闭运行时开关时立即取消候选/反馈并恢复最新视图；再次开启不补播旧触摸。

## 9. 错误处理与回退

| 错误 | 行为 |
|---|---|
| 无活动 pointer/坐标无效 | 取消候选，不消费现有短击 |
| 光晕或标签创建失败 | 禁用本页抚摸视觉，原有点击继续工作 |
| prefs 打开/读取/写入失败 | 默认开启或保留 RAM 值，打印有限错误，不阻塞 GUI |
| tick 回绕 | 无符号差值继续计时 |
| 图传/Agent/PLAY/typing/木鱼中途到来 | 结束反馈并走统一最新状态恢复 |
| 页面中途退出 | 同步取消状态和动画，由 root 统一释放 |
| 功能编译关闭 | 完全走基线短击/木鱼路径 |

不在输入回调进行文件、BLE、音频或阻塞操作。状态机无需锁，因为所有调用来自 GUI 线程；BLE 状态仍通过现有快照接口跨线程传递。

## 10. 资源和性能预算

- 状态机静态上下文：目标小于 64 B，硬门限 256 B。
- Pet UI 新增指针/标志：预计不超过 32 B。
- LVGL 页面存活新增 2 个对象，经验预算小于 2 KiB LVGL heap；必须真机/模拟器观测，无 API 时标注未直接测量。
- Flash 增量目标小于 6 KiB；素材增量严格为 0。
- 不新增线程、线程栈、RTOS queue、decoder、canvas 或 PSRAM 图片缓存。
- 轨迹提交不高于 10 Hz；现有 100 ms status timer承担释放/抢占轮询，不新增 timer。
- 视觉只产生 stage/光晕的低频无素材动画，GIF 模式不增加解码帧率。

资源 A/B 使用相同代码和工具链分别关闭/开启 `AGENT_PET_USING_STROKE`完整构建，记录 ELF `text/data/bss`；若配置系统的自动生成文件需要改变，只在构建目录操作，不提交生成物。

## 11. 测试矩阵

### 11.1 纯 C 主机测试

以 `-std=c11 -Wall -Wextra -Werror` 编译：

- 650 ms 停留和 350 ms 温和轨迹独立成功；开始反馈延迟边界；
- 649/650 ms、349/350 ms、11/12/160/161 px、69/70%和 80/81 px 边界；
- 起点在热区边界内外、stage 外、离开 stage、离区 249/250 ms；
- 短击、快速划过、超速、候选释放不消费；
- 一次序列恰好一对 STARTED/ENDED；释放 149/150 ms；
- `UINT32_MAX`附近开始并跨零完成停留、释放和离区计时；
- 连续 100 次有效轨迹识别计数；连续 100 次负向序列误触计数；
- 连续 1,000 次开始/释放/取消，状态最终为 IDLE且无计数漂移。

### 11.2 PC 模拟器

- 完整构建 `work/watch_bt_audio_template/simulator/build.bat`；
- 用鼠标确定性验证停留、慢移、短击、快速滑动、运行时开关；
- 切换内置图、JPEG、GIF、远端 PLAY/RESTORE、typing、Agent 四态和图传模拟，确认优先级及最新状态恢复；
- 快速进出 Pet 页面，确认无悬空对象/timer/动画。

PC 鼠标事件不能替代硬件触摸采样精度与误触验收。

### 11.3 目标构建与回归

- `./tests/run_momo_stroking_host_test.ps1`；
- 现有 Agent protocol、audio protocol、quest garden 及仓库内相关主机回归；
- PC 完整构建；
- `./build.ps1` 完整目标构建，记录命令、exit code、告警和 ELF size；
- 编译开关 A/B 完整构建，记录相同 ELF 的 `text/data/bss` 差值；
- `git diff --check`、新增代码的数组/指针边界、整数溢出、LVGL 生命周期、RTOS共享状态和禁止函数人工复核。

### 11.4 真机验收（不得以模拟器冒充）

- 100 次有效抚摸，成功不少于 95；记录手法和原始计数。
- 100 次普通短击/快速划过，误触不超过 2；确认木鱼不重复触发。
- 连续 1,000 次开始/结束与页面快速进出，无崩溃、持续内存增长、悬空动画。
- 真机移动坐标质量、150 ms内视觉反馈、150～300 ms释放恢复。
- 图传开始/结束、远端 PLAY/typing、Agent 四态、任务完成、木鱼和页面退出抢占。
- 运行时开关重启持久化；编译关闭固件的基线短击一致性。

未连接硬件时，上述项目统一标记“待硬件验收”，不宣称通过。

## 12. 交付与回退

交付顺序固定为：产品提交 → 本实现方案提交 → 开发/测试提交。开发提交必须附测试记录、实际命令、exit code、资源差异和真机待验收项。

回退优先使用运行时开关；若需要固件级回退，关闭 `AGENT_PET_USING_STROKE`即可恢复原始事件注册和 UI。不会迁移或改写现有 GIF、Agent、任务花园和木鱼持久化数据。
