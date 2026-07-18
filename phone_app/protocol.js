export const SERIAL_CATEGORY_WATCHFACE = 0x04;
export const CONTROL_PROTOCOL_VERSION = "HWS1";
export const WATCHFACE_TYPE_BACKGROUND = 2;
export const PHONE_TYPE_ANDROID = 2;
export const CHUNK_SIZE = 180;
export const MAX_IMAGE_SIZE = 2 * 1024 * 1024 - 4;

export function u16(value) {
  const bytes = new Uint8Array(2);
  new DataView(bytes.buffer).setUint16(0, value, true);
  return bytes;
}

export function u32(value) {
  const bytes = new Uint8Array(4);
  new DataView(bytes.buffer).setUint32(0, value, true);
  return bytes;
}

export function joinBytes(...parts) {
  const size = parts.reduce((sum, part) => sum + part.length, 0);
  const result = new Uint8Array(size);
  let offset = 0;
  for (const part of parts) { result.set(part, offset); offset += part.length; }
  return result;
}

export function crc32Mpeg2(data) {
  let crc = 0xffffffff;
  for (const byte of data) {
    crc ^= byte << 24;
    for (let bit = 0; bit < 8; bit += 1) {
      crc = (crc & 0x80000000) ? ((crc << 1) ^ 0x04c11db7) >>> 0 : (crc << 1) >>> 0;
    }
  }
  return crc >>> 0;
}

export function watchfaceMessage(id, data = new Uint8Array()) {
  return joinBytes(u16(id), u16(data.length), data);
}

export function serialCarrierFrames(payload) {
  if (payload.length <= 16) {
    return [joinBytes(new Uint8Array([SERIAL_CATEGORY_WATCHFACE, 0]), u16(payload.length), payload)];
  }
  const frames = [joinBytes(new Uint8Array([SERIAL_CATEGORY_WATCHFACE, 1]), u16(payload.length), payload.slice(0, 16))];
  for (let offset = 16; offset < payload.length; offset += 18) {
    const part = payload.slice(offset, offset + 18);
    frames.push(joinBytes(new Uint8Array([SERIAL_CATEGORY_WATCHFACE, offset + part.length === payload.length ? 3 : 2]), part));
  }
  return frames;
}

export function makeUploadPayload(jpeg) {
  const padding = (4 - (jpeg.length % 4)) % 4;
  const payload = joinBytes(jpeg, new Uint8Array(padding));
  return joinBytes(payload, u32(crc32Mpeg2(payload)));
}

export function controlRequest(requestId, operation, payload = "") {
  const suffix = payload ? `|${payload}` : "";
  return `${CONTROL_PROTOCOL_VERSION}|${requestId}|${operation}${suffix}`;
}

export function parseControlResponse(text) {
  const parts = text.split("|");
  if (parts.length < 3 || parts[0] !== CONTROL_PROTOCOL_VERSION || !/^\d+$/.test(parts[1])) return undefined;
  return {
    requestId: Number(parts[1]),
    status: parts[2],
    payload: parts.slice(3).join("|"),
  };
}
