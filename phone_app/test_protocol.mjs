import assert from "node:assert/strict";

import {
  SERIAL_CATEGORY_WATCHFACE,
  LINK_CHARACTERISTIC_UUID,
  LINK_SERVICE_UUID,
  SERIAL_DATA_UUID,
  SERIAL_SERVICE_UUID,
  bluetoothUuidFromSifliLsb,
  controlRequest,
  crc32Mpeg2,
  makeUploadPayload,
  parseControlResponse,
  serialCarrierFrames,
  u32be,
  watchfaceMessage,
  watchfaceResponseStatus,
} from "./protocol.js";

const bytes = (...values) => Uint8Array.from(values);

assert.equal(bluetoothUuidFromSifliLsb("48535741-5443-485f-4c49-4e4b00000001"), "01000000-4b4e-494c-5f48-435441575348");
assert.equal(LINK_SERVICE_UUID, "01000000-4b4e-494c-5f48-435441575348");
assert.equal(LINK_CHARACTERISTIC_UUID, "02000000-4b4e-494c-5f48-435441575348");
assert.equal(SERIAL_SERVICE_UUID, "00000000-0000-0000-6473-5f696c666973");
assert.equal(SERIAL_DATA_UUID, "00000000-0000-0200-6473-5f696c666973");
assert.throws(() => bluetoothUuidFromSifliLsb("bad"), /invalid SiFli UUID/);
assert.equal(crc32Mpeg2(new TextEncoder().encode("123456789")), 0x0376e6e7);
assert.equal(controlRequest(7, "TIME", "1700000000,480"), "HWS1|7|TIME|1700000000,480");
assert.deepEqual(parseControlResponse("HWS1|7|OK|time=20260718T120000;tz=480"), {
  requestId: 7,
  status: "OK",
  payload: "time=20260718T120000;tz=480",
});
assert.deepEqual(parseControlResponse("HWS1|8|ERR|3"), {
  requestId: 8,
  status: "ERR",
  payload: "3",
});
assert.equal(parseControlResponse("badge:s=2"), undefined);
assert.deepEqual([...watchfaceMessage(0, bytes(2, 0, 2, 12, 0, 0, 0))], [0, 0, 7, 0, 2, 0, 2, 12, 0, 0, 0]);
assert.equal(watchfaceResponseStatus(bytes(1, 0, 0, 0, 0, 40, 3, 0)), 0);
assert.equal(watchfaceResponseStatus(bytes(3, 0, 28, 0)), 28);
assert.throws(() => watchfaceResponseStatus(bytes(1, 0, 0)), /invalid watchface response/);

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
assert.deepEqual([...upload.slice(8)], [...u32be(crc32Mpeg2(upload.slice(0, 8)))]);

console.log("badge protocol vectors passed");
