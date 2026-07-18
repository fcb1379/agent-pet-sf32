import {
  CHUNK_SIZE,
  LINK_CHARACTERISTIC_UUID,
  LINK_SERVICE_UUID,
  MAX_IMAGE_SIZE,
  PHONE_TYPE_ANDROID,
  SERIAL_CATEGORY_WATCHFACE,
  SERIAL_DATA_UUID,
  SERIAL_SERVICE_UUID,
  WATCHFACE_TYPE_BACKGROUND,
  controlRequest,
  joinBytes,
  makeUploadPayload,
  parseControlResponse,
  serialCarrierFrames,
  u16,
  u32,
  watchfaceMessage,
  watchfaceResponseStatus,
} from "./protocol.js";

const LINK_SERVICE = LINK_SERVICE_UUID;
const LINK_CHARACTERISTIC = LINK_CHARACTERISTIC_UUID;
const SERIAL_SERVICE = SERIAL_SERVICE_UUID;
const SERIAL_DATA = SERIAL_DATA_UUID;

const elements = {
  connect: document.querySelector("#connectButton"),
  dot: document.querySelector("#connectionDot"),
  imageInput: document.querySelector("#imageInput"),
  send: document.querySelector("#sendButton"),
  refresh: document.querySelector("#refreshButton"),
  cancel: document.querySelector("#cancelButton"),
  clear: document.querySelector("#clearButton"),
  syncTime: document.querySelector("#syncTimeButton"),
  canvas: document.querySelector("#previewCanvas"),
  empty: document.querySelector("#emptyPreview"),
  overlay: document.querySelector("#transferOverlay"),
  progress: document.querySelector("#progressValue"),
  progressDetail: document.querySelector("#progressDetail"),
  deviceName: document.querySelector("#deviceName"),
  imageInfo: document.querySelector("#imageInfo"),
  transferInfo: document.querySelector("#transferInfo"),
  timeInfo: document.querySelector("#timeInfo"),
};

let device;
let linkCharacteristic;
let serialCharacteristic;
let preparedImage;
let serialAssembly;
let uploadInProgress = false;
const serialMessages = [];
const serialWaiters = [];
const controlWaiters = new Map();
const encoder = new TextEncoder();
const decoder = new TextDecoder();
let nextControlRequestId = 1;

function setTransferInfo(text) { elements.transferInfo.textContent = text; }
function isConnected() { return Boolean(device?.gatt?.connected && linkCharacteristic && serialCharacteristic); }

async function getRequiredService(server, uuid) {
  const services = await server.getPrimaryServices();
  const service = services.find((candidate) => candidate.uuid === uuid);
  if (service) return service;
  const available = services.map((candidate) => candidate.uuid).join(", ");
  throw new Error(`未找到服务 ${uuid}；已发现：${available || "无"}`);
}

function updateControls() {
  const connected = isConnected();
  elements.dot.classList.toggle("connected", connected);
  elements.send.disabled = !connected || !preparedImage || uploadInProgress;
  elements.refresh.disabled = !connected || uploadInProgress;
  elements.cancel.disabled = !connected;
  elements.clear.disabled = !connected || uploadInProgress;
  elements.syncTime.disabled = !connected || uploadInProgress;
}

function setProgress(percent, detail) {
  elements.progress.textContent = `${Math.round(percent)}%`;
  elements.progressDetail.textContent = detail;
}

function showTransfer(visible) { elements.overlay.hidden = !visible; }

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

function rejectControlWaiters(error) {
  for (const waiter of controlWaiters.values()) waiter.reject(error);
  controlWaiters.clear();
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
  } else if (serialCharacteristic.writeValueWithResponse) {
    await serialCharacteristic.writeValueWithResponse(packet);
  } else {
    await serialCharacteristic.writeValue(packet);
  }
}

async function sendSerial(payload) {
  for (const frame of serialCarrierFrames(payload)) await writeSerialPacket(frame);
}

async function sendSerialRequest(payload, responseId) {
  const response = waitForWatchface(responseId);
  try {
    await sendSerial(payload);
  } catch (error) {
    const index = serialWaiters.findIndex((waiter) => waiter.id === responseId);
    if (index >= 0) serialWaiters.splice(index, 1)[0].reject(error);
    return response;
  }
  return response;
}

