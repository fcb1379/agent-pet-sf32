# SF32 GPS 码表实施计划

日期：2026-09-17

分支：`codex/feature-bike-computer`

基线：`d2192dc Add persistent watch settings`

## 1. 开发原则

- 保留 SiFli-SDK、RT-Thread、LVGL v8 和当前 GUI app framework。
- 先完成模组电气接口确认和文档评审，再修改代码；无法读取的转接板网络保持可配置并单独门禁。
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

当前结论：DX-GP10 模组接口已确认；UART1 与调试口冲突，排除使用；UART2 的 SF32LB52 SDK 参考映射为 PA20(RX)/PA27(TX)，当前板级资源未占用。转接板原理图/网表尚未读取，故真实针脚与供电连接仍是设备验收门禁。

### 阶段 1：产品裁剪和数据模型

任务：

- 首次构建保留基线功能以量化资源增量，再决定是否移除 A2DP、WebRTC、本地音乐和非码表页面。
- 定义固定容量 `BIKE_RIDE_STATE`、GNSS 有效位、时间戳和错误码。
- 编写纯逻辑单元测试/主机测试，覆盖速度、里程、暂停和超时。

门禁：无硬件依赖的数据模型测试通过；没有动态内存。

### 阶段 2：GNSS 驱动打通

当前进度：UART2 固定缓冲输入、GGA/RMC/VTG 校验解析、GNSS 超时、RTC 校时和骑行数据提交已完成源码、主机测试与目标构建验证。VTG 只刷新航向/速度，不延长坐标定位有效期；真实模组连续输入和断线恢复留到最终实机阶段验证。

任务：

- 配置独立 UART2（默认 PA20/PA27）和后续可选 WAKE_UP/RESET/PPS。
- 复用 SiFli GNSS 框架或实现通用 NMEA 输入层。
- 解析 GGA/RMC/VTG；对校验、截断和超时做防护。
- 通过消息/快照更新数据模型，回调不操作 UI。

门禁：台架连续 30 分钟稳定；断开/重连、丢星和脏数据可恢复。

### 阶段 3：390 x 450 码表 UI

当前进度：主数据、定位详情、骑行总结三页已完成；主数据页提供开始/暂停与保存结束，总结页提供保存/丢弃，KEY2 可完成开始/暂停/继续和保存结束。`bikedemo on/off/status` 可在无 GPS 模组时每秒生成固定容量模拟定位，UART2 缺失时工作线程仍可启动，界面明确显示 `DEMO`。

任务：

- 新增 `bike` GUI app。
- 完成主数据、定位状态和骑行总结三页。
- 支持触摸和按键；模拟数据模式可在无 GPS 时演示。
- UI 刷新频率与 GNSS 更新解耦，避免每帧重建对象。

门禁：PC/设备布局检查通过；连续运行无明显堆增长和卡顿。

### 阶段 4：GPX 记录

当前进度：固定缓冲流式写入、空间余量、原子状态文件、异常恢复、恢复发布后再次掉电收敛、异常跳点断段、正常保存和未发布轨迹丢弃均已完成源码、主机测试与目标构建验证。板载文件系统复位/满盘行为留到最终实机阶段验证。

任务：

- 使用现有文件系统 API 流式写入 GPX，固定格式缓冲。
- 增加空间检查、原子状态文件、异常中断恢复和文件轮转。
- 限制无效点、异常跳点和最小移动距离。
- 桌面脚本验证 XML、点数、时间和距离。

门禁：正常结束和复位中断两类文件均可读取；文件系统满时安全失败。

### 阶段 5：BLE 骑行传感器

当前进度：广播识别、手动扫描、连接/配对、地址持久化、退避重连、手机链路隔离、项目自有 HR/CSC/Cycling Power/BAS 逐连接 GATT client、标准 Measurement 安全解析、传感器电量初始读取/通知、数据超时、轮速/踏频计算以及 CSC 优先/GNSS 回退的速度源仲裁已完成源码、主机测试与目标构建验证。功率计可与 HR/CSC 分别连接，也可在同地址组合设备上共享链路。真实设备连续骑行测试留在所有代码移植和检查完成后执行。

任务：

- 参考 SDK `multi_connection` 示例，使用项目自有逐连接 GATT client 接入 HR、CSC 和 Cycling Power。
- 增加扫描、配对、重连、数据超时和电池状态。
- 验证手机连接与传感器连接共存的连接数、RAM 和功耗。
- 按轮周和事件时间计算速度/踏频，处理 16/32 位回绕。

门禁：真实心率计和 CSC 传感器各完成一次连续骑行测试。

### 阶段 6：X-TRACK 离线地图与实时轨迹

当前进度：已按 X-TRACK 的 Web Mercator 和 `/MAP/<zoom>/<x>/<y>.bin` 组织方式重写固定容量地图层，完成主机投影/路径边界测试和 SF32 目标构建。LVGL 第四页已支持 3 x 3 瓦片、3~19 级缩放、当前位置和 128 点静态实时轨迹；瓦片缺失时保留定位/轨迹显示。实际 `.bin` 瓦片解码与页面视觉效果留到最终实机阶段。

任务：

- 使用固定容量进行 WGS84 Web Mercator 投影和瓦片路径生成。
- 在 LVGL 页面中显示离线瓦片、当前位置、缩放控件和实时轨迹。
- 地图文件缺失或损坏时安全降级，不影响码表主数据和 GPX 记录。

门禁：投影/路径主机测试与目标构建已通过；瓦片格式、缩放、轨迹叠加和缺图降级需实机验收。

### 阶段 7：存储与扩展功能

范围状态：X-TRACK 原工程使用 Micro SD 存放地图和轨迹；当前 SF32 工程仅启用 4 MiB 板载 Elm FAT，SPI1 TF 引脚有定义但未启用。计步所需 IMU 也尚未确认。

任务：

- 在确认 TF 卡座和挂载点后，把 `/MAP` 和 `/Track` 切换为可配置存储根目录。
- 确认板载 IMU 型号和总线后，再移植计步驱动与统计页。
- FIT 输出和路线导航不是 X-TRACK 主线必备功能，保留为后续独立扩展。

门禁：TF/IMU 必须先有硬件网表或型号证据；实机热插拔、异常断电和计步准确率放在所有代码检查之后。

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
