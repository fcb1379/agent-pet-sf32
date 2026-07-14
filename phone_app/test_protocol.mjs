import assert from "node:assert/strict";

import {
  SERIAL_CATEGORY_WATCHFACE,
  crc32Mpeg2,
  makeUploadPayload,
  serialCarrierFrames,
  u32,
  watchfaceMessage,
} from "./protocol.js";

const bytes = (...values) => Uint8Array.from(values);

assert.equal(crc32Mpeg2(new TextEncoder().encode("123456789")), 0x0376e6e7);
assert.deepEqual([...watchfaceMessage(0, bytes(2, 0, 2, 12, 0, 0, 0))], [0, 0, 7, 0, 2, 0, 2, 12, 0, 0, 0]);

const carrier = serialCarrierFrames(Uint8Array.from({ length: 36 }, (_, index) => index));
assert.equal(carrier.length, 3);
assert.deepEqual([...carrier[0].slice(0, 4)], [SERIAL_CATEGORY_WATCHFACE, 1, 36, 0]);
assert.equal(carrier[0].length, 20);
assert.equal(carrier[1].length, 20);
assert.equal(carrier[1][1], 2);
assert.equal(carrier[2].length, 4);
assert.equal(carrier[2][1], 3);

const upload = makeUploadPayload(bytes(0xff, 0xd8, 0xff, 0xd9, 0x7f));
assert.equal(upload.length, 12);
assert.deepEqual([...upload.slice(5, 8)], [0, 0, 0]);
assert.deepEqual([...upload.slice(8)], [...u32(crc32Mpeg2(upload.slice(0, 8)))]);

console.log("badge protocol vectors passed");
