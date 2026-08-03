# PC 模拟器

在 PC 上直接运行桌宠 UI，无需硬件。改代码 → 编译 → 双击运行即可看到效果。

## 1. 编译和运行

```bash
# 编译（在 simulator/ 目录下执行）
cd simulator
build.bat

# 运行（双击即可）
simulator\build\bf0_ap.exe
```

增量编译只需几秒，全量编译约 1-2 分钟。窗口标题 "LVGL Simulator for Windows Desktop"。

## 2. 常见操作

### 看不同状态的效果

打开 `sim_src\mock_ble.c`，找到这一段：

```c
s_status.tSnapshot.ucAggregateState = AGENTPET_STATE_RUNNING;  // 改这里
```

把 `RUNNING` 换成以下任意一个，重新编译运行：

| 值 | 窗口效果 |
|---|---|
| `AGENTPET_STATE_IDLE` | 灰字 "Idle"，宠物缓慢浮动 |
| `AGENTPET_STATE_RUNNING` | 蓝字 "Running"，宠物上下跳动 |
| `AGENTPET_STATE_NEEDS_INPUT` | 黄字 "Needs input"，宠物左右摇摆 |
| `AGENTPET_STATE_COMPLETED` | 绿字 "Completed"，宠物弹跳一次 |
| `AGENTPET_STATE_ERROR` | 红字 "Error"，宠物快速抖动 |

### 换一张宠物图片

把 `sim_src\resource\images\mascot\mascot_small.png` 替换成自己的图片（建议 100×100 以内），重新编译。

### 调整颜色、位置、动画

编辑 `src\gui_apps\pet\app_pet.c`，找到 `pet_on_start()` 函数里的这一段：

```c
#ifdef BSP_USING_PC_SIMULATOR
    {
        lv_obj_t *mascot = lv_img_create(g_pet_ui.stage);
        lv_img_set_src(mascot, &mascot_small);
        lv_obj_set_pos(mascot, 8, 8);       // 图片在舞台中的位置
    }
```

这段代码的上方和下方分别是背景、标题、尾巴、火花粒子和状态文字，都可以直接改。

**常用改法：**

| 代码位置 | 改什么 | 示例 |
|---|---|---|
| `lv_color_hex(0x10232b)` | 背景色 | 换成 `0x000000`（纯黑）|
| `lv_obj_set_pos(g_pet_ui.stage, 62, 48)` | 宠物在窗口中的位置 | 增大 x 值右移 |
| `pet_start_y_animation(..., 800, 120)` | 浮动动画的速度和幅度 | 减小时长 = 更快 |
| `pet_shape(..., 0x58d5c3, 10)` | 尾巴颜色 | 换成 `0xff6b7a`（红色）|

## 3. 文件说明

```
simulator/          ← 编译这套东西
sim_src/
  main.c            ← 程序入口
  mock_ble.c        ← 假数据（改状态改这里）
  resource/images/
    mascot_small.png  ← 宠物图片
src/gui_apps/pet/
  app_pet.c         ← UI 代码（改外观改这里）
```

`sim_src/` 和 `app_pet.c` 中的 `#ifdef BSP_USING_PC_SIMULATOR` 只在模拟器编译时生效，**不影响正式硬件固件**。

## 4. 换机器后的配置

**SiFli-SDK**：`git clone --recurse-submodules` 自动拉取到仓库 `sdk/`，不需要额外配置。

**SiFli-ENV 工具包**：从 SiFli 官网下载，参考第2点：https://docs.sifli.com/projects/solution/1.get-started/development_env.html，在本地解压后修改 `simulator\build.bat` 顶部：

```batch
if not defined SIFLI_ENV  set SIFLI_ENV=你的env路径
```

**Visual Studio + Windows SDK**：修改 `simulator\msvc_setup.bat` 的 3 行：

```batch
subst x: "你的VS安装目录\VC\Tools\MSVC\版本号"
subst y: "你的Windows Kits目录\10\Include\版本号"
subst l: "你的Windows Kits目录\10\Lib\版本号"
```

如果不清楚本机路径，打开 **Visual Studio Installer → 单个组件**，搜索 "MSVC" 和 "Windows SDK" 查看已安装的版本号。

## 5. 常见问题

**编译报错 `无法打开包括文件`**

IDE 的 IntelliSense 误报，不影响 scons 编译，忽略即可。

**运行立即闪退**

用命令行启动看退出码。若为 `0xC0000094`，说明 `sim_src\main.c` 中 `lv_ex_data_pool_init()` 缺失或未在 `littlevgl2rtt_init()` 之后调用。

**图片没显示**

确认 `mascot_small.png` 在 `sim_src\resource\images\mascot\` 下，且文件名不含中文。图片标识符由 ezip 自动生成：`mascot_small.png` → `LV_IMG_DECLARE(mascot_small)`。
