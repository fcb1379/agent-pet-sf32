# SF32 GPS 码表实施计划

日期：2026-09-17

分支：`codex/feature-bike-computer`

基线：`d2192dc Add persistent watch settings`

## 1. 开发原则

- 保留 SiFli-SDK、RT-Thread、LVGL v8 和当前 GUI app framework。
- 先完成硬件接口确认和文档评审，再修改代码。
- 每一阶段单独提交、单独构建并记录资源变化。
- 不修改 SDK 源码；板级差异放在项目内 board overlay。
- 不推送、不烧录、不创建 PR，除非用户明确要求。

## 2. 计划模块

建议在 `work/watch_bt_audio_template/src` 下新增以下边界：

```text
app_utils/
  bike_gnss_port.c/.h       RT-Thread UART、模组上电和原始帧输入
  bike_nmea.c/.h            固定缓冲 NMEA 解析/SDK GNSS 适配
  bike_ride_model.c/.h      骑行状态、统计、有效性和线程安全快照
  bike_ride_recorder.c/.h   GPX 会话、文件轮转和恢复
  bike_sensor_ble.c/.h      第二阶段 HRPC/CSCPC 聚合
gui_apps/
  bike/                     LVGL 码表应用和页面
```

名称在实现前还需与现有 `SConscript` 和 app 注册方式核对。

## 3. 阶段与门禁

### 阶段 0：硬件接口确认

输出：

- GPS 转接板引脚表。
- 模组上电/复位/串口时序。
- 黄山派引脚复用和冲突检查。
- 电流预算和天线注意事项。

门禁：原理图/网表已读，UART 不与调试口冲突，IO 电平安全。

### 阶段 1：产品裁剪和数据模型

任务：

- 确定是否移除 A2DP、WebRTC、本地音乐和非码表页面。
- 定义固定容量 `BIKE_RIDE_STATE`、GNSS 有效位、时间戳和错误码。
- 编写纯逻辑单元测试/主机测试，覆盖速度、里程、暂停和超时。

门禁：无硬件依赖的数据模型测试通过；没有动态内存。

### 阶段 2：GNSS 驱动打通

任务：

- 配置独立 UART 和可选 EN/RESET/PPS。
- 复用 SiFli GNSS 框架或实现通用 NMEA 输入层。
- 解析 GGA/RMC/VTG；对校验、截断和超时做防护。
- 通过消息/快照更新数据模型，回调不操作 UI。

门禁：台架连续 30 分钟稳定；断开/重连、丢星和脏数据可恢复。

### 阶段 3：390 x 450 码表 UI

任务：

- 新增 `bike` GUI app。
- 完成主数据、定位状态和骑行总结三页。
- 支持触摸和按键；模拟数据模式可在无 GPS 时演示。
- UI 刷新频率与 GNSS 更新解耦，避免每帧重建对象。

门禁：PC/设备布局检查通过；连续运行无明显堆增长和卡顿。

### 阶段 4：GPX 记录

任务：

- 使用现有文件系统 API 流式写入 GPX，固定格式缓冲。
- 增加空间检查、原子状态文件、异常中断恢复和文件轮转。
- 限制无效点、异常跳点和最小移动距离。
- 桌面脚本验证 XML、点数、时间和距离。

门禁：正常结束和复位中断两类文件均可读取；文件系统满时安全失败。

### 阶段 5：BLE 骑行传感器

任务：

- 从 SDK `multi_connection` 示例接入 HRPC 和 CSCPC。
- 增加扫描、配对、重连、数据超时和电池状态。
- 验证手机连接与传感器连接共存的连接数、RAM 和功耗。
- 按轮周和事件时间计算速度/踏频，处理 16/32 位回绕。

门禁：真实心率计和 CSC 传感器各完成一次连续骑行测试。

### 阶段 6：FIT、TF 卡和导航评估

任务：

- 从 OpenTrailPaper 的 Apache-2.0 `fit_writer` 设计重写最小 FIT 输出。
- 若硬件支持，启用 SPI1 TF 卡并做热插拔/异常断电测试。
- 评估 GPX 路线、转向提示和轻量地图；不直接搬 H3/e-paper 渲染栈。

门禁：由用户单独确认范围后启动。

## 4. 预计提交拆分

1. `docs: evaluate open-source bike computer ports`
2. `feat(bike): add fixed-capacity ride state model`
3. `feat(bike): add GNSS UART and NMEA pipeline`
4. `feat(bike): add LVGL ride dashboard`
5. `feat(bike): add recoverable GPX recorder`
6. `feat(bike): add BLE heart-rate and CSC clients`

每次提交前检查工作区、暂存差异、`git diff --cached --check` 和构建结果，避免夹带其他功能。

## 5. 构建与验证

构建目录：`work/watch_bt_audio_template/project`

目标命令：

```text
scons --board=sf32lb52-lchspi-ulp --board_search_path=../boards -j8
```

新 worktree 不复制或修改当前主工作区中未提交的 `work/sifli-sdk`。构建时应显式指向已验证的 SiFli-SDK v2.4.0 路径，或在后续单独提交受控的 submodule 配置。

每阶段报告：

- 源码/配置支持状态。
- 构建命令、结果和资源占用。
- 是否烧录及镜像校验结果。
- 串口/设备运行证据。
- 仍待完成的道路、功耗和机械验收。
