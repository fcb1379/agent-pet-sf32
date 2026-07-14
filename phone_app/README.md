# Huangshan Badge Companion

This is a local-first phone companion for the Huangshan watch electronic badge.
It center-crops an image to 240 x 240, encodes JPEG locally, and sends it through
the SiFli BLE serial/WFPUSH2 transport used by the watch firmware.

Run it from a secure context. Android Chrome supports Web Bluetooth directly;
desktop Chrome can be used for protocol development. iOS requires a later native
wrapper because Safari does not expose Web Bluetooth.

```sh
cd phone_app
python3 -m http.server 8080
```

Open `http://localhost:8080` in Chrome, connect `Huangshan-Watch-BLE`, choose an
image, then send it. The app also exposes the `badge`, `badge cancel`, and
`badge clear` controls through the custom GATT characteristic.
