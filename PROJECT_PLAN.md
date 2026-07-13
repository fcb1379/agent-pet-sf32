# Huangshan SF32LB52 Watch Audio/Bluetooth Project Plan

Date: 2026-07-07

## Project Goal

Build a smartwatch-style application for the LCSC/OSHWHub Huangshan SF32LB52 development board. The application starts from the SiFli-SDK watch UI demo and grows into a Bluetooth/audio product prototype with BLE phone interaction, iOS notification/media integration, Classic BT audio roles, local music playback, and speaker output.

## Hardware Baseline

- Board: Huangshan SF32LB52 development board V1.2
- Correct working board target for Huangshan board: `sf32lb52-lchspi-ulp`
- Earlier bring-up target `sf32lb52-lcd_n16r8` was build/flashable but did not light the Huangshan board LCD, so it must not be used as the product baseline.
- Verified serial port during setup: `/dev/cu.usbserial-110`
- SDK: `/Users/reus/sifli-sdk`
- SDK version verified: SiFli-SDK v2.4.0
- Toolchain verified: `scons`, `sftool`, `arm-none-eabi-gcc`
- Speaker: connected and should be used for local and Bluetooth audio playback

## Template Baseline

- Source demo copied from: `/Users/reus/sifli-sdk/example/multimedia/lvgl/watch`
- Local working template: `work/watch_bt_audio_template`
- Base UI: LVGL v8 watch demo
- Alternative candidate for later evaluation: `/Users/reus/sifli-sdk/example/multimedia/lvgl/watch_v9`
- Build command after adding the project-local Huangshan board overlay:
  - `cd work/watch_bt_audio_template/project`
  - `source /Users/reus/sifli-sdk/export.sh >/tmp/sifli-export.log && scons --board=sf32lb52-lchspi-ulp --board_search_path=../boards -j8`

Reason for current baseline: the LVGL v8 `watch` demo supports SF32LB52 LCD-class boards and also builds successfully for the Huangshan board target `sf32lb52-lchspi-ulp`.

## Required Features

### Electronic Badge

- Receive a user-selected image from a phone over BLE.
- Validate and persist the image without corrupting the previous image on interruption.
- Display the selected image as an electronic badge.
- Later support image library management and switching from the watch UI.

1. Watch UI
   - Use the existing SDK watch demo as the application UI template.
   - Keep the project buildable and flashable on `sf32lb52-lchspi-ulp`.

2. BLE phone link
   - BLE is the primary phone interaction channel.
   - Needs pairing/bonding and a stable connection model.
   - Use CTKD (Cross-Transport Key Derivation) for BLE + Classic BT one-step/dual transport pairing where supported by the phone.
   - Needs custom phone interaction service later, if app-side protocol is defined.

3. iOS ANCS
   - Support Apple Notification Center Service.
   - Receive iOS notifications over BLE and expose them to the watch UI/application layer.
   - Reference example: `/Users/reus/sifli-sdk/example/ble/ancs`
   - Key config references:
     - `CONFIG_BLUETOOTH=y`
     - `CONFIG_BSP_USING_DATA_SVC=y`
     - `CONFIG_BSP_USING_ANCS_SVC=y`
     - `CONFIG_PKG_USING_FLASHDB=y`

4. iOS AMS
   - Support Apple Media Service.
   - Track iOS player state, queue/track info, and media controls where supported.
   - Reference example: `/Users/reus/sifli-sdk/example/ble/ams`
   - Key config references:
     - `CONFIG_BLUETOOTH=y`
     - `CONFIG_BSP_USING_DATA_SVC=y`
     - `CONFIG_BSP_USING_AMS_SVC=y`
     - `CONFIG_PKG_USING_FLASHDB=y`

5. Classic BT A2DP source
   - Device connects to Bluetooth headphones/speakers and sends local audio to them.
   - Reference example: `/Users/reus/sifli-sdk/example/bt/music_source`
   - Key config references:
     - `CONFIG_RT_USING_BLUETOOTH=y`
     - `CONFIG_BLUETOOTH=y`
     - `CONFIG_BT_PROFILE_CUSTOMIZE=y`
     - `CONFIG_CFG_AV=y`
     - `CONFIG_CFG_AV_SRC=y`
     - `CONFIG_CFG_AVRCP=y`
     - `CONFIG_AUDIO=y`
     - `CONFIG_AUDIO_LOCAL_MUSIC=y`
     - `CONFIG_AUDIO_SPEAKER_USING_CODEC=y`

6. Classic BT A2DP sink
   - Other devices connect to this board and play music through the board speaker.
   - Reference example: `/Users/reus/sifli-sdk/example/bt/music_sink`
   - Key config references:
     - `CONFIG_RT_USING_BLUETOOTH=y`
     - `CONFIG_BLUETOOTH=y`
     - `CONFIG_BT_PROFILE_CUSTOMIZE=y`
     - `CONFIG_CFG_AV=y`
     - `CONFIG_CFG_AV_SNK=y`
     - `CONFIG_CFG_AVRCP=y`
     - `CONFIG_AUDIO=y`
     - `CONFIG_AUDIO_SPEAKER_USING_CODEC=y`

7. Simultaneous source/sink product behavior
   - Target behavior: support connecting to headphones for playback and also being connectable by another source device.
   - Engineering risk: Classic BT profile coexistence, routing policy, maximum ACL connections, and audio path arbitration need verification in SiFli stack.
   - Investigation references:
     - `middleware/bluetooth/Kconfig`
     - `/Users/reus/sifli-sdk/example/bt/music_source`
     - `/Users/reus/sifli-sdk/example/bt/music_sink`
     - `/Users/reus/sifli-sdk/example/bt/music_sink_with_relay`

8. Speaker and local audio
   - Speaker output should work for local playback and Bluetooth sink playback.
   - Local music should play from bundled/resource filesystem content first, then later from user storage if needed.
   - Reference example: `/Users/reus/sifli-sdk/example/multimedia/audio/local_music`
   - Important config references:
     - `CONFIG_AUDIO=y`
     - `CONFIG_AUDIO_LOCAL_MUSIC=y`
     - `CONFIG_AUDIO_SPEAKER_USING_CODEC=y`
     - Audio codec/process driver options in board menuconfig

## Milestones

### M0 - Baseline Preservation

Status: copied, build-verified, flashed, and visually confirmed for `sf32lb52-lchspi-ulp`

