package com.huangshan.badge;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanResult;
import android.content.Context;
import android.os.Handler;
import android.os.Looper;

import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.time.ZoneId;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

final class WatchBleClient {
    interface Listener {
        void onDevices(List<Device> devices);
        void onConnection(String text, boolean connected);
        void onTransfer(String text);
        void onTime(String text);
        void onProgress(int percent);
        void onError(String text);
    }

    static final class Device {
        final BluetoothDevice bluetoothDevice;
        final String name;

        Device(BluetoothDevice bluetoothDevice, String name) {
            this.bluetoothDevice = bluetoothDevice;
            this.name = name;
        }
    }

    private static final UUID CCCD_UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");
    private final Context context;
    private final Listener listener;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private final Map<String, Device> devices = new LinkedHashMap<>();
    private final Map<Integer, CompletableFuture<String>> controlWaiters = new ConcurrentHashMap<>();
    private final Map<Integer, CompletableFuture<byte[]>> serialWaiters = new ConcurrentHashMap<>();
    private final AtomicInteger nextControlId = new AtomicInteger(1);
    private final Object writeLock = new Object();

    private BluetoothAdapter adapter;
    private BluetoothLeScanner scanner;
    private BluetoothGatt gatt;
    private BluetoothGattCharacteristic linkCharacteristic;
    private BluetoothGattCharacteristic serialCharacteristic;
    private CompletableFuture<Boolean> writeWaiter;
    private ByteArrayOutputStream serialAssembly;
    private int serialExpectedLength;
    private boolean linkNotificationsEnabled;
    private boolean serialNotificationsEnabled;
    private boolean serviceDiscoveryRequested;
    private boolean scanning;
    private volatile boolean ready;

    WatchBleClient(Context context, Listener listener) {
        this.context = context.getApplicationContext();
        this.listener = listener;
        BluetoothManager manager = context.getSystemService(BluetoothManager.class);
        adapter = manager == null ? null : manager.getAdapter();
    }

    boolean isBluetoothEnabled() {
        return adapter != null && adapter.isEnabled();
    }

    boolean isReady() {
        return ready && gatt != null && linkCharacteristic != null && serialCharacteristic != null;
    }

    void startScan() {
        if (scanning) return;
        if (!isBluetoothEnabled()) {
            reportError("请先开启手机蓝牙");
            return;
        }
        scanner = adapter.getBluetoothLeScanner();
        if (scanner == null) {
            reportError("无法启动蓝牙扫描");
            return;
        }
        synchronized (devices) { devices.clear(); }
        publishDevices();
        scanner.startScan(scanCallback);
        scanning = true;
        postTransfer("正在搜索 Huangshan-Watch-BLE");
    }

    void stopScan() {
        if (scanner != null && scanning) scanner.stopScan(scanCallback);
        scanning = false;
    }

