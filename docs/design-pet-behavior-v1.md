# Agent Pet 多状态自主行为实现方案 v1

## 架构

实现分为三个边界清晰的部分：

1. `pet_behavior.*`：纯 C、固定内存的行为模型，维护情绪、短时反应、长期属性、
   冷却时间和确定性随机数。该模块不依赖 LVGL、RT-Thread 或硬件。
2. `app_pet.c` 事件适配：触摸、IMU、时间和 Agent 状态转换为行为事件，并在现有
   100 ms LVGL timer 中推进模型。
3. `pet_state_assets.*`：编译期版本化素材映射，把有效视觉状态映射到
   `/pet/<state>.gif`；素材不存在时由视图层安全降级。

V1 不创建新的常驻 RTOS 线程。当前事件源都由 LVGL 回调或 LVGL timer 驱动，复用
现有节拍可以避免额外线程栈和跨线程 LVGL 风险。页面关闭期间不持续运行动画；重新
进入页面时以持久化的最后互动 epoch 计算 Sleepy/Lonely，保持生命周期连续性。

## 优先级

从高到低：

1. Agent Error / Needs input / Running / Completed。
2. Celebrate / Startled / Annoyed / Happy / Curious 短时反应。
3. Lonely / Sleepy / Happy / Curious / Calm 基础情绪。

Agent 状态只覆盖显示，不写入 affinity、energy 或 arousal。

## 状态稳定策略

- 基础情绪最短驻留 8 秒。
- 空闲自主决策间隔为 20~45 秒，由确定性 PRNG 产生。
- reaction 使用绝对 tick 截止时间，支持 32 位 tick 回绕。
- 点击窗口 1.5 秒；点击奖励冷却 10 秒；长按冷却 20 秒；动作冷却 3 秒。
- 亲密度达到 60 时近期互动倾向 Happy；唤醒度达到 60 时基础情绪倾向 Curious；
  精力降到 20 时进入 Sleepy。
- 无效事件和越界状态被拒绝，不允许污染模型。

## 持久化

通过独立 `share_prefs` 记录保存：

- schema version
- affinity、energy、arousal
- last interaction epoch
- PRNG state

只在长期值发生变化时标脏；页面存活期间最多每 5 分钟合并写一次，页面关闭时补写。
无存储或 RTC 未同步时使用默认值并继续运行。

## LVGL 生命周期

- 所有对象、动画、GIF decoder 创建和销毁只发生在 LVGL 线程。
- 状态变化时先验证目标 GIF 可打开，再释放当前 GIF。
- 目标状态素材缺失时保留基础自定义 GIF；基础 GIF 不可用时使用内置 mascot。
- GIF 播放期间不叠加高频位置动画，避免同一对象出现两个持续刷新源。

## 素材映射

素材映射版本为 1，路径如下：

| 状态 | 路径 |
|---|---|
| Calm | `/pet/calm.gif` |
| Happy | `/pet/happy.gif` |
| Curious | `/pet/curious.gif` |
| Sleepy | `/pet/sleepy.gif` |
| Lonely | `/pet/lonely.gif` |
| Celebrate | `/pet/celebrate.gif` |
| Startled | `/pet/startled.gif` |
| Annoyed | `/pet/annoyed.gif` |
| Working | `/pet/working.gif` |
| Needs input | `/pet/needs_input.gif` |
| Error | `/pet/error.gif` |

## 资源预算

- 行为模型目标小于 256 字节，无动态内存。
- 素材映射为只读常量，内部 SRAM 增量可忽略。
- UI 增加一个短标签和少量状态字段。
- 活动 GIF 继续使用 PSRAM：一个压缩源、一个 decoder canvas 和一个输出帧。
- 不同时预加载状态 GIF，避免 PSRAM 使用量随状态数量线性增长。

## 测试矩阵

- 主机单元测试：默认值、三击/六击、长按冷却、动作冷却、Agent 优先级、昼夜、
  24 小时未互动、持久化恢复、tick 回绕和随机可复现。
- PC 模拟器：触摸反馈、状态文字、颜色和动画，缺失状态素材的回退。
- 目标构建：编译告警为零，比较修改前后的 text/data/bss。
- 硬件：短按/长按、轻摇/撞击误触发、24 小时稳定性、1000 次状态切换、
  GIF 解码最坏耗时、PSRAM 基线恢复和功耗。

## 回退策略

- 编译失败：行为模块可从 SConscript 单独移除，不改变协议与存储格式。
- 运行期素材失败：回退 `/pet.img`，再回退内置 mascot。
- IMU 连续读取错误：沿用现有熔断，行为模型继续由触摸和时间驱动。
- 持久化失败：保留 RAM 中状态并记录错误，下次启动使用默认值。