- Copy watch demo into local project template. Done: `work/watch_bt_audio_template`.
- Verify baseline template builds for `sf32lb52-lchspi-ulp`. Done on 2026-07-07.
- Flash baseline UI to board and confirm display/touch/button behavior. LCD display confirmed on 2026-07-07.

Baseline build note: the copied watch template builds successfully for `sf32lb52-lchspi-ulp`, with RAM usage around 69%. BLE, Classic BT, and audio integration must be done incrementally with memory usage checked after each step.

### M1 - Audio Bring-Up

Status: local playback path integrated; speaker runtime verification pending

- Enable audio codec/process/speaker options in the watch template. Done for first local playback path.
- Add minimal local music asset and local playback path. Done: `/16k.wav` in `fs_root`.
- Verify audio output through the connected speaker.

### M2 - BLE Foundation

Status: first bring-up firmware built/flashed; phone-side BLE verification pending

- Enable Bluetooth, BLE connection manager, NVDS/FlashDB, and data service.
- Confirm advertising, pairing, bonding, reconnect behavior.
- Decide the phone app/custom BLE protocol surface.

### M3 - ANCS + AMS

Status: integrated, UI-visible, build/flashed, iPhone runtime verification pending

- Merge ANCS service logic into the watch app.
- Merge AMS service logic into the watch app.
- Route notifications/media status into internal app events and UI.

### M4 - Classic BT A2DP Sink

Status: completed for first bring-up; phone pairing and speaker playback verified

- Merge music sink configuration and initialization.
- Make the board discoverable/connectable from phone or computer.
- Route received A2DP audio to the local speaker.

### M5 - Classic BT A2DP Source

Status: pending

- Merge music source configuration and commands/API.
- Scan/connect to headphones.
- Route local music to remote A2DP sink.

### M6 - Coexistence and Routing

Status: pending

- Validate BLE + Classic BT coexistence.
- Validate A2DP source + A2DP sink coexistence.
- Define audio routing policy:
  - local speaker playback
  - local music to headset
  - phone/computer A2DP input to speaker
  - conflict handling when source and sink are both active

### M7 - Productization

Status: pending

- Replace finsh-only control with watch UI/application control.
- Add persistent settings for paired devices, volume, and routing.
- Add recovery paths for disconnects, failed pairings, and audio underflow.
- Prepare a repeatable build/flash/test checklist.

## Open Questions

- Confirm whether the final UI should stay on LVGL v8 or migrate to LVGL v9.
- Confirm exact speaker hardware path: on-chip codec DAC, external amp enable pin, mute pin, and required board-level pinmux.
- Confirm expected phone platform priority: iOS only first, Android later, or both from the start.
- Confirm desired custom BLE phone protocol beyond ANCS/AMS.
- Confirm if simultaneous A2DP source and sink means simultaneous active audio streams, or just both roles available with one active route at a time.
- Confirm expected local music storage: embedded asset, root filesystem, SD card, external flash, or phone transfer.

## Immediate Next Tasks

1. Use an iPhone with nRF Connect or LightBlue to scan/connect `Huangshan-Watch-BLE`.
2. Accept the BLE pairing prompt; ANCS requires iOS pairing/bonding permission.
3. Allow notification sharing from iOS Bluetooth settings if prompted, then generate a test notification.
4. Start music playback on the iPhone and verify AMS metadata/control behavior through logs or the `iossvc` finsh command if console access is available.
5. After BLE/ANCS/AMS runtime verification, route notification/media state into the watch UI.

## Execution Task Breakdown

### T11 - Electronic Badge and BLE Image Transfer

Status: phase 1 receiver and viewer implemented; phone transfer verification pending

- Use the SDK BLE serial transport and watch-face transfer protocol so the official
  SiFli Android/iOS tooling can send files.
- Receive one JPEG with CRC validation into `/badge.tmp`, then atomically replace
  `/badge.jpg` only after a complete valid transfer.
- Limit the first implementation to 2 MB and reject non-JPEG payloads.
- Expose transfer status through the `badge` finsh command.
- Add a badge viewer UI that refreshes when a new image generation is committed.
- Follow-up: image library, deletion, selection, and a dedicated phone experience.

Result on 2026-07-09:

- Enabled the SDK BLE serial transport and watch-face transfer protocol:
  - `CONFIG_BSP_BLE_SERIAL_TRANSMISSION=y`
  - `CONFIG_BSP_BLE_WATCH_FACE=y`
- The serial transfer GATT service is initialized alongside the existing custom BLE,
  ANCS, and AMS services after BLE power-on.
- Added `badge_transfer.c/.h`:
  - accepts one JPEG up to 2 MB
  - validates SDK watch-face CRC32 and JPEG SOI bytes
  - writes to `/badge.tmp`
  - preserves `/badge.jpg` through `/badge.bak` during replacement and restores the
    backup after an interrupted commit
  - exposes `badge` finsh status and a snapshot API
- Added a main-menu application named `电子吧唧` / `Badge`:
  - displays `/badge.jpg` full-screen on a black background
  - keeps image aspect ratio and avoids upscaling
  - refreshes automatically after a successful new transfer
  - uses the existing Photos icon
- Added custom BLE text command `badge`, returning state, image availability,
  received/total bytes, generation, and last error.
- Full build passed for `sf32lb52-lchspi-ulp`; RAM remains about 78%.
- Flashed through `/dev/cu.usbserial-110`.
- Manual verification to run later:
  - Connect `Huangshan-Watch-BLE` using the SiFli BLE app.
  - Open `WFPUSH2`, choose a JPEG prepared as required by the SDK file-transfer
    tool, select file type `3`, enable byte alignment, and start transfer.
  - Open the watch main menu and select `电子吧唧`; expected the uploaded image to
    fill the display while preserving its aspect ratio.
  - Write `badge` to the custom BLE characteristic; expected `img=1`, `gen>=1`,
    and `err=0`.
  - Reboot and confirm the image remains available.
  - Interrupt a second transfer and confirm the previous image remains available.
  - Send a file with invalid CRC or a non-JPEG payload and confirm it is rejected.

UI cleanup on 2026-07-09:

- Removed the watch demo's hard-coded main-menu filler block, which created 60
  dimmed icons with the invalid application command `none`.
- The main menu now lists only applications exported through
  `BUILTIN_APP_EXPORT`, currently including the working Clock, Rotation 3D, and
  Electronic Badge applications.