function responseStatus(message) {
  let status;
  try {
    status = watchfaceResponseStatus(message);
  } catch {
    throw new Error("手表响应格式错误");
  }
  if (status !== 0) throw new Error(`手表拒绝传输，错误码 ${status}`);
}

async function sendCommand(command) {
  const payload = encoder.encode(command);
  if (linkCharacteristic.writeValueWithResponse) {
    await linkCharacteristic.writeValueWithResponse(payload);
  } else {
    await linkCharacteristic.writeValue(payload);
  }
}

function onLinkNotification(event) {
  const text = decoder.decode(event.target.value);
  const control = parseControlResponse(text);
  if (control) {
    const waiter = controlWaiters.get(control.requestId);
    if (waiter) {
      controlWaiters.delete(control.requestId);
      if (control.status === "OK") waiter.resolve(control.payload);
      else waiter.reject(new Error(`手表协议错误 ${control.payload || "unknown"}`));
    }
    return;
  }
  if (text.startsWith("badge:")) setTransferInfo(text);
}

function waitForControl(requestId, timeout = 6000) {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      controlWaiters.delete(requestId);
      reject(new Error("手表控制响应超时"));
    }, timeout);
    controlWaiters.set(requestId, {
      resolve: (payload) => { clearTimeout(timer); resolve(payload); },
      reject: (error) => { clearTimeout(timer); reject(error); },
    });
  });
}

async function sendControl(operation, payload = "") {
  const requestId = nextControlRequestId;
  nextControlRequestId = nextControlRequestId % 65535 + 1;
  const response = waitForControl(requestId);
  try {
    await sendCommand(controlRequest(requestId, operation, payload));
  } catch (error) {
    const waiter = controlWaiters.get(requestId);
    controlWaiters.delete(requestId);
    waiter?.reject(error);
    throw error;
  }
  return response;
}

function parseStatePayload(payload) {
  return Object.fromEntries(payload.split(";").map((part) => part.split("=", 2)).filter(([key, value]) => key && value !== undefined));
}

function formatWatchTime(value, timezoneOffsetMinutes) {
  const match = /^(\d{4})(\d{2})(\d{2})T(\d{2})(\d{2})(\d{2})$/.exec(value || "");
  const offset = Number(timezoneOffsetMinutes);
  const timezone = Number.isFinite(offset) ? ` UTC${offset >= 0 ? "+" : ""}${offset / 60}` : "";
  return match ? `${match[1]}-${match[2]}-${match[3]} ${match[4]}:${match[5]}:${match[6]}${timezone}` : "等待同步";
}

function displayWatchState(payload) {
  const state = parseStatePayload(payload);
  elements.timeInfo.textContent = formatWatchTime(state.time, state.tz);
  return state;
}

async function syncTime() {
  const epochSeconds = Math.floor(Date.now() / 1000);
  const timezoneOffsetMinutes = -new Date().getTimezoneOffset();
  setTransferInfo("正在同步时间");
  const payload = await sendControl("TIME", `${epochSeconds},${timezoneOffsetMinutes}`);
  displayWatchState(payload);
  setTransferInfo("时间已同步");
}

async function refreshWatchState() {
  setTransferInfo("正在读取手表状态");
  const statePayload = await sendControl("STATE");
  const badgePayload = await sendControl("BADGE", "STATUS");
  const state = displayWatchState(statePayload);
  const badge = parseStatePayload(badgePayload);
  const image = state.img === "1" ? "已保存" : "未保存";
  setTransferInfo(`图片${image}，传输状态 ${badge.s ?? "未知"}`);
}

async function runBadgeAction(action, successText) {
  setTransferInfo("正在处理图片");
  await sendControl("BADGE", action);
  setTransferInfo(successText);
  await refreshWatchState();
}

