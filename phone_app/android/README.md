# Huangshan Badge for Android

This is the native Android companion for the Huangshan watch electronic badge.
It scans and connects to `Huangshan-Watch-BLE`, synchronizes the watch time, and
transfers a center-cropped 240 x 240 JPEG using the HWS1/WFPUSH2 BLE protocol.

## Build and install

The local build environment uses JDK 17 and the Android SDK at
`/Users/reus/Library/Android/sdk`.

```sh
cd phone_app/android
export JAVA_HOME=/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home
export ANDROID_HOME=/Users/reus/Library/Android/sdk
./gradlew :app:assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

On Android 12 and later, allow the app's Nearby devices permission, tap
`扫描手表`, choose `Huangshan-Watch-BLE`, and then choose an image.

The protocol contract is in `../../docs/huangshan-watch-protocol-v1.md`.

## SiFli firmware OTA

The Android client uses SiFli-SDK NOR Offline OTA through
`com.sifli:siflidfu:1.1.36`. The artifact is hosted in SiFli's authenticated
Maven repository. Supply credentials issued by SiFli without committing them to
this repository, using either environment variables:

```powershell
$env:SIFLI_MAVEN_USERNAME = "<issued username>"
$env:SIFLI_MAVEN_PASSWORD = "<issued password>"
```

or user-level Gradle properties in `%USERPROFILE%\.gradle\gradle.properties`:

```properties
sifliMavenUsername=<issued username>
sifliMavenPassword=<issued password>
```

Build the firmware with the `sf32lb52-lchspi-ulp` board. The project now builds
and flashes the DFU installer image in addition to the normal application image.
Create the phone package with SiFli-SDK's `imgtoolv37.exe`; for an HCPU plus DFU
update, the SDK example command is:

```text
imgtoolv37.exe gen_dfu --img_para hcpu 16 0 dfu 16 6 --com_type=0 --offline_img=2
```

Copy the generated `offline_install.bin` to the phone. In the app, connect the
watch, tap `选择升级包`, select that exact file, and tap `开始 OTA`. The current
board reserves 1152 KiB for `DFU_DOWNLOAD_REGION`; the app rejects larger or
renamed files. Keep the app open and do not power off the watch until it installs
the package and reboots.

## Resource differential update

UI images are stored in the watch `/ex` file system and can be updated without
sending a full firmware image. Select an `.apres` file under “资源差分升级”, then
tap “更新资源”. The app and device both validate the base/target versions, file
paths, payload size, and SHA-256 hashes. Packages are limited to 16 changed files
and 512 KiB of uncompressed transfer payload. Interrupted updates are cancelled
or rolled back from the device journal at the next boot.

Package generation and the factory-image requirement are documented in
[`../../docs/resource-differential-update.md`](../../docs/resource-differential-update.md).
