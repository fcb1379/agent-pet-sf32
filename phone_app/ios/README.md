# iOS Native Companion

This directory contains a no-dependency SwiftUI/CoreBluetooth implementation of
the Huangshan electronic-badge client. It uses the same custom GATT and SiFli
serial/WFPUSH2 frame protocol as `../app.js`, but works with iOS CoreBluetooth
instead of Web Bluetooth.

The current Mac has Command Line Tools only and no iPhone SDK, so this project
cannot be compiled or signed here yet. After installing Xcode, open
`HuangshanBadge.xcodeproj`, choose a unique signing team and bundle identifier,
then build for a physical iPhone. The project targets iOS 16 or newer and already
contains the Bluetooth privacy declaration.

Run the project on a physical iPhone, select `Huangshan-Watch-BLE`, choose a
photo, and send it. Simulator builds cannot exercise Bluetooth. The native client
uses the negotiated CoreBluetooth write length, while the browser prototype keeps
the conservative 20-byte ATT framing required by Web Bluetooth.

After both control and serial notifications are enabled, the client performs the
`HWS1` capability handshake and writes the current iPhone Unix time plus timezone
to the watch RTC. It registers every WFPUSH2 response before writing the matching
packet, and honors CoreBluetooth `writeWithoutResponse` backpressure during image
transfer. The control protocol is defined in `../../docs/huangshan-watch-protocol-v1.md`.
