# PC 模拟器

模拟器直接编译硬件工程的桌宠 UI 源码，不再维护一份容易过期的 UI 副本。

## 一键预览硬件 UI

双击 `preview_simulator.bat`。脚本会：

1. 从 `src/gui_apps/pet/` 编译最新硬件 UI；
2. 编译 PC 模拟器；
3. 启动 `simulator/build/bf0_ap.exe`。

修改该目录中已有的 `.c` 文件，或者新增 `.c` 文件后，再次双击脚本即可
看到最新效果，不需要手工复制代码。

## 同步边界

| 目录 | 用途 | 同步方式 |
|---|---|---|
| `src/gui_apps/pet/` | 硬件与模拟器共用的 LVGL UI | 模拟器直接编译，自动同步 |
| `sim_src/main.c` | PC 程序入口 | 仅模拟器 |
| `sim_src/mock_ble.c` | PC 端测试状态 | 仅模拟器 |
| `src/app_utils/` | 硬件 BLE、音频、设置等实现 | 不复制；模拟器使用 mock |

硬件和模拟器现在共用同一套宠物绘制、布局、颜色和动画代码。只有 GUI
框架注册和 BLE 数据来源仍通过 `BSP_USING_PC_SIMULATOR` 保留平台适配。

## 手工编译

```bat
cd simulator
build.bat
build\bf0_ap.exe
```

增量编译会自动检测 `src/gui_apps/pet/` 的修改。

## 切换模拟状态

编辑 `sim_src/mock_ble.c`：

```c
s_status.tSnapshot.ucAggregateState = AGENTPET_STATE_RUNNING;
```

可选值包括：

- `AGENTPET_STATE_IDLE`
- `AGENTPET_STATE_RUNNING`
- `AGENTPET_STATE_NEEDS_INPUT`
- `AGENTPET_STATE_COMPLETED`
- `AGENTPET_STATE_ERROR`

## 本机环境

- `SIFLI_SIM_SDK`：可选，默认使用仓库根目录的 `sdk/`。
- `SIFLI_ENV`：SiFli-ENV 工具目录；未设置时使用
  `simulator/build.bat` 中的默认值。
- Visual Studio 与 Windows SDK 路径由 `simulator/msvc_setup.bat` 配置。

若换电脑，请先确认以上工具路径可用。