- Manual verification to run later:
  - Open the main menu and confirm no dimmed placeholder icons remain.
  - Tap each remaining icon and confirm it opens a registered application.

Badge transfer usability update on 2026-07-09:

- Added `badge clear` on the finsh console to delete `/badge.jpg`, `/badge.tmp`,
  and `/badge.bak`, reset the transfer state, and bump the UI generation.
- Added BLE text command `badge clear`, returning `badge:clear:<ret>`, so phone-side
  tests can reset the badge image without opening a serial shell.
- The `电子吧唧` / `Badge` app now shows transfer progress while receiving and an
  error code after failed transfers. Existing images are hidden during active
  transfer so progress is visible.
- Manual verification to run later:
  - Send `badge clear` over BLE and confirm a `badge:clear:0` notification.
  - Reopen `电子吧唧`; expected `BLE image pending` after clearing.
  - Start a JPEG transfer while the app is open; expected `Receiving <percent>%`.
  - Force an invalid transfer; expected `Transfer failed` with a non-zero error.

Status app update on 2026-07-09:

- Added a main-menu application named `状态` / `Status` using the existing Settings
  icon.
- The page refreshes once per second and displays:
  - A2DP sink state, connected/streaming flags, last error, disconnect and recovery
    counts.
  - ANCS and AMS counters plus current iOS player/track strings when available.
  - Persisted local and Bluetooth volume settings.
  - Electronic badge transfer/image state.
- Added two table-side action buttons for low-risk recovery/testing:
  - `BT recover` calls `bt_audio_sink_request_recovery()`.
  - `Clear badge` calls `badge_transfer_clear()`.
- Full build passed for `sf32lb52-lchspi-ulp`; RAM remains about 78%.
- Manual verification to run later:
  - Open the main menu and confirm `状态` appears as a valid clickable icon.
  - Confirm the page opens, scrolls vertically, and refreshes changing values.
  - Tap `BT recover` and confirm the action result label changes.
  - Tap `Clear badge` and confirm the badge app returns to the pending state.

Music control app update on 2026-07-09:

- Added a main-menu application named `音乐` / `Music` using the existing iTunes
  icon.
- The page provides table-side controls for the current implemented audio paths:
  - Local `/16k.wav` play, pause, resume, and stop.
  - AMS previous, play/pause toggle, next, volume up, and volume down.
- The page refreshes once per second and displays the latest AMS player, playback,
  track, artist, and album strings when iOS provides them.
- Full build passed for `sf32lb52-lchspi-ulp`; RAM remains about 78%.
- Manual verification to run later:
  - Open the main menu and confirm `音乐` appears as a valid clickable icon.
  - Tap `Local play` and confirm the connected speaker plays `/16k.wav`.
  - Tap `Pause`, `Resume`, and `Local stop`; confirm the speaker follows.
  - With iPhone AMS connected, tap `Prev`, `Play/Pause`, `Next`, `Vol +`, and
    `Vol -`; confirm the phone player responds.
  - Confirm long track metadata wraps and the page can scroll vertically.

Text ghosting mitigation on 2026-07-09:

- User reported that text still had trailing/ghost artifacts, similar to an
  incorrect text-widget memory region.
- First mitigation treats it as a partial-refresh/transparent-label redraw issue:
  - Dynamic labels in the `状态` / `Status` app now use fixed-size opaque black
    backgrounds.
  - Dynamic labels in the `音乐` / `Music` app now use fixed-size opaque black
    backgrounds.
  - The `电子吧唧` / `Badge` progress/error label now uses an opaque black
    background and explicit invalidation around text updates.
  - Status and music pages now cache the previously rendered text and call
    `lv_label_set_text()` only when the content actually changes, reducing
    unnecessary redraws.
- Full build passed for `sf32lb52-lchspi-ulp`; RAM remains about 78%.
- Manual verification to run later:
  - Open `状态`, leave it running for at least 30 seconds, and confirm refreshed
    counters do not leave old glyphs behind.
  - Open `音乐`, connect AMS or change playback state, and confirm text changes
    erase cleanly.
  - Start a badge image transfer and confirm progress percentages do not leave
    residual digits.
  - If residual text remains, next investigation should move down to LVGL draw
    buffer stride/color-format/flush-area handling rather than label allocation.

Text ghosting mitigation, display-buffer pass on 2026-07-09:

- User reported that text ghosting still remained after the opaque-label changes.
- A full-screen direct LCD flush attempt was investigated but rejected for now:
  `LCD_FB_USING_NONE` is not compatible with the current SDK's `DRV_EPIC_NEW_API`
  path without patching `/Users/reus/sifli-sdk/middleware/lvgl/lv_drivers/lv_lcd.c`.
- Kept the proven LCD framebuffer path and increased the LVGL partial draw buffer
  height from 50 lines to 152 lines, matching several SDK LVGL examples. This
  reduces how often text updates are split across small strips.
- Added project-local `FRAME_BUFFER_IN_PSRAM` Kconfig support so the larger LVGL
  double buffer is placed in PSRAM instead of HCPU SRAM.
- Build verification passed for `sf32lb52-lchspi-ulp`:
  - `LV_FB_LINE_NUM=152`
  - `FRAME_BUFFER_IN_PSRAM=1`
  - RAM usage is about 63%; PSRAM usage is about 19%.
- Flashed through `/dev/cu.usbserial-110`.
- Readback verification passed:
  - `FTAB_CMP_EXIT=0`
  - `MAIN_CMP_EXIT=0`
  - `FS_ROOT_CMP_EXIT=0`
- Manual verification to run later:
  - Open the clock/status bar, `状态`, `音乐`, and `电子吧唧` pages and confirm
    refreshed text no longer leaves trailing glyphs.
  - If residual text still appears, next step should inspect the LCD framebuffer
    copy/flush area and CO5300 TE/VSYNC timing, not individual label allocation.

Text rendering diagnostic on 2026-07-09:

- User asked whether the abnormal text could be caused by anti-aliasing.
- Current finding: `LV_USE_FONT_SUBPX` is disabled, so it is not subpixel AA.
  However, normal grayscale font AA still uses alpha masks for glyph edges, and
  SiFli's LVGL port routes text through the EPIC `draw_letter` / `letter_blend`
  path.
- Added project option `WATCH_UI_DISABLE_EPIC_GPU`, enabled by default for this
  diagnostic build. After `littlevgl2rtt_init()`, the watch UI calls
  `lv_gpu_set_enable(false)` so LVGL uses software rendering for text/blending.
