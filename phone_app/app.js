const LINK_SERVICE = "48535741-5443-485f-4c49-4e4b00000001";
const LINK_CHARACTERISTIC = "48535741-5443-485f-4c49-4e4b00000002";
const SERIAL_SERVICE = "7369666c-695f-7364-0000-000000000000";
const SERIAL_DATA = "7369666c-695f-7364-0002-000000000000";
const SERIAL_CATEGORY_WATCHFACE = 0x04;
const WATCHFACE_TYPE_BACKGROUND = 2;
const PHONE_TYPE_ANDROID = 2;
const CHUNK_SIZE = 180;
const MAX_IMAGE_SIZE = 2 * 1024 * 1024 - 4;

const elements = {
  connect: document.querySelector("#connectButton"),
  dot: document.querySelector("#connectionDot"),
  imageInput: document.querySelector("#imageInput"),
  send: document.querySelector("#sendButton"),
  refresh: document.querySelector("#refreshButton"),
  cancel: document.querySelector("#cancelButton"),
  clear: document.querySelector("#clearButton"),
  canvas: document.querySelector("#previewCanvas"),
  empty: document.querySelector("#emptyPreview"),
  overlay: document.querySelector("#transferOverlay"),
  progress: document.querySelector("#progressValue"),
  progressDetail: document.querySelector("#progressDetail"),
  deviceName: document.querySelector("#deviceName"),
  imageInfo: document.querySelector("#imageInfo"),
  transferInfo: document.querySelector("#transferInfo"),
};

let device;
let linkCharacteristic;
let serialCharacteristic;
let preparedImage;
let serialAssembly;
let uploadInProgress = false;
const serialMessages = [];
const serialWaiters = [];
const encoder = new TextEncoder();
const decoder = new TextDecoder();

function setTransferInfo(text) { elements.transferInfo.textContent = text; }
function isConnected() { return Boolean(device?.gatt?.connected && linkCharacteristic && serialCharacteristic); }

function updateControls() {
  const connected = isConnected();
  elements.dot.classList.toggle("connected", connected);
  elements.send.disabled = !connected || !preparedImage || uploadInProgress;
  elements.refresh.disabled = !connected;
  elements.cancel.disabled = !connected;
  elements.clear.disabled = !connected;
}

function setProgress(percent, detail) {
  elements.progress.textContent = `${Math.round(percent)}%`;
  elements.progressDetail.textContent = detail;
}

function showTransfer(visible) { elements.overlay.hidden = !visible; }

function u16(value) {
  const bytes = new Uint8Array(2);
  new DataView(bytes.buffer).setUint16(0, value, true);
  return bytes;
}

function u32(value) {
  const bytes = new Uint8Array(4);
  new DataView(bytes.buffer).setUint32(0, value, true);
  return bytes;
}

function joinBytes(...parts) {
  const size = parts.reduce((sum, part) => sum + part.length, 0);
  const result = new Uint8Array(size);
  let offset = 0;
  for (const part of parts) { result.set(part, offset); offset += part.length; }
  return result;
}

function crc32Mpeg2(data) {
  let crc = 0xffffffff;
  for (const byte of data) {
    crc ^= byte << 24;
    for (let bit = 0; bit < 8; bit += 1) {
      crc = (crc & 0x80000000) ? ((crc << 1) ^ 0x04c11db7) >>> 0 : (crc << 1) >>> 0;
    }
  }
  return crc >>> 0;
}

function watchfaceMessage(id, data = new Uint8Array()) {
  return joinBytes(u16(id), u16(data.length), data);
}

function serialMessageReceived(message) {
  if (message.length < 4) return;
  const index = serialWaiters.findIndex((waiter) => waiter.id === new DataView(message.buffer, message.byteOffset, message.byteLength).getUint16(0, true));
  if (index >= 0) {
    serialWaiters.splice(index, 1)[0].resolve(message);
  } else {
    serialMessages.push(message);
  }
}

function rejectSerialWaiters(error) {
  while (serialWaiters.length) serialWaiters.pop().reject(error);
  serialMessages.length = 0;
  serialAssembly = undefined;
}

