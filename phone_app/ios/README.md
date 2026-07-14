# iOS Native Companion

This directory contains a no-dependency SwiftUI/CoreBluetooth implementation of
the Huangshan electronic-badge client. It uses the same custom GATT and SiFli
serial/WFPUSH2 frame protocol as `../app.js`, but works with iOS CoreBluetooth
instead of Web Bluetooth.

The current Mac has Command Line Tools only and no iPhone SDK, so this source
cannot be compiled or signed here yet. After installing Xcode, create a new iOS
App named `HuangshanBadge`, set the deployment target to iOS 16 or newer, add the
three Swift files from `HuangshanBadge/`, and add this `Info.plist` entry:

```xml
<key>NSBluetoothAlwaysUsageDescription</key>
<string>Used to send your selected badge image to the Huangshan watch.</string>
```

Run the project on a physical iPhone, select `Huangshan-Watch-BLE`, choose a
photo, and send it. Simulator builds cannot exercise Bluetooth. The native client
uses the negotiated CoreBluetooth write length, while the browser prototype keeps
the conservative 20-byte ATT framing required by Web Bluetooth.
