# PC 完整手表模拟器

模拟器直接编译硬件工程的完整 LVGL 应用图，包括主表盘、应用启动器、闹钟、计时器、音乐、计算器、状态页和宠物功能。硬件 UI 修改后不需要复制代码，重新构建即可在 PC 上预览。

## 一键预览

双击 `preview_simulator.bat`。普通 Git clone 首次运行时，脚本会自动初始化仓库固定版本的 `sdk` 子模块，无需手工执行子模块命令。首次运行需要能够访问 GitHub。

脚本会：

1. 使用 `project/pc_hcpu` 配置编译完整手表工程；
2. 使用 PC 服务桩替代蓝牙、音频和持久化硬件接口；
3. 启动 `project/build_pc_hcpu/main.exe`。

默认模拟分辨率为 390×490，启动后首先显示主表盘，可通过手表界面进入各个应用。

## 手动构建

```bat
cd simulator
build.bat
..\project\build_pc_hcpu\main.exe
```

## 同步边界

| 目录 | 用途 | 同步方式 |
|---|---|---|
| `src/gui_apps/` | 硬件与模拟器共用的完整 LVGL 应用 | 模拟器直接编译，自动同步 |
| `src/resource/` | 图片、字体和字符串资源 | 模拟器直接编译，自动同步 |
| `src/app_utils/` | 应用服务接口 | 硬件使用真实服务，PC 使用 `pc_simulator_services.c` |
| `project/pc_hcpu/` | PC 平台配置 | 仅模拟器 |
| `sim_src/` | 旧的宠物单页预览入口 | 保留兼容，不是默认入口 |

宠物布局、木鱼点击互动以及其他应用界面均来自硬件使用的同一套源码。平台差异仅通过 `BSP_USING_PC_SIMULATOR` 隔离硬件服务。

## 本机环境

- `SIFLI_SIM_SDK`：可选，默认使用仓库根目录的 `sdk/`。
- `SIFLI_ENV`：可选；未配置时使用系统 Python、SCons 和 MSVC。
- Visual Studio 与 Windows SDK 路径由 `simulator/msvc_setup.bat` 自动配置。

换用其他电脑时，请先确认 Python、SCons、MSVC 和 Windows SDK 可用。