async function connect() {
  if (!navigator.bluetooth) throw new Error("当前浏览器不支持 Web Bluetooth");
  setTransferInfo("请选择 Huangshan 手表");
  device = await navigator.bluetooth.requestDevice({
    filters: [{ namePrefix: "Huangshan-Watch" }],
    optionalServices: [LINK_SERVICE, SERIAL_SERVICE],
  });
  setTransferInfo("正在连接手表");
  device.addEventListener("gattserverdisconnected", () => {
    rejectSerialWaiters(new Error("手表已断开连接"));
    rejectControlWaiters(new Error("手表已断开连接"));
    linkCharacteristic = undefined;
    serialCharacteristic = undefined;
    elements.deviceName.textContent = "已断开";
    setTransferInfo("等待连接");
    updateControls();
  });
  const server = await device.gatt.connect();
  setTransferInfo("正在读取手表服务");
  const linkService = await getRequiredService(server, LINK_SERVICE);
  linkCharacteristic = await linkService.getCharacteristic(LINK_CHARACTERISTIC);
  await linkCharacteristic.startNotifications();
  linkCharacteristic.addEventListener("characteristicvaluechanged", onLinkNotification);
  const serialService = await getRequiredService(server, SERIAL_SERVICE);
  serialCharacteristic = await serialService.getCharacteristic(SERIAL_DATA);
  await serialCharacteristic.startNotifications();
  serialCharacteristic.addEventListener("characteristicvaluechanged", onSerialNotification);
  elements.deviceName.textContent = device.name || "Huangshan Watch";
  setTransferInfo("已连接");
  updateControls();
  const hello = await sendControl("HELLO");
  setTransferInfo(`已连接 ${hello}`);
  await syncTime();
  await refreshWatchState();
}

async function canvasBlob(canvas, quality) {
  return new Promise((resolve) => canvas.toBlob(resolve, "image/jpeg", quality));
}

async function prepareImage(file) {
  if (!["image/jpeg", "image/png", "image/webp"].includes(file.type)) {
    throw new Error("网页端请使用 JPG、PNG 或 WebP 图片；HEIC 请使用 iOS 原生客户端");
  }
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
  const upload = makeUploadPayload(preparedImage);
  const fileName = encoder.encode("badge.jpg");
  uploadInProgress = true;
  updateControls();
  showTransfer(true);
  try {
    setProgress(0, "建立传输");
    responseStatus(await sendSerialRequest(watchfaceMessage(0, joinBytes(u16(WATCHFACE_TYPE_BACKGROUND), new Uint8Array([PHONE_TYPE_ANDROID]), u32(upload.length))), 1));
    responseStatus(await sendSerialRequest(watchfaceMessage(2, joinBytes(u32(upload.length), u16(fileName.length), fileName)), 3));
    let index = 0;
    for (let offset = 0; offset < upload.length; offset += CHUNK_SIZE) {
      const part = upload.slice(offset, offset + CHUNK_SIZE);
      responseStatus(await sendSerialRequest(watchfaceMessage(4, joinBytes(u32(index), part)), 5));
      index += 1;
      setProgress(((offset + part.length) / upload.length) * 100, `正在发送 ${Math.round((offset + part.length) / 1024)} KB`);
    }
    responseStatus(await sendSerialRequest(watchfaceMessage(6), 7));
    responseStatus(await sendSerialRequest(watchfaceMessage(8), 9));
    setProgress(100, "已保存到手表");
    await refreshWatchState();
  } finally {
    uploadInProgress = false;
    updateControls();
    setTimeout(() => showTransfer(false), 900);
  }
}

elements.connect.addEventListener("click", () => connect().catch((error) => { setTransferInfo(error.message); updateControls(); }));
elements.imageInput.addEventListener("change", (event) => {
  const [file] = event.target.files;
  if (file) prepareImage(file).catch((error) => {
    preparedImage = undefined;
    elements.imageInfo.textContent = "尚未选择";
    updateControls();
    setTransferInfo(error.message);
  });
});
elements.send.addEventListener("click", () => uploadImage().catch((error) => { setTransferInfo(error.message); showTransfer(false); }));
elements.refresh.addEventListener("click", () => refreshWatchState().catch((error) => { setTransferInfo(error.message); }));
elements.cancel.addEventListener("click", () => runBadgeAction("CANCEL", "已取消传输").catch((error) => { setTransferInfo(error.message); }));
elements.clear.addEventListener("click", () => runBadgeAction("CLEAR", "已清除图片").catch((error) => { setTransferInfo(error.message); }));
elements.syncTime.addEventListener("click", () => syncTime().catch((error) => { setTransferInfo(error.message); }));
updateControls();

if ("serviceWorker" in navigator) {
  window.addEventListener("load", () => navigator.serviceWorker.register("./sw.js"));
}
