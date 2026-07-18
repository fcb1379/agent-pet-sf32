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

After the connection is established, the client negotiates `HWS1` and syncs the
watch RTC from the phone's Unix time and timezone. Use `同步时间` to run it again.
The wire contract is documented in `../docs/huangshan-watch-protocol-v1.md`.

For the web companion, choose JPG, PNG, or WebP. HEIC selection is deliberately
handled by the native iOS client because browser HEIC decoding is not consistent.

The app has a web manifest and service-worker cache, so it can be installed as an
Android PWA after serving it through HTTPS. `localhost` is sufficient for desktop
development only; an Android phone needs an HTTPS host (for example, a GitHub
Pages deployment) because Web Bluetooth is a secure-context API.

Run the protocol-only checks without a Bluetooth device:

```sh
node test_protocol.mjs
```