- Added finsh command for A/B comparison:
  - `uimode status`
  - `uimode gpu off`
  - `uimode gpu on`
- Build verification passed for `sf32lb52-lchspi-ulp`:
  - `WATCH_UI_DISABLE_EPIC_GPU=1`
  - RAM usage is about 63%; PSRAM usage is about 19%.
- Flashed through `/dev/cu.usbserial-110`.
- Readback verification passed:
  - `FTAB_CMP_EXIT=0`
  - `MAIN_CMP_EXIT=0`
  - `FS_ROOT_CMP_EXIT=0`
- Manual verification to run now:
  - Check text-heavy pages with the default `gpu=off` state.
  - Run `uimode status` over finsh; expected `gpu=off`.
  - If text is clean with GPU off, run `uimode gpu on` and confirm whether the
    abnormal text returns. That would isolate the issue to EPIC accelerated text
    alpha blending rather than LVGL label memory.

Text rendering diagnostic, LCD flush path pass on 2026-07-13:

- User reported that text was still abnormal with the EPIC GPU render callbacks
  disabled, so the issue is not isolated to `draw_letter` / `letter_blend`.
- Switched the project away from the SDK's `DRV_EPIC_NEW_API` render-list path
  as a diagnostic:
  - `# CONFIG_DRV_EPIC_NEW_API is not set`
  - the build now uses `lv_gpu.c` and the older `lcd_flush` path instead of
    `lv_gpu_new_api.c` and `lcd_flush_new_api`.
- Kept the previous display mitigations:
  - `LV_FB_LINE_NUM=152`
  - `FRAME_BUFFER_IN_PSRAM=1`
  - `WATCH_UI_DISABLE_EPIC_GPU=1`
- Build verification passed for `sf32lb52-lchspi-ulp`; RAM usage is about 54%,
  PSRAM usage is about 19%.
- Flashed through `/dev/cu.usbserial-110`.
- Readback verification passed:
  - `FTAB_CMP_EXIT=0`
  - `MAIN_CMP_EXIT=0`
  - `FS_ROOT_CMP_EXIT=0`
- Manual verification to run now:
  - Check whether text-heavy pages display correctly with the old LCD flush path.
  - If text is still abnormal, the next diagnostic should focus on font bitmap
    format / RGB565 byte order (`LV_COLOR_16_SWAP`) rather than EPIC render-list
    scheduling.

LCD flush path rollback on 2026-07-13:

- User reported that the legacy LCD flush diagnostic caused the entire screen to
  become corrupted.
- Reverted the project back to the newer SDK render-list path:
  - `CONFIG_DRV_EPIC_NEW_API=y`
- Rebuilt successfully for `sf32lb52-lchspi-ulp`; RAM usage returned to about
  63%, PSRAM usage about 19%.
- Flashed through `/dev/cu.usbserial-110`.
- Readback verification passed:
  - `FTAB_CMP_EXIT=0`
  - `MAIN_CMP_EXIT=0`
  - `FS_ROOT_CMP_EXIT=0`
- Do not continue with the legacy `lcd_flush` path on this board. Next display
  diagnostics should stay on `DRV_EPIC_NEW_API` and focus on RGB565 byte order,
  font bitmap format, or CO5300/LCDC data-lane configuration.

Flash verification policy update on 2026-07-13:

- User requested that subsequent firmware downloads do not perform flash readback
  comparisons, because they add substantial turnaround time without helping the
  current display diagnosis.
- Default deployment verification is now limited to a successful build and a
  successful UART download through `/dev/cu.usbserial-110`.
- Flash readback is retired from normal bring-up steps and may only be reintroduced
  when the user explicitly requests it for a suspected programming failure.

Display recovery pass on 2026-07-13:

- User reported the display had become completely blank after the unsuccessful
  legacy-flush experiment and its initial rollback.
- Restoring `DRV_EPIC_NEW_API` alone did not provide a dependable visual recovery,
  so the next recovery image returns to the board's earlier display allocation:
  `LV_FB_LINE_NUM=50` and LVGL draw buffers in HCPU SRAM.
- The temporary PSRAM framebuffer and runtime EPIC-GPU-disable diagnostics are
  now disabled. This keeps the known-good render-list path while removing the two
  recent display-memory/rendering variables before any further text investigation.
- Bluetooth, audio, BLE, and badge features are unchanged by this recovery pass.

RGB565 text-format diagnostic on 2026-07-13:

- The stable SRAM-buffer configuration restored the screen, but the user reports
  that text remains abnormal.
- The board uses an SPI LCD. The SDK documents `LV_COLOR_16_SWAP` for displays
  with an 8-bit/SPI transfer interface; when enabled, its LVGL LCD driver selects
  `RTGRAPHIC_PIXEL_FORMAT_RGB565P` for the framebuffer.
- Enable `CONFIG_LV_COLOR_16_SWAP=y` as the next isolated diagnostic. This changes
  RGB565 byte ordering only; it leaves the render path, buffer allocation, and all
  product features unchanged.

RGB565 diagnostic rollback on 2026-07-13:

- User reported that enabling RGB565 byte swapping prevented the screen from
  displaying at all.
- Revert `CONFIG_LV_COLOR_16_SWAP`; the recovered baseline remains the normal
  RGB565 framebuffer format with `DRV_EPIC_NEW_API` and a 50-line SRAM buffer.
- Further work must stay in the application text-control layer instead of changing
  LCD rendering, framebuffer allocation, or pixel-format configuration.

Text-control refresh pass on 2026-07-13:

- The dynamic text controls in the `status`, `music`, and `badge` apps and the
  clock status panel were manually invalidating labels both before and after each
  `lv_label_set_text` call.
- LVGL already invalidates a label's old and new extents when its text changes.
  Removed the redundant explicit invalidations so every dynamic update creates one
  standard label refresh rather than multiple overlapping refresh requests.
- No LCD, EPIC, framebuffer, font-engine, or color-format setting is changed by
  this pass.

Text-control bitmap-font diagnostic on 2026-07-13:

- The standard label-refresh pass did not resolve the reported text corruption.
- Enabled LVGL's built-in `UNSCII 16` font, whose glyph bitmaps are 1 bit per pixel
  and therefore do not use grayscale anti-aliasing.
- Applied it only to labels in the new status, music, badge, and clock status-panel
  controls. The rest of the system and every display-driver setting remain unchanged.