    void connect(Device device) {
        stopScan();
        disconnect();
        postConnection("正在连接 " + device.name, false);
        gatt = device.bluetoothDevice.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE);
    }

    void disconnect() {
        ready = false;
        serviceDiscoveryRequested = false;
        BluetoothGatt current = gatt;
        gatt = null;
        if (current != null) {
            current.disconnect();
            current.close();
        }
        resetWaiters(new IllegalStateException("手表已断开连接"));
    }

    void synchronizeTime() {
        runWorker(() -> {
            long epochSeconds = System.currentTimeMillis() / 1000L;
            int timezoneMinutes = ZoneId.systemDefault().getRules().getOffset(java.time.Instant.now()).getTotalSeconds() / 60;
            String state = sendControl("TIME", epochSeconds + "," + timezoneMinutes);
            publishState(state);
            postTransfer("时间已同步");
        });
    }

    void refreshState() {
        runWorker(() -> {
            String state = sendControl("STATE", "");
            publishState(state);
            postTransfer(formatBadgeStatus(sendControl("BADGE", "STATUS")));
        });
    }

    void cancelTransfer() {
        runWorker(() -> postTransfer(sendControl("BADGE", "CANCEL")));
    }

    void clearBadge() {
        runWorker(() -> postTransfer(sendControl("BADGE", "CLEAR")));
    }

    void uploadImage(byte[] jpeg) {
        runWorker(() -> {
            if (!isReady()) throw new IllegalStateException("请先连接手表");
            byte[] upload = WatchProtocol.makeUpload(jpeg);
            postTransfer("建立图片传输");
            requestWatchface(WatchProtocol.watchfaceMessage(0,
                    WatchProtocol.join(WatchProtocol.u16(WatchProtocol.BACKGROUND_FILE_TYPE),
                            new byte[] { WatchProtocol.PHONE_TYPE_ANDROID }, WatchProtocol.u32(upload.length))), 1);
            byte[] fileName = "badge.jpg".getBytes(StandardCharsets.UTF_8);
            requestWatchface(WatchProtocol.watchfaceMessage(2,
                    WatchProtocol.join(WatchProtocol.u32(upload.length), WatchProtocol.u16(fileName.length), fileName)), 3);

            int index = 0;
            for (int offset = 0; offset < upload.length; offset += WatchProtocol.CHUNK_SIZE) {
                int end = Math.min(offset + WatchProtocol.CHUNK_SIZE, upload.length);
                byte[] data = new byte[end - offset];
                System.arraycopy(upload, offset, data, 0, data.length);
                requestWatchface(WatchProtocol.watchfaceMessage(4,
                        WatchProtocol.join(WatchProtocol.u32(index++), data)), 5);
                postProgress((int) ((long) end * 100 / upload.length));
                postTransfer("正在发送 " + (end / 1024) + " KB");
            }
            requestWatchface(WatchProtocol.watchfaceMessage(6, new byte[0]), 7);
            requestWatchface(WatchProtocol.watchfaceMessage(8, new byte[0]), 9);
            postProgress(100);
            postTransfer("图片已保存到手表");
        });
    }

    void close() {
        disconnect();
        worker.shutdownNow();
    }

    private final ScanCallback scanCallback = new ScanCallback() {
        @Override public void onScanResult(int callbackType, ScanResult result) {
            BluetoothDevice device = result.getDevice();
            String name = device.getName();
            if (name == null && result.getScanRecord() != null) name = result.getScanRecord().getDeviceName();
            if (name == null || !name.startsWith("Huangshan-Watch")) return;
            synchronized (devices) {
                devices.put(device.getAddress(), new Device(device, name));
            }
            publishDevices();
        }

        @Override public void onScanFailed(int errorCode) {
            scanning = false;
            reportError("蓝牙扫描失败，错误码 " + errorCode);
        }
    };

    private final BluetoothGattCallback gattCallback = new BluetoothGattCallback() {
        @Override public void onConnectionStateChange(BluetoothGatt callbackGatt, int status, int newState) {
            if (status == BluetoothGatt.GATT_SUCCESS && newState == BluetoothProfile.STATE_CONNECTED) {
                postConnection("已连接，正在发现服务", false);
                serviceDiscoveryRequested = false;
                if (!callbackGatt.requestMtu(247)) discoverServices(callbackGatt);
                return;
            }
            ready = false;
            if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                postConnection("已断开", false);
                resetWaiters(new IllegalStateException("手表已断开连接"));
            } else {
                reportError("蓝牙连接失败，错误码 " + status);
            }
        }

        @Override public void onMtuChanged(BluetoothGatt callbackGatt, int mtu, int status) {
            discoverServices(callbackGatt);
        }

        @Override public void onServicesDiscovered(BluetoothGatt callbackGatt, int status) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                reportError("手表服务发现失败，错误码 " + status);
                return;
            }
            android.bluetooth.BluetoothGattService linkService = callbackGatt.getService(WatchProtocol.LINK_SERVICE_UUID);
            android.bluetooth.BluetoothGattService serialService = callbackGatt.getService(WatchProtocol.SERIAL_SERVICE_UUID);
            linkCharacteristic = linkService == null ? null : linkService.getCharacteristic(WatchProtocol.LINK_CHARACTERISTIC_UUID);
            serialCharacteristic = serialService == null ? null : serialService.getCharacteristic(WatchProtocol.SERIAL_DATA_UUID);
            if (linkCharacteristic == null || serialCharacteristic == null) {
                reportError("未找到黄山手表控制服务，请确认固件版本");
                return;
            }
            enableNotifications(linkCharacteristic);
        }

        @Override public void onDescriptorWrite(BluetoothGatt callbackGatt, BluetoothGattDescriptor descriptor, int status) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                reportError("无法订阅手表通知，错误码 " + status);
                return;
            }
            if (descriptor.getCharacteristic().getUuid().equals(WatchProtocol.LINK_CHARACTERISTIC_UUID)) {
                linkNotificationsEnabled = true;
                enableNotifications(serialCharacteristic);
            } else if (descriptor.getCharacteristic().getUuid().equals(WatchProtocol.SERIAL_DATA_UUID)) {
                serialNotificationsEnabled = true;
                markReady();
            }
        }

        @Override public void onCharacteristicChanged(BluetoothGatt callbackGatt, BluetoothGattCharacteristic characteristic) {
            byte[] value = characteristic.getValue();
            if (value == null) return;
            if (characteristic.getUuid().equals(WatchProtocol.LINK_CHARACTERISTIC_UUID)) {
                handleControlResponse(new String(value, StandardCharsets.UTF_8));
            } else if (characteristic.getUuid().equals(WatchProtocol.SERIAL_DATA_UUID)) {
                handleSerial(value);
            }
        }

        @Override public void onCharacteristicWrite(BluetoothGatt callbackGatt, BluetoothGattCharacteristic characteristic, int status) {
            synchronized (writeLock) {
                if (writeWaiter != null) {
                    writeWaiter.complete(status == BluetoothGatt.GATT_SUCCESS);
                    writeWaiter = null;
                }
            }
        }
    };

    private void discoverServices(BluetoothGatt callbackGatt) {
        if (serviceDiscoveryRequested) return;
        serviceDiscoveryRequested = true;
        if (!callbackGatt.discoverServices()) reportError("无法开始手表服务发现");
    }

    private void enableNotifications(BluetoothGattCharacteristic characteristic) {
        BluetoothGatt current = gatt;
        if (current == null || !current.setCharacteristicNotification(characteristic, true)) {
            reportError("无法开启手表通知");
            return;
        }
        BluetoothGattDescriptor descriptor = characteristic.getDescriptor(CCCD_UUID);
        if (descriptor == null) {
            reportError("手表通知描述符不存在");
            return;
        }
        descriptor.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
        if (!current.writeDescriptor(descriptor)) reportError("无法写入手表通知设置");
    }

    private void markReady() {
        if (!linkNotificationsEnabled || !serialNotificationsEnabled || ready) return;
        ready = true;
        String name = gatt != null && gatt.getDevice() != null && gatt.getDevice().getName() != null
                ? gatt.getDevice().getName() : "Huangshan Watch";
        postConnection(name, true);
        runWorker(() -> {
            postTransfer("正在初始化手表连接");
            sendControl("HELLO", "");
            synchronizeTimeInternal();
            refreshStateInternal();
        });
    }

    private void synchronizeTimeInternal() throws Exception {
        long epochSeconds = System.currentTimeMillis() / 1000L;
        int timezoneMinutes = ZoneId.systemDefault().getRules().getOffset(java.time.Instant.now()).getTotalSeconds() / 60;
        publishState(sendControl("TIME", epochSeconds + "," + timezoneMinutes));
        postTransfer("时间已同步");
    }

    private void refreshStateInternal() throws Exception {
        publishState(sendControl("STATE", ""));
        postTransfer(formatBadgeStatus(sendControl("BADGE", "STATUS")));
    }

    private String sendControl(String operation, String payload) throws Exception {
        if (!isReady()) throw new IllegalStateException("请先连接手表");
        int requestId = nextControlId.getAndUpdate(value -> value >= 65535 ? 1 : value + 1);
        CompletableFuture<String> response = new CompletableFuture<>();
        controlWaiters.put(requestId, response);
        String suffix = payload.isEmpty() ? "" : "|" + payload;
        try {
            write(linkCharacteristic, ("HWS1|" + requestId + "|" + operation + suffix).getBytes(StandardCharsets.UTF_8));
            return response.get(6, TimeUnit.SECONDS);
        } finally {
            controlWaiters.remove(requestId);
        }
    }

    private void requestWatchface(byte[] payload, int responseId) throws Exception {
        CompletableFuture<byte[]> response = new CompletableFuture<>();
        serialWaiters.put(responseId, response);
        try {
            for (byte[] frame : WatchProtocol.serialFrames(payload)) write(serialCharacteristic, frame);
            byte[] message = response.get(8, TimeUnit.SECONDS);
            if (message.length < 4) throw new IllegalStateException("手表响应格式错误");
            int status = WatchProtocol.readU16(message, 2);
            if (status != 0) throw new IllegalStateException("手表拒绝传输，错误码 " + status);
        } finally {
            serialWaiters.remove(responseId);
        }
    }

    private void write(BluetoothGattCharacteristic characteristic, byte[] value) throws Exception {
        BluetoothGatt current = gatt;
        if (current == null) throw new IllegalStateException("手表已断开连接");
        boolean withResponse = (characteristic.getProperties() & BluetoothGattCharacteristic.PROPERTY_WRITE) != 0;
        CompletableFuture<Boolean> response = withResponse ? new CompletableFuture<>() : null;
        synchronized (writeLock) {
            characteristic.setWriteType(withResponse ? BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
                    : BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE);
            characteristic.setValue(value);
            writeWaiter = response;
            if (!current.writeCharacteristic(characteristic)) {
                writeWaiter = null;
                throw new IllegalStateException("蓝牙写入未被手表接受");
            }
        }
        if (response != null && !response.get(6, TimeUnit.SECONDS)) {
            throw new IllegalStateException("蓝牙写入失败");
        }
        if (!withResponse) Thread.sleep(8);
    }

    private void handleControlResponse(String response) {
        String[] fields = response.split("\\|", -1);
        if (fields.length < 3 || !"HWS1".equals(fields[0])) {
            if (response.startsWith("badge:")) postTransfer(response);
            return;
        }
        try {
            int requestId = Integer.parseInt(fields[1]);
            CompletableFuture<String> waiter = controlWaiters.get(requestId);
            if (waiter == null) return;
            String payload = joinFields(fields, 3);
            if ("OK".equals(fields[2])) waiter.complete(payload);
            else waiter.completeExceptionally(new IllegalStateException("手表协议错误 " + payload));
        } catch (NumberFormatException ignored) { }
    }

    private void handleSerial(byte[] value) {
        if (value.length < 2 || (value[0] & 0xff) != WatchProtocol.SERIAL_CATEGORY_WATCHFACE) return;
        int flag = value[1] & 0xff;
        if (flag == 0 && value.length >= 4) {
            handleWatchface(slice(value, 4));
        } else if (flag == 1 && value.length >= 4) {
            serialExpectedLength = WatchProtocol.readU16(value, 2);
            serialAssembly = new ByteArrayOutputStream(serialExpectedLength);
            serialAssembly.write(value, 4, value.length - 4);
        } else if (serialAssembly != null && (flag == 2 || flag == 3)) {
            serialAssembly.write(value, 2, value.length - 2);
            if (flag == 3) {
                byte[] assembled = serialAssembly.toByteArray();
                serialAssembly = null;
                if (assembled.length > serialExpectedLength) assembled = slice(assembled, 0, serialExpectedLength);
                handleWatchface(assembled);
            }
        }
    }

    private void handleWatchface(byte[] message) {
        if (message.length < 4) return;
        CompletableFuture<byte[]> waiter = serialWaiters.get(WatchProtocol.readU16(message, 0));
        if (waiter != null) waiter.complete(message);
    }

    private void publishState(String payload) {
        String[] pieces = payload.split(";");
        for (String piece : pieces) {
            if (piece.startsWith("time=")) {
                String value = piece.substring("time=".length());
                if (value.length() == 15) {
                    postTime(value.substring(0, 4) + "-" + value.substring(4, 6) + "-" + value.substring(6, 8)
                            + " " + value.substring(9, 11) + ":" + value.substring(11, 13) + ":" + value.substring(13, 15));
                    return;
                }
            }
        }
        postTime(payload.isEmpty() ? "等待同步" : payload);
    }

    private static String formatBadgeStatus(String payload) {
        Map<String, String> values = new LinkedHashMap<>();
        for (String field : payload.split(";")) {
            String[] pair = field.split("=", 2);
            if (pair.length == 2) values.put(pair[0], pair[1]);
        }
        if (!values.containsKey("i")) return payload;
        if (!"0".equals(values.getOrDefault("e", "0"))) {
            return "传输错误，代码 " + values.get("e");
        }
        String bytes = values.getOrDefault("r", "0") + " / " + values.getOrDefault("t", "0") + " B";
        if ("1".equals(values.get("i"))) return "图片已保存（" + bytes + "）";
        if ("1".equals(values.get("s"))) return "正在接收图片（" + bytes + "）";
        return "尚未保存图片";
    }

    private void runWorker(ThrowingRunnable work) {
        worker.execute(() -> {
            try {
                work.run();
            } catch (Exception error) {
                reportError(error.getMessage() == null ? "蓝牙操作失败" : error.getMessage());
            }
        });
    }

    private void resetWaiters(Exception error) {
        controlWaiters.values().forEach(waiter -> waiter.completeExceptionally(error));
        serialWaiters.values().forEach(waiter -> waiter.completeExceptionally(error));
        controlWaiters.clear();
        serialWaiters.clear();
    }

    private void publishDevices() {
        List<Device> snapshot;
        synchronized (devices) { snapshot = new ArrayList<>(devices.values()); }
        main.post(() -> listener.onDevices(snapshot));
    }

    private void postConnection(String text, boolean connected) { main.post(() -> listener.onConnection(text, connected)); }
    private void postTransfer(String text) { main.post(() -> listener.onTransfer(text)); }
    private void postTime(String text) { main.post(() -> listener.onTime(text)); }
    private void postProgress(int percent) { main.post(() -> listener.onProgress(percent)); }
    private void reportError(String text) { main.post(() -> listener.onError(text)); }

    private static String joinFields(String[] fields, int start) {
        StringBuilder value = new StringBuilder();
        for (int index = start; index < fields.length; index++) {
            if (index > start) value.append('|');
            value.append(fields[index]);
        }
        return value.toString();
    }

    private static byte[] slice(byte[] source, int start) { return slice(source, start, source.length); }
    private static byte[] slice(byte[] source, int start, int end) {
        byte[] result = new byte[end - start];
        System.arraycopy(source, start, result, 0, result.length);
        return result;
    }

    private interface ThrowingRunnable { void run() throws Exception; }
}
