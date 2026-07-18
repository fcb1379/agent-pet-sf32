#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
binary="${TMPDIR:-/tmp}/watch_protocol_host_test.$$"
trap 'rm -f "$binary"' EXIT

cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
  -I "$root/tests/watch_protocol_host/include" \
  -I "$root/work/watch_bt_audio_template/src/app_utils" \
  "$root/tests/watch_protocol_host/watch_protocol_host_test.c" \
  "$root/work/watch_bt_audio_template/src/app_utils/watch_protocol.c" \
  -o "$binary"

"$binary"
