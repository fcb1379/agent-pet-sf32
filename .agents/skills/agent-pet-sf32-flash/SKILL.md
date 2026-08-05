---
name: agent-pet-sf32-flash
description: Build, validate, flash, and observe the Agent Pet firmware in this repository for the SiFli SF32LB52 Huangshan board. Use when asked to build firmware, burn or download images, flash a COM port, perform a device demo, or recover the exact sf32lb52-lchspi-ulp image addresses and sftool procedure.
---

# Agent Pet SF32 Flash

Use the project-owned `scripts/build_flash.ps1` so builds and downloads use the complete, validated four-image layout.

## Workflow

1. Confirm the current repository and inspect `git status` without modifying unrelated work.
2. Resolve the requested serial port. If none was supplied, use the `serial` skill to enumerate ports; never guess a port.
3. Build and validate without flashing:

   ```powershell
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.agents\skills\agent-pet-sf32-flash\scripts\build_flash.ps1 -Port COM5 -BuildOnly
   ```

4. Report the four image paths, sizes, SHA-256 hashes, and addresses printed by the script.
5. Flash only when the user explicitly requests a download, burn, or device demonstration:

   ```powershell
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\.agents\skills\agent-pet-sf32-flash\scripts\build_flash.ps1 -Port COM5 -Confirm:$false
   ```

6. For runtime observation, use the `serial` skill at 1,000,000 baud. The download UART may not carry application TX; if no runtime output is received, report that hardware-observation boundary instead of treating it as proof of firmware failure.

## Fixed Configuration

- Board: `sf32lb52-lchspi-ulp`
- Project: `work/watch_bt_audio_template/project`
- Build directory: `build_sf32lb52-lchspi-ulp_hcpu`
- Chip: `SF32LB52`
- Bootloader: `bootloader/bootloader.bin@0x12010000`
- Main image: `main.bin@0x12020000`
- Flash table: `ftab/ftab.bin@0x12000000`
- Filesystem: `fs_root.bin@0x129A0000`
- Flasher fallback: newest installed `%USERPROFILE%\.sifli\tools\sftool\*\sftool.exe`

Always rebuild by default. Use `-SkipBuild` only for an intentional repeat download; the script still rejects missing, empty, or stale outputs. Never flash only `main.bin` when a change touches files under `work/watch_bt_audio_template/disk`, because those assets are packaged into `fs_root.bin`.

## Validation Requirements

- SCons must exit successfully.
- All four images must exist and be non-empty.
- `main.bin` must be at least as new as application/project inputs.
- `fs_root.bin` must be at least as new as every disk asset.
- The requested COM port must be enumerated before flashing.
- Report SDK/compiler warnings separately from build or flash failures.