- This is a control-layer A/B test: clear UNSCII text would isolate the failure to
  anti-aliased Montserrat glyph handling; equally abnormal UNSCII text would rule
  out the label font bitmap as the primary cause.

Bitmap-font diagnostic rollback on 2026-07-14:

- User reported that the UNSCII text-control test caused the watch to freeze.
- Removed `CONFIG_LV_FONT_UNSCII_16` and restored the prior Montserrat font
  selections for all affected controls. The previous label-refresh simplification
  remains in place.
- Do not repeat the UNSCII font test. Future text investigation must avoid changes
  that alter the font object's compiled representation at runtime.

### T01 - Flash and Verify Watch Baseline

Status: flashed, binary-verified, and visually confirmed

- Build the copied watch template for `sf32lb52-lchspi-ulp`.
- Flash `bootloader.bin`, `main.bin`, and `ftab.bin`.
- Read back at least `ftab.bin` and one main image segment for binary comparison.
- Ask for/record visual confirmation that the watch UI boots on the LCD.

Result on 2026-07-07:

- First attempt used `sf32lb52-lcd_n16r8`; binary readback matched, but the LCD stayed blank. Treat this target as incorrect for the Huangshan board.
- Rebuilt for `sf32lb52-lchspi-ulp`, the board name documented for Huangshan follow-up development.
- Flashed through `/dev/cu.usbserial-110` using `work/watch_bt_audio_template/project/build_sf32lb52-lchspi-ulp_hcpu/uart_download.sh`.
- `ftab.bin` readback from `0x12000000` matched local file (`FTAB_CMP_EXIT=0`).
- `main.bin` readback from `0x12020000` matched local file (`MAIN_CMP_EXIT=0`).
- Manual check complete: user reported the board LCD can display the watch UI.

### T02 - Board Audio Capability Audit

Status: completed

- Compare `sf32lb52-lcd_n16r8` board config against `music_source`, `music_sink`, and `local_music`.
- Identify codec, audio process, speaker path, amp/mute pins, and required Kconfig options.
- Record exact config delta before editing.

Result on 2026-07-07:

- Correct board config is `/Users/reus/sifli-sdk/customer/boards/sf32lb52-lchspi-ulp/hcpu/board.conf`.
- Board enables audio process and codec with `CONFIG_BSP_ENABLE_AUD_PRC=y` and `CONFIG_BSP_ENABLE_AUD_CODEC=y`.
- External speaker amp path appears to use AW8155 with `CONFIG_PA_USING_AW8155=y` and `CONFIG_AW8155_GPIO_PIN=42`.
- Pinmux base maps PA42 as `AUDIO_PA_CTRL`.
- SDK `example/bt/music_sink` builds and flashes for `sf32lb52-lchspi-ulp`, giving a known-good A2DP sink reference.

### T03 - Local Speaker Playback

Status: integrated and build-verified; speaker runtime verification pending

- Enable minimum audio stack in the watch template.
- Add or reuse a small bundled local audio asset.
- Add a minimal finsh command or boot-time test hook to play local audio.
- Build, flash, and verify speaker output.

Result on 2026-07-08:

- Enabled SDK local music stack in `work/watch_bt_audio_template/project/proj.conf`:
  - `CONFIG_AUDIO_LOCAL_MUSIC=y`
  - this selects `CONFIG_PKG_USING_LIBHELIX=y` for MP3/WAV decode support.
- Added bundled local audio asset:
  - `work/watch_bt_audio_template/disk/16k.wav`
  - generated into `fs_root.bin` by the project build.
- Updated `work/watch_bt_audio_template/project/SConstruct` to build and flash `fs_root` from `../disk`.
- Updated `work/watch_bt_audio_template/src/app_utils/main.c` so the `FS_REGION` flash partition is registered and mounted at `/` during init.
- Added local playback module:
  - `work/watch_bt_audio_template/src/app_utils/local_music_player.c`
  - command: `localmusic status`
  - command: `localmusic play [path] [loop]`
  - command: `localmusic stop`
  - command: `localmusic pause`
  - command: `localmusic resume`
  - command: `localmusic vol <0-15>`
- Updated `work/watch_bt_audio_template/src/app_utils/SConscript` to compile the module when `AUDIO_LOCAL_MUSIC` is enabled.
- Build verification passed for `sf32lb52-lchspi-ulp`.
- Build generated `fs_root.bin` at 4 MB with one bundled file, `16k.wav`; `sftool_param.json` places it at `0x129A0000`.
- Manual verification to run later:
  - Boot firmware.
  - If console access is available, run `localmusic status`; expected `exists=1` for `/16k.wav`.
  - Run `localmusic vol 10`.
  - Run `localmusic play`; expected local WAV playback through the board speaker.
  - Run `localmusic pause`, `localmusic resume`, and `localmusic stop`.
  - Confirm local playback does not break the already verified A2DP sink path after reboot.

### T04 - BLE Foundation

Status: first bring-up firmware built/flashed; phone-side BLE verification pending

- Enable Bluetooth base, BLE connection manager, NVDS/FlashDB, and data service.
- Add a minimal BLE startup module.
- Verify advertising, pairing, bonding, and reconnect behavior.

Result on 2026-07-08:

- A2DP source work is intentionally deferred to a later milestone.
- Added BLE phone-link foundation module: `work/watch_bt_audio_template/src/app_utils/ble_watch_link.c`.
- The module listens for `BLE_POWER_ON_IND` from the existing unified Bluetooth stack; it does not call `sifli_ble_enable()` itself, avoiding double initialization with A2DP sink.
- BLE advertising name: `Huangshan-Watch-BLE`.
- Added a custom 128-bit GATT service with one characteristic:
  - read
  - write request
  - write command
  - notify
- Writes from the phone are stored as the latest payload and echoed back as a notification when notifications are enabled.
- A timer sends `tick:<rt_tick>` notifications every 5 seconds while notifications are enabled.
- Added finsh command: `blelink status|adv|notify <text>|clear`.
- Updated `project/proj.conf` to enable custom BT connection counts:
  - `CONFIG_BT_CON_NUM_CUSTOMIZE=y`
  - `CONFIG_CFG_MAX_BT_ACL_NUM=2`
  - `CONFIG_CFG_MAX_BT_BOND_NUM=3`
  - `CONFIG_BLE_CTKD_ENABLE=y`