function waitForWatchface(id, timeout = 8000) {
  const queued = serialMessages.findIndex((message) => new DataView(message.buffer, message.byteOffset, message.byteLength).getUint16(0, true) === id);
  if (queued >= 0) return Promise.resolve(serialMessages.splice(queued, 1)[0]);
  return new Promise((resolve, reject) => {
    const waiter = {
      id,
      resolve: (message) => { clearTimeout(timer); resolve(message); },
      reject: (error) => { clearTimeout(timer); reject(error); },
    };
    const timer = setTimeout(() => {
      const index = serialWaiters.indexOf(waiter);
      if (index >= 0) serialWaiters.splice(index, 1);
      reject(new Error("手表响应超时"));
    }, timeout);
    serialWaiters.push(waiter);
  });
}

function onSerialNotification(event) {
  const value = event.target.value;
  const packet = new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
  if (packet.length < 2 || packet[0] !== SERIAL_CATEGORY_WATCHFACE) return;
  const flag = packet[1];
  if (flag === 0 && packet.length >= 4) {
    serialMessageReceived(packet.slice(4));
  } else if (flag === 1 && packet.length >= 4) {
    serialAssembly = { expected: new DataView(packet.buffer).getUint16(2, true), data: [packet.slice(4)] };
  } else if (serialAssembly && (flag === 2 || flag === 3)) {
    serialAssembly.data.push(packet.slice(2));
    if (flag === 3) {
      const message = joinBytes(...serialAssembly.data).slice(0, serialAssembly.expected);
      serialAssembly = undefined;
      serialMessageReceived(message);
    }
  }
}

async function writeSerialPacket(packet) {
  if (serialCharacteristic.writeValueWithoutResponse) {
    await serialCharacteristic.writeValueWithoutResponse(packet);
  } else {
    await serialCharacteristic.writeValueWithResponse(packet);
  }
}

async function sendSerial(payload) {
  if (payload.length <= 16) {
    await writeSerialPacket(joinBytes(new Uint8Array([SERIAL_CATEGORY_WATCHFACE, 0]), u16(payload.length), payload));
    return;
  }
  let offset = 0;
  const first = payload.slice(0, 16);
  await writeSerialPacket(joinBytes(new Uint8Array([SERIAL_CATEGORY_WATCHFACE, 1]), u16(payload.length), first));
  offset += first.length;
  while (offset < payload.length) {
    const part = payload.slice(offset, offset + 18);
    offset += part.length;
    await writeSerialPacket(joinBytes(new Uint8Array([SERIAL_CATEGORY_WATCHFACE, offset === payload.length ? 3 : 2]), part));
  }
}

function responseStatus(message) {
  if (message.length < 6) throw new Error("手表响应格式错误");
  const status = new DataView(message.buffer, message.byteOffset, message.byteLength).getUint16(4, true);
  if (status !== 0) throw new Error(`手表拒绝传输，错误码 ${status}`);
}

async function sendCommand(command) {
  await linkCharacteristic.writeValueWithResponse(encoder.encode(command));
}

function onLinkNotification(event) {
  const text = decoder.decode(event.target.value);
  if (text.startsWith("badge:")) setTransferInfo(text);
}

async function connect() {
  if (!navigator.bluetooth) throw new Error("当前浏览器不支持 Web Bluetooth");
  device = await navigator.bluetooth.requestDevice({
    filters: [{ namePrefix: "Huangshan-Watch" }],
    optionalServices: [LINK_SERVICE, SERIAL_SERVICE],
  });
  device.addEventListener("gattserverdisconnected", () => {
    rejectSerialWaiters(new Error("手表已断开连接"));
    linkCharacteristic = undefined;
    serialCharacteristic = undefined;
    elements.deviceName.textContent = "已断开";
    setTransferInfo("等待连接");
    updateControls();
  });
  const server = await device.gatt.connect();
  const linkService = await server.getPrimaryService(LINK_SERVICE);
  linkCharacteristic = await linkService.getCharacteristic(LINK_CHARACTERISTIC);
  await linkCharacteristic.startNotifications();
  linkCharacteristic.addEventListener("characteristicvaluechanged", onLinkNotification);
  const serialService = await server.getPrimaryService(SERIAL_SERVICE);
  serialCharacteristic = await serialService.getCharacteristic(SERIAL_DATA);
  await serialCharacteristic.startNotifications();
  serialCharacteristic.addEventListener("characteristicvaluechanged", onSerialNotification);
  elements.deviceName.textContent = device.name || "Huangshan Watch";
  setTransferInfo("已连接");
  updateControls();
  await sendCommand("badge");
}

