# Huangshan Watch Communication Protocol v1.0

## Scope

This protocol defines the phone-to-watch application control plane. It is carried
by the writable/notifiable custom GATT characteristic:

- Service: `48535741-5443-485f-4c49-4e4b00000001`
- Characteristic: `48535741-5443-485f-4c49-4e4b00000002`

The characteristic is deliberately used for compact control messages only. Image
bytes continue to use the SiFli serial/WFPUSH2 GATT service (category `0x04`),
which already has framing, acknowledgement, and CRC support.

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
| `HELLO` | none | `model=HS52;cap=TIME,BADGE,STATE` | Capability handshake after connect. |
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
