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

Reason for current baseline: the LVGL v8 `watch` demo supports SF32LB52 LCD-class boards and also builds successfully for the Huangshan board target `sf32lb52-lchspi-ulp`.

## Required Features

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

Status: BLE command control path started; UI and persistence still pending

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