async function canvasBlob(canvas, quality) {
  return new Promise((resolve) => canvas.toBlob(resolve, "image/jpeg", quality));
}

async function prepareImage(file) {
  const image = await createImageBitmap(file);
  const canvas = elements.canvas;
  const context = canvas.getContext("2d");
  const edge = Math.min(image.width, image.height);
  context.fillStyle = "#000";
  context.fillRect(0, 0, canvas.width, canvas.height);
  context.drawImage(image, (image.width - edge) / 2, (image.height - edge) / 2, edge, edge, 0, 0, canvas.width, canvas.height);
  elements.empty.hidden = true;
  let quality = 0.9;
  let blob = await canvasBlob(canvas, quality);
  while (blob.size > MAX_IMAGE_SIZE && quality > 0.35) {
    quality -= 0.1;
    blob = await canvasBlob(canvas, quality);
  }
  if (blob.size > MAX_IMAGE_SIZE) throw new Error("图片压缩后仍超过 2 MB");
  preparedImage = new Uint8Array(await blob.arrayBuffer());
  elements.imageInfo.textContent = `${Math.round(preparedImage.length / 1024)} KB JPEG`;
  updateControls();
}

async function uploadImage() {
  if (!preparedImage || uploadInProgress) return;
  const padding = (4 - (preparedImage.length % 4)) % 4;
  const payload = joinBytes(preparedImage, new Uint8Array(padding));
  const upload = joinBytes(payload, u32(crc32Mpeg2(payload)));
  const fileName = encoder.encode("badge.jpg");
  uploadInProgress = true;
  updateControls();
  showTransfer(true);
  try {
    setProgress(0, "建立传输");
    await sendSerial(watchfaceMessage(0, joinBytes(u16(WATCHFACE_TYPE_BACKGROUND), new Uint8Array([PHONE_TYPE_ANDROID]), u32(upload.length))));
    responseStatus(await waitForWatchface(1));
    await sendSerial(watchfaceMessage(2, joinBytes(u32(upload.length), u16(fileName.length), fileName)));
    responseStatus(await waitForWatchface(3));
    let index = 0;
    for (let offset = 0; offset < upload.length; offset += CHUNK_SIZE) {
      const part = upload.slice(offset, offset + CHUNK_SIZE);
      await sendSerial(watchfaceMessage(4, joinBytes(u32(index), part)));
      responseStatus(await waitForWatchface(5));
      index += 1;
      setProgress(((offset + part.length) / upload.length) * 100, `正在发送 ${Math.round((offset + part.length) / 1024)} KB`);
    }
    await sendSerial(watchfaceMessage(6));
    responseStatus(await waitForWatchface(7));
    await sendSerial(watchfaceMessage(8));
    responseStatus(await waitForWatchface(9));
    setProgress(100, "已保存到手表");
    setTransferInfo("传输完成");
  } finally {
    uploadInProgress = false;
    updateControls();
    setTimeout(() => showTransfer(false), 900);
  }
}

elements.connect.addEventListener("click", () => connect().catch((error) => { setTransferInfo(error.message); updateControls(); }));
elements.imageInput.addEventListener("change", (event) => {
  const [file] = event.target.files;
  if (file) prepareImage(file).catch((error) => { setTransferInfo(error.message); });
});
elements.send.addEventListener("click", () => uploadImage().catch((error) => { setTransferInfo(error.message); showTransfer(false); }));
elements.refresh.addEventListener("click", () => sendCommand("badge").catch((error) => { setTransferInfo(error.message); }));
elements.cancel.addEventListener("click", () => sendCommand("badge cancel").catch((error) => { setTransferInfo(error.message); }));
elements.clear.addEventListener("click", () => sendCommand("badge clear").catch((error) => { setTransferInfo(error.message); }));
updateControls();
