# 资源差分升级

本工程将界面图片和 `tiny55_full.ttf` 从 HCPU 代码镜像移到了 `/ex` 文件系统。工厂镜像仍包含完整资源；后续只把发生变化的文件打成 `.apres` 差分包，通过 Android App 的“资源差分升级”入口发送。

## 容量边界

- `FS_REGION`：4 MiB，当前图片、字库和音频共约 3.52 MiB。
- 单个 `.apres` 的未压缩传输负载：最多 512 KiB。
- 单包最多更新 16 个文件。
- 支持 `resource/*.bin` 和 `font/*.ttf`，不支持删除文件。
- 当前完整字库约 1.04 MiB，不能放入差分包；字库变更应随工厂/完整文件系统镜像发布。

512 KiB 限制为原文件大小之和（每个文件另计 4 字节 WFPUSH CRC），不是 ZIP 压缩后的大小。这样设备能在保留旧文件用于回滚的同时安全地完成更新。

## 生成工厂资源

在 `work/watch_bt_audio_template` 下运行。Windows 使用 `ezip.exe`：

```powershell
python tools/resource_bundle.py build `
  --sources src `
  --image-sources src/resource/images/common `
  --ezip ../../../sdk/tools/png2ezip/ezip.exe `
  --font src/resource/fonts/freetype/tiny55_full.ttf `
  --output disk/ex `
  --version 1
```

Linux/WSL 将转换器替换为 `../../../sdk/tools/png2ezip/ezip_linux`。该命令可从原始 PNG 和 TTF 清洁重建，不依赖旧的 `build_*` 目录。生成内容包括：

- `disk/ex/resource/*.bin`
- `disk/ex/font/tiny55_full.ttf`
- `disk/ex/manifest.json`
- `disk/ex/resource.version`

启用 `CONFIG_AGENT_PET_EXTERNAL_RESOURCES=y` 后，固件中的 `LV_EXT_IMG_GET()` 会解析为 `/ex/resource/<name>.bin`。因此首次部署必须烧录包含 `fs_root.bin` 的完整工厂镜像，不能只升级 HCPU。

## 生成差分包

发布每个资源版本时，必须保存该版本的完整目录和 `manifest.json`。以下示例以版本 1 为基线、版本 2 为目标：

```powershell
python tools/resource_bundle.py build `
  --sources src `
  --image-sources src/resource/images/common `
  --ezip ../../../sdk/tools/png2ezip/ezip.exe `
  --font src/resource/fonts/freetype/tiny55_full.ttf `
  --output out/resources-v2 `
  --version 2

python tools/resource_bundle.py diff `
  --base releases/resources-v1/manifest.json `
  --target out/resources-v2/manifest.json `
  --output out/agent-pet-res-v1-v2.apres
```

`diff` 只写入 SHA-256 发生变化的文件，并拒绝以下情况：基线与目标版本相同、删除文件、超过 16 个文件、负载超过 512 KiB、目标文件与清单的大小或 SHA-256 不一致。

## App 操作

1. 用 Android App 正常连接手表，保持 HWS1 连接就绪。
2. 在“资源差分升级”中选择 `.apres`。
3. App 会先检查 ZIP 路径、版本、文件数量、负载和每个文件的 SHA-256。
4. 点击“更新资源”。设备会再次校验当前 `resource.version` 必须等于包内 `baseVersion`。
5. 所有文件写入暂存区并通过 WFPUSH CRC、文件长度和 SHA-256 校验后，设备才提交新版本。

传输中断时 App 会发送 `RESOURCE CANCEL`。即使断电或连接已丢失，设备也会在下次启动时读取 `/ex/.update/journal`，回滚未完成的替换；`resource.version` 始终最后提交，避免出现“版本已更新但文件不完整”。

## 与整机 OTA 的关系

资源差分包不经过 `DFU_DOWNLOAD_REGION`，它通过应用协议直接更新 `/ex`。当前完整构建结果中，`main.bin` 约 2.84 MiB；SiFli 官方工具压缩后的纯 HCPU `offline_install.bin` 仍约 1.98 MiB，超过现有 1152 KiB 下载区。因此：

- 普通图片改动使用 `.apres`，可显著缩短 BLE 传输时间。
- C/C++ 代码变更仍属于整机 OTA，不能伪装成资源差分包。
- 若要在现有硬件上支持完整代码 OTA，需要另行扩大 `DFU_DOWNLOAD_REGION`，或实现并验证 bootloader 级二进制补丁方案。

