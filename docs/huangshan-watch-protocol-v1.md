# Huangshan Watch Communication Protocol v1.0

## Scope

This protocol defines the phone-to-watch application control plane. It is carried
by the writable/notifiable custom GATT characteristic. SiFli stores UUID bytes
LSB-first; Web Bluetooth and CoreBluetooth clients must use the displayed UUIDs:

| Endpoint | SiFli LSB-first bytes | Web Bluetooth / CoreBluetooth UUID |
| --- | --- | --- |
| Control service | `48 53 57 41 54 43 48 5f 4c 49 4e 4b 00 00 00 01` | `01000000-4b4e-494c-5f48-435441575348` |
| Control characteristic | `48 53 57 41 54 43 48 5f 4c 49 4e 4b 00 00 00 02` | `02000000-4b4e-494c-5f48-435441575348` |
| SiFli serial service | `73 69 66 6c 69 5f 73 64 00 00 00 00 00 00 00 00` | `00000000-0000-0000-6473-5f696c666973` |
| SiFli serial data | `73 69 66 6c 69 5f 73 64 00 02 00 00 00 00 00 00` | `00000000-0000-0200-6473-5f696c666973` |

The characteristic is deliberately used for compact control messages only. Image
bytes continue to use the SiFli serial/WFPUSH2 GATT service (category `0x04`),
which already has framing, acknowledgement, and CRC support.

Each WFPUSH2 acknowledgement begins with a little-endian `uint16` command id
followed by a little-endian `uint16` status. Clients must read the status at
byte offset 2; start acknowledgements append transfer-capability fields after it.

WFPUSH2 file sizes, packet indexes, and message fields are little-endian. The
four-byte MPEG-2 CRC32 appended after the padded JPEG payload is the exception:
it is encoded big-endian (network byte order). The watch compares it as
`(byte[0] << 24) | ... | byte[3]`; writing this field little-endian yields
`CRC_CALCULATE_ERROR` (status `36`) near the final packet.

## Control Frames

All frames are UTF-8 ASCII and must be no longer than 63 bytes excluding the
terminating NUL byte used by the watch implementation.

The watch requests ATT MTU exchange immediately after a BLE connection. Clients
must wait until GATT service/notification setup completes before sending control
frames; the application protocol itself does not fragment a control frame.

Request:

```text
HWS1|<request-id>|<operation>[|<payload>]
```

Response notification:

```text
HWS1|<request-id>|OK[|<payload>]
HWS1|<request-id>|ERR|<error-code>
```

The watch may also send the unsolicited notification `HWS1|0|TIME_REQ` after a
BLE connection is established. A companion that receives it must reply with a
normal `TIME` request after it has finished GATT setup. The Android companion
also remembers the last watch address and reconnects to satisfy this request
after either device restarts.

`request-id` is a decimal integer in the range `1..65535`. The phone must use a
new id for every outstanding request and match the response id. The watch handles
one request per GATT write; clients should serialize requests.

Error codes:

| Code | Meaning |
| --- | --- |
| `1` | malformed frame |
| `2` | unsupported operation |
| `3` | invalid payload or value out of range |
| `4` | watch runtime or storage failure |

## v1 Operations

| Operation | Payload | Response payload | Purpose |
| --- | --- | --- | --- |
| `HELLO` | none | `model=HS52;cap=TIME,BADGE,STATE,TIME_REQ` | Capability handshake after connect. |
| `TIME` | `<unix-seconds>,<utc-offset-minutes>` | `time=YYYYMMDDTHHMMSS;tz=<minutes>` | Set the watch local RTC time. The phone sends UTC Unix seconds and its offset east of UTC. |
| `STATE` | none | `time=...;tz=...;img=<0|1>` | Read the current local watch time, saved timezone offset, and badge availability. |
| `BADGE` | `STATUS`, `CLEAR`, or `CANCEL` | `i=<0|1>;s=<state>;r=<bytes>;t=<bytes>;e=<error>` for `STATUS`, or `action=<...>` | Control the image-transfer session; image payloads use WFPUSH2. |
| `MEDIA` | reserved | reserved | Future phone media state/control bridge. |
| `NOTIFY` | reserved | reserved | Future app notification bridge. |
| `FIND` | reserved | reserved | Future find-watch alert. |

The v1 watch accepts `TIME` from 2020-01-01 through 2038-01-01 and timezone
offsets from -840 through +840 minutes. It converts UTC plus offset into the
watch's local RTC value and persists both the offset and source UTC timestamp.

## Image Transfer

1. Complete `HELLO` and `TIME` after connecting.
2. Send `BADGE|STATUS` or use the legacy `badge` command to display transfer
   state.
3. Encode a 240 x 240 JPEG locally, then send it with WFPUSH2 over the SiFli
   serial transport using category `0x04`.
4. The watch validates alignment, MPEG-2 CRC32, free space, and JPEG markers
   before atomically replacing `/badge.jpg`.
5. Optionally send `BADGE|CLEAR` or `BADGE|CANCEL` through this control protocol.

## Compatibility and Security

- Legacy text commands such as `badge`, `badge clear`, and `status` remain
  available during the v1 migration.
- The control service is currently unencrypted so Chrome Web Bluetooth can
  discover it. ANCS/AMS pairing remains independent and may be initiated by iOS
  when those services require it.
- Future privileged operations must require an authenticated/bonded session and
  allocate a new v2 capability instead of silently changing v1 semantics.

## Contract Tests

Run the firmware-side host contract test from the repository root before changing
this protocol implementation:

```sh
sh tests/run_watch_protocol_host_test.sh
```

It compiles the production `watch_protocol.c` with mocked RTC, settings, and badge
storage dependencies. The test covers handshake, valid and failed time writes,
status formatting, badge actions, malformed frames, error codes, and the 64-byte
custom-characteristic response limit.
