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