- Rebuilt successfully for `sf32lb52-lchspi-ulp`; RAM usage is about 77%.
- Flashed through `/dev/cu.usbserial-110`.
- Readback verification passed: `FTAB_CMP_EXIT=0`, `MAIN_CMP_EXIT=0`.
- Manual verification required:
  - Use nRF Connect or LightBlue to scan for `Huangshan-Watch-BLE`.
  - Connect to it.
  - Find the custom 128-bit service whose UUID starts with ASCII `HSWATCH_LINK`.
  - Enable notifications on the characteristic.
  - Write a short ASCII value such as `ping`; the board should notify the same value back, then send periodic `tick:<rt_tick>` notifications.

### T05 - ANCS Integration

Status: integrated, UI-visible, build/flashed, iPhone runtime verification pending

- Merge ANCS service from the SDK example.
- Convert received notifications into app-level events.
- Add logging first, then UI display.

Result on 2026-07-08:

- Added iOS service bridge module: `work/watch_bt_audio_template/src/app_utils/ble_ios_services.c`.
- Enabled SDK data service and ANCS service in `work/watch_bt_audio_template/project/proj.conf`:
  - `CONFIG_BSP_USING_DATA_SVC=y`
  - `CONFIG_BSP_USING_ANCS_SVC=y`
- The bridge subscribes to SDK data service `ANCS`, configures notification attribute/category masks, and caches the latest notification app id, title, message, category, and UID.
- The SDK sample's automatic incoming-call rejection behavior was intentionally not copied.
- BLE connection handling now sends `ble_gap_security_request()` with `GAP_AUTH_REQ_SEC_CON_BOND`, so iOS can pair/bond before ANCS is enabled.
- CTKD is explicitly enabled with `CONFIG_BLE_CTKD_ENABLE=y`, so the SDK BLE connection manager includes `GAP_KDIST_LINKKEY` in pairing key distribution for BLE + Classic BT one-step pairing where the phone supports it.
- Added finsh command `iossvc status` to show cached ANCS state when console access is available.
- Added application snapshot API:
  - `work/watch_bt_audio_template/src/app_utils/ble_ios_services.h`
  - `ble_ios_services_get_snapshot()`
- Added status-bar UI binding:
  - `work/watch_bt_audio_template/src/gui_apps/clock/app_clock_status_bar.c`
  - Pulling down the watch status area now shows a `Phone` page with an `iPhone Notifications` dynamic card.

### T06 - AMS Integration

Status: integrated, UI-visible, build/flashed, iPhone runtime verification pending

- Merge AMS service from the SDK example.
- Track media/player metadata and controls.
- Connect AMS events to UI and later audio routing policy.

Result on 2026-07-08:

- Enabled SDK AMS service in `work/watch_bt_audio_template/project/proj.conf`:
  - `CONFIG_BSP_USING_AMS_SVC=y`
- The iOS bridge subscribes to SDK data service `AMS`, configures player/queue/track masks, enables CCCD, and caches player, playback, artist, album, and track fields.
- Added finsh command `iossvc` with AMS controls:
  - `iossvc status`
  - `iossvc play`
  - `iossvc pause`
  - `iossvc toggle`
  - `iossvc next`
  - `iossvc prev`
  - `iossvc volup`
  - `iossvc voldown`
  - `iossvc cmd <0-13>`
- Rebuilt successfully for `sf32lb52-lchspi-ulp` after ANCS/AMS integration.
- Flashed through `/dev/cu.usbserial-110`.
- Readback verification passed: `FTAB_CMP_EXIT=0`, `MAIN_CMP_EXIT=0`.
- Follow-up CTKD build on 2026-07-08 explicitly pinned `CONFIG_BLE_CTKD_ENABLE=y` in `proj.conf`, rebuilt, reflashed, and passed readback verification again: `FTAB_CMP_EXIT=0`, `MAIN_CMP_EXIT=0`.
- Follow-up UI build on 2026-07-08 added status-bar cards for `iPhone Notifications` and `iPhone Media`, rebuilt, reflashed, and passed readback verification again: `FTAB_CMP_EXIT=0`, `MAIN_CMP_EXIT=0`.
- Manual verification required on iPhone:
  - Scan for `Huangshan-Watch-BLE` using nRF Connect or LightBlue.
  - Connect and accept pairing.
  - After BLE pairing, check whether iOS also treats `Huangshan-Watch` Classic Bluetooth audio as paired/available without a separate full pairing flow.
  - If iOS asks for notification permission/sharing, allow it.
  - Generate a notification from another app to verify ANCS.
  - Pull down the watch status area and check the `Phone` page; the `iPhone Notifications` card should update after ANCS events.
  - Start music on the iPhone and check the `iPhone Media` card; AMS controls remain available through `iossvc` until UI buttons are added.
  - Runtime event observation is still limited because the current USB serial path has not produced usable logs; UI routing or a working console path should be added in a follow-up.

### T07 - A2DP Sink Integration

Status: completed for first bring-up

- Enable Classic BT A2DP sink and AVRCP.
- Make the board discoverable/connectable.
- Route phone/computer Bluetooth audio to the local speaker.

Result on 2026-07-07:

- Added minimal A2DP sink startup module: `work/watch_bt_audio_template/src/app_utils/bt_audio_sink.c`.
- The module enables Bluetooth, registers BT event callbacks, sets the Classic BT local name to `Huangshan-Watch`, tracks A2DP/AVRCP state, and exposes a finsh command: `btaudio status|clear|vol <0-15>`.
- Updated `work/watch_bt_audio_template/src/app_utils/SConscript` to include `bt_audio_sink.c` when `CONFIG_CFG_AV_SNK=y`, while avoiding missing optional `ble_app.c`/`bt_app.c` files.
- Enabled minimum BT/audio sink configs in `work/watch_bt_audio_template/project/proj.conf`.
- Built successfully for `sf32lb52-lchspi-ulp`; RAM usage is about 76.9% after adding BT/audio.
- Flashed through `/dev/cu.usbserial-110`.
- Readback verification passed: `FTAB_CMP_EXIT=0`, `MAIN_CMP_EXIT=0`.
- Manual verification still required: from phone/computer, scan Classic Bluetooth for `Huangshan-Watch`, pair/connect, play audio, and confirm speaker output.

Follow-up on 2026-07-07:

