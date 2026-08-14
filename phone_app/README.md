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
image, then send it. Refresh, cancellation, and deletion use the versioned
`HWS1` `STATE` and `BADGE` control operations through the custom GATT
characteristic.

After the connection is established, the client negotiates `HWS1` and syncs the
watch RTC from the phone's Unix time and timezone. It then reads the image and
transfer state; use `同步时间` or `刷新状态` to run these operations again. The wire
contract is documented in `../docs/huangshan-watch-protocol-v1.md`.

For the web companion, choose JPG, PNG, or WebP. HEIC selection is deliberately
handled by the native iOS client because browser HEIC decoding is not consistent.

The app has a web manifest and a network-first service-worker cache, so it can be
installed as an Android PWA after serving it through HTTPS while still retaining
an offline fallback. `localhost` is sufficient for desktop development only; an
Android phone needs an HTTPS host (for example, a GitHub Pages deployment) because
Web Bluetooth is a secure-context API.

## Android HTTPS Deployment

The repository includes `.github/workflows/deploy-phone-app.yml`, which publishes
`phone_app/` through GitHub Pages whenever this directory changes on `main`. In
the repository's **Settings > Pages**, select **GitHub Actions** as the publishing
source once. After the workflow succeeds, open its deployment URL on Android
Chrome, install it as a PWA if desired, and connect to `Huangshan-Watch-BLE`.
GitHub Pages serves the companion via HTTPS, which satisfies the Web Bluetooth
secure-context requirement.

Run the protocol-only checks without a Bluetooth device:

```sh
node test_protocol.mjs
```

The native Android client also provides SiFli NOR Offline firmware OTA. OTA is
not exposed by the browser/PWA because the official SiFli transfer library is a
native mobile SDK. See `android/README.md` for dependency credentials, package
generation, size limits, and the upgrade procedure.