- User could not find `Huangshan-Watch` during Bluetooth scan.
- Root cause likely: the first integration set the local name but did not explicitly open Classic BT inquiry/page scan.
- Updated `bt_audio_sink.c` to call `bt_open_bt_request()` after BT stack ready and after setting local name.
- Added `btaudio discover` finsh command to reopen Classic BT inquiry/page scan manually.
- Rebuilt, reflashed, and verified flash readback again: `FTAB_CMP_EXIT=0`, `MAIN_CMP_EXIT=0`.

Second follow-up on 2026-07-07:

- User still could not find `Huangshan-Watch`.
- Core `.config` values were compared with SDK `music_sink`; Classic BT sink settings match for `RT_USING_BLUETOOTH`, `BSP_BLE_SIBLES`, `BT_FINSH`, `BT_PROFILE_CUSTOMIZE`, `CFG_AV`, `CFG_AV_SNK`, `CFG_AVRCP`, `BSP_BT_CONNECTION_MANAGER`, and `BTS2_APP_MENU`.
- Flash image list also matches the SDK reference layout: bootloader at `0x12010000`, main at `0x12020000`, ftab at `0x12000000`.
- UART log capture did not receive data at 1000000/921600/460800/115200 baud, so runtime BT state is not yet observable from the current USB serial path.
- Updated `bt_audio_sink.c` to also call `bt_interface_register_av_snk_sdp()`, `bt_av_snk_open()`, and `bt_interface_open_avrcp()` after BT stack ready.
- Added `btaudio open` finsh command to manually open A2DP sink/AVRCP and scan together.
- Rebuilt, reflashed, and verified flash readback again: `FTAB_CMP_EXIT=0`, `MAIN_CMP_EXIT=0`.

Third follow-up on 2026-07-07:

- User still could not find `Huangshan-Watch`.
- Flashed SDK reference firmware `/Users/reus/sifli-sdk/example/bt/music_sink/project` built for `sf32lb52-lchspi-ulp` as an A/B control.
- SDK reference flash readback passed: `FTAB_CMP_EXIT=0`, `MAIN_CMP_EXIT=0`.
- Manual check now required: scan Classic Bluetooth for `sifli_music_sink`.
- Interpretation:
  - If `sifli_music_sink` is visible, the issue is in watch-template integration/startup sequencing.
  - If `sifli_music_sink` is not visible, the issue is below the app layer: board/BT RF startup, phone scan method, firmware storage layout, boot state, or serial/diagnostic visibility.

Fourth follow-up on 2026-07-08:

- User reported the SDK reference can be discovered as `sifi music sink`/`sifli_music_sink`.
- This confirms the board BT RF path and phone scan method are working.
- Root cause for the watch integration was likely missing LCPU Bluetooth controller firmware in the watch project build.
- Compared project scripts:
  - SDK `music_sink/project/SConstruct` calls `AddLCPU(SIFLI_SDK, rtconfig.CHIP, "../src/lcpu_img.c")`.
  - Watch template `project/SConstruct` did not call `AddLCPU`.
  - SDK `music_sink/project/SConscript` includes the LCPU patch SConscript.
  - Watch template `project/SConscript` did not include that patch.
- Updated watch template build scripts to include `AddLCPU` and the LCPU patch SConscript.
- Rebuilt watch template; build output now includes `main.lcpu`, `build_sf32lb52-lchspi-ulp_hcpu/lcpu/lcpu.bin`, and `build_sf32lb52-lchspi-ulp_hcpu/lcpu/lcpu.c`.
- Reflashed watch template and verified readback: `FTAB_CMP_EXIT=0`, `MAIN_CMP_EXIT=0`.
- Manual check now required: scan Classic Bluetooth for `Huangshan-Watch`.

Final verification on 2026-07-08:

- User confirmed `Huangshan-Watch` is discoverable.
- User confirmed the board can connect and play Bluetooth music through the speaker.
- A2DP sink bring-up is complete on the watch template baseline.
- Known root cause of earlier failure: watch template did not include the LCPU Bluetooth controller firmware build path; adding `AddLCPU` and the LCPU patch fixed discoverability and playback.

### T08 - A2DP Source Integration

Status: pending

- Enable Classic BT A2DP source and AVRCP.
- Add scan/connect control for headphones.
- Route local audio to a connected Bluetooth headset.

### T09 - Coexistence and Routing Policy

Status: first one-active-speaker-route policy integrated; runtime verification pending

- Validate BLE + Classic BT coexistence.
- Validate A2DP source + sink role coexistence.
- Implement one-active-route policy unless simultaneous streams are proven stable.

Result on 2026-07-08:

- A2DP source remains intentionally deferred per product planning; this step only covers the already integrated local speaker routes:
  - local WAV playback through `AUDIO_TYPE_LOCAL_MUSIC`
  - phone/computer A2DP sink playback through `AUDIO_TYPE_BT_MUSIC`
- Added `work/watch_bt_audio_template/src/app_utils/bt_audio_sink.h`.
- Added `work/watch_bt_audio_template/src/app_utils/local_music_player.h`.
- Updated `bt_audio_sink.c` to track A2DP stream state via:
  - `BT_NOTIFY_A2DP_START_IND`
  - `BT_NOTIFY_A2DP_SUSPEND_IND`
  - profile disconnect
- Updated `bt_audio_sink.c` to stop local music when A2DP sink streaming starts.
- Updated `local_music_player.c` so `local_music_play_file()` returns `-RT_EBUSY` if A2DP sink is currently streaming, preventing local playback from stealing the speaker while a phone/computer is playing over Bluetooth.
- Updated `btaudio status` to report A2DP streaming state.
- Rebuilt successfully for `sf32lb52-lchspi-ulp`.
- Flashed through `/dev/cu.usbserial-110`.
- Readback verification passed:
  - `FTAB_CMP_EXIT=0`
  - `MAIN_CMP_EXIT=0`
  - `FS_ROOT_CMP_EXIT=0`
- Manual verification to run later:
  - Start A2DP sink playback from phone/computer, then run or trigger local music playback; expected local playback to be denied or stopped while A2DP is streaming.
  - Start local music first, then start A2DP playback from phone/computer; expected local music to stop and Bluetooth audio to take speaker ownership.
  - Pause/suspend Bluetooth playback, then start local music; expected local playback to be allowed.
  - Confirm BLE advertising/connection remains stable while Classic BT A2DP sink is connected and while local playback is idle/active.

### T10 - Product UI and Persistence

Status: BLE command control path and first persistent settings layer integrated; richer UI controls still pending

- Replace finsh-only controls with watch UI controls.
- Persist paired devices, routing mode, volume, and reconnect preferences.
- Add user-visible states for connection, playback, and errors.

Result on 2026-07-08:

- Began replacing finsh-only controls with a BLE-accessible text command path over the custom `Huangshan-Watch-BLE` service.
- Updated `work/watch_bt_audio_template/src/app_utils/ble_watch_link.c` so writes to the custom characteristic are parsed as commands. Unknown payloads still return `echo:<payload>` for backward-compatible bring-up testing.
- Supported BLE write commands:
  - `ping` -> notify `ok:pong`
  - `status` -> notify compact state including BLE connected, A2DP connected, A2DP streaming, ANCS count, and AMS count
  - `local play`
  - `local stop`
  - `local pause`
  - `local resume`
  - `ams play`
  - `ams pause`
  - `ams toggle`
  - `ams next`
  - `ams prev`
  - `ams volup`
  - `ams voldown`
- Updated `work/watch_bt_audio_template/src/app_utils/ble_ios_services.c` and `.h` to expose `ble_ios_services_send_ams_cmd()` for the BLE command layer.
- Build verification passed for `sf32lb52-lchspi-ulp`.
- Manual verification to run later:
  - Connect to `Huangshan-Watch-BLE`.
  - Enable notifications on the custom characteristic.
  - Write `ping`; expected notification `ok:pong`.
  - Write `status`; expected notification beginning `st:`.
  - Write `local play`; expected local playback if A2DP sink is not streaming.
  - Write `local stop`; expected local playback to stop.
  - Pair/bond iPhone for AMS, start phone music, then write `ams next` or `ams toggle`; expected iPhone media control action if AMS is connected and authorized.
  - Confirm unknown text still returns `echo:<payload>`.

Result update on 2026-07-08:

- Added a project-local board overlay at `work/watch_bt_audio_template/boards/sf32lb52-lchspi-ulp` so product-specific partition changes are tracked in this repository rather than patched into the SDK.
- Added a dedicated FlashDB preference partition:
  - FAL partition name: `prefdb`
  - Start address: `0x12DA8000`
  - Size: `0x00004000`
  - Purpose: product settings only; keep it separate from BLE bonding/NVDS partition `ble` and DFU partition `dfu`.
- Enabled persistent preference support:
  - `CONFIG_BSP_SHARE_PREFS=y`
  - `CONFIG_PKG_FDB_USING_FAL_MODE=y`
- Added `work/watch_bt_audio_template/src/app_utils/watch_settings.c/.h` as the single product settings API.
- Persisted settings in this first layer:
  - local music volume, default `8`
  - Bluetooth music volume, default `8`
  - route mode, currently `speaker-only`
- Updated `localmusic vol <0-15>` and `btaudio vol <0-15>` to save through `watch_settings`.
- Added `wsettings` finsh command:
  - `wsettings status`
  - `wsettings localvol <0-15>`
  - `wsettings btvol <0-15>`
  - `wsettings route speaker`
  - `wsettings apply`
- Extended BLE custom characteristic write commands:
  - `settings` -> notify `settings:local=<n>,bt=<n>,route=<n>`
  - `set localvol <0-15>`
  - `set btvol <0-15>`
  - `set route speaker`
- Extended BLE `status` response to include settings fields:
  - `lv=<local volume>`
  - `bv=<Bluetooth music volume>`
  - `rt=<route mode>`
- Added reset commands:
  - BLE write `reset settings` -> notify `reset:settings:<ret>`
  - finsh `wsettings reset`
- Build verification passed with project-local board overlay:
  - `scons --board=sf32lb52-lchspi-ulp --board_search_path=../boards -j8`
  - Generated `ptab.h` contains `KVDB_PREF_REGION_START_ADDR (0x12DA8000)` and `KVDB_PREF_REGION_SIZE (0x00004000)`.
- Flashed through `/dev/cu.usbserial-110`.
- Readback verification passed:
  - `FTAB_CMP_EXIT=0`
  - `MAIN_CMP_EXIT=0`
  - `FS_ROOT_CMP_EXIT=0`
- Manual verification to run later:
  - `wsettings status` should report `storage=1` after boot.
  - Run `wsettings localvol 5`, reboot, then `wsettings status`; expected `local_vol=5`.
  - Run `wsettings btvol 6`, reboot, then `wsettings status`; expected `bt_vol=6`.
  - Connect to `Huangshan-Watch-BLE`, enable notify, write `settings`; expected `settings:` response with the persisted values.
  - Write `set localvol 4` and `set btvol 7` over BLE, reboot, then confirm both persist.
  - Write `reset settings` over BLE, reboot, then confirm defaults are restored.
  - Confirm existing BLE pairing/bonding still survives independently of product settings because `prefdb` is separate from the `ble` partition.

Result update after reset/status extension on 2026-07-08:

- Rebuilt successfully for `sf32lb52-lchspi-ulp` with `--board_search_path=../boards`.
- Flashed through `/dev/cu.usbserial-110`.
- Readback verification passed:
  - `FTAB_CMP_EXIT=0`
  - `MAIN_CMP_EXIT=0`
  - `FS_ROOT_CMP_EXIT=0`

Result update on 2026-07-09:

- Added an A2DP sink health snapshot with lifecycle state, last error, disconnect count,
  recovery count, and last-event tick.
- Changed the BT startup worker so an 8-second stack-ready timeout is diagnostic only;
  it keeps waiting and can finish initialization when the delayed ready event arrives.
- A2DP disconnection now queues a recovery request in the BT worker, which reopens
  Classic BT inquiry/page scan without doing stack work directly in the event callback.
- Added manual recovery and diagnostics:
  - finsh `btaudio recover`
  - finsh `btaudio status` includes health counters
  - BLE write `health`
  - BLE write `recover bt`
- Rebuilt successfully for `sf32lb52-lchspi-ulp` with the project-local board
  overlay; RAM usage remains about 78%.
- Flashed through `/dev/cu.usbserial-110`.
- Manual verification to run later:
  - Pair and play A2DP audio, disconnect from the phone, and confirm the watch becomes
    discoverable/connectable again without rebooting.
  - BLE write `health`; expected response starts with `health:bt=` and recovery count
    increases after an A2DP disconnect or `recover bt`.
  - BLE write `recover bt`; expected `recover:bt:0` after the BT stack is ready.
