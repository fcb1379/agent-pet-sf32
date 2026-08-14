package com.huangshan.badge;

import static com.sifli.siflidfu.SifliDFUService.BROADCAST_DFU_LOG;
import static com.sifli.siflidfu.SifliDFUService.BROADCAST_DFU_PROGRESS;
import static com.sifli.siflidfu.SifliDFUService.BROADCAST_DFU_STATE;
import static com.sifli.siflidfu.SifliDFUService.EXTRA_DFU_PROGRESS;
import static com.sifli.siflidfu.SifliDFUService.EXTRA_DFU_PROGRESS_TYPE;
import static com.sifli.siflidfu.SifliDFUService.EXTRA_DFU_STATE;
import static com.sifli.siflidfu.SifliDFUService.EXTRA_DFU_STATE_RESULT;
import static com.sifli.siflidfu.SifliDFUService.EXTRA_LOG_MESSAGE;

import android.bluetooth.BluetoothAdapter;
import android.content.BroadcastReceiver;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.ServiceConnection;
import android.database.Cursor;
import android.net.Uri;
import android.os.IBinder;
import android.provider.OpenableColumns;

import androidx.localbroadcastmanager.content.LocalBroadcastManager;

import com.sifli.siflidfu.ISifliDFUService;
import com.sifli.siflidfu.SifliDFUService;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

final class SifliOtaClient {
    private static final String OFFLINE_PACKAGE_NAME = "offline_install.bin";
    private static final long MAX_OFFLINE_PACKAGE_SIZE = 0x00120000L;

    interface Listener {
        void onOtaPackagePrepared(String name, long bytes);
        void onOtaProgress(int percent, int stage);
        void onOtaState(String text);
        void onOtaError(String text);
    }

    private final Context context;
    private final Listener listener;
    private final LocalBroadcastManager broadcastManager;
    private ISifliDFUService dfuService;
    private File packageFile;
    private String pendingAddress;
    private boolean bindRequested;
    private boolean busy;
    private int lastProgress;

    SifliOtaClient(Context context, Listener listener) {
        this.context = context.getApplicationContext();
        this.listener = listener;
        this.broadcastManager = LocalBroadcastManager.getInstance(this.context);

        IntentFilter filter = new IntentFilter();
        filter.addAction(BROADCAST_DFU_LOG);
        filter.addAction(BROADCAST_DFU_PROGRESS);
        filter.addAction(BROADCAST_DFU_STATE);
        broadcastManager.registerReceiver(dfuReceiver, filter);

        Intent serviceIntent = new Intent(this.context, SifliDFUService.class);
        bindRequested = this.context.bindService(serviceIntent, serviceConnection, Context.BIND_AUTO_CREATE);
        if (!bindRequested) {
            listener.onOtaError("无法启动 SiFli OTA 服务");
        }
    }

    boolean hasPackage() {
        return packageFile != null && packageFile.isFile() && packageFile.length() > 0L;
    }

    boolean isBusy() {
        return busy;
    }

    void importPackage(Uri uri) throws IOException {
        if (uri == null) throw new IOException("未选择升级包");

        String displayName = queryDisplayName(uri);
        if (!OFFLINE_PACKAGE_NAME.equalsIgnoreCase(displayName)) {
            throw new IOException("请选择由 SiFli 工具生成的 offline_install.bin");
        }

        File directory = new File(context.getFilesDir(), "ota");
        if (!directory.isDirectory() && !directory.mkdirs()) {
            throw new IOException("无法创建 OTA 缓存目录");
        }

        File partial = new File(directory, OFFLINE_PACKAGE_NAME + ".part");
        File target = new File(directory, OFFLINE_PACKAGE_NAME);
        if (partial.exists() && !partial.delete()) {
            throw new IOException("无法清理旧的 OTA 临时文件");
        }

        long total = 0L;
        byte[] buffer = new byte[8192];
        try (InputStream input = context.getContentResolver().openInputStream(uri);
             FileOutputStream output = new FileOutputStream(partial)) {
            if (input == null) throw new IOException("无法读取升级包");
            int count;
            while ((count = input.read(buffer)) >= 0) {
                if (0 == count) continue;
                total += count;
                if (MAX_OFFLINE_PACKAGE_SIZE < total) {
                    throw new IOException("升级包超过设备 DFU 下载区容量 1152 KiB");
                }
                output.write(buffer, 0, count);
            }
            output.getFD().sync();
        } catch (IOException error) {
            partial.delete();
            throw error;
        }

        if (0L == total) {
            partial.delete();
            throw new IOException("升级包为空");
        }
        if (target.exists() && !target.delete()) {
            partial.delete();
            throw new IOException("无法替换旧的升级包");
        }
        if (!partial.renameTo(target)) {
            partial.delete();
            throw new IOException("无法保存升级包");
        }

        packageFile = target;
        listener.onOtaPackagePrepared(displayName, total);
    }

    void start(String bluetoothAddress) {
        if (busy) {
            listener.onOtaError("OTA 正在进行中");
            return;
        }
        if (!hasPackage()) {
            listener.onOtaError("请先选择 offline_install.bin");
            return;
        }
        if (!BluetoothAdapter.checkBluetoothAddress(bluetoothAddress)) {
            listener.onOtaError("无效的手表蓝牙地址");
            return;
        }
        if (!bindRequested) {
            listener.onOtaError("SiFli OTA 服务尚未就绪");
            return;
        }

        busy = true;
        lastProgress = 0;
        pendingAddress = bluetoothAddress;
        listener.onOtaState("正在切换到 OTA 连接");
        startPendingRequest();
    }

    void close() {
        pendingAddress = null;
        busy = false;
        broadcastManager.unregisterReceiver(dfuReceiver);
        if (bindRequested) {
            context.unbindService(serviceConnection);
            bindRequested = false;
        }
        dfuService = null;
    }

    private void startPendingRequest() {
        if (pendingAddress == null || dfuService == null) return;

        String address = pendingAddress;
        pendingAddress = null;
        try {
            dfuService.startActionDFUNorOffline(context, address, null, packageFile.getAbsolutePath());
            listener.onOtaState("正在传输升级包，请勿退出 App 或关闭设备");
        } catch (RuntimeException error) {
            busy = false;
            listener.onOtaError(error.getMessage() == null ? "无法启动 OTA" : error.getMessage());
        }
    }

    private String queryDisplayName(Uri uri) {
        try (Cursor cursor = context.getContentResolver().query(uri,
                new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                int index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (0 <= index) return cursor.getString(index);
            }
        }
        String lastSegment = uri.getLastPathSegment();
        return lastSegment == null ? "" : lastSegment;
    }

    private final ServiceConnection serviceConnection = new ServiceConnection() {
        @Override public void onServiceConnected(ComponentName name, IBinder service) {
            SifliDFUService.SifliDFUBinder binder = (SifliDFUService.SifliDFUBinder) service;
            dfuService = binder.getDfuService();
            startPendingRequest();
        }

        @Override public void onServiceDisconnected(ComponentName name) {
            dfuService = null;
            pendingAddress = null;
            if (busy && 100 > lastProgress) {
                busy = false;
                listener.onOtaError("SiFli OTA 服务意外断开");
            }
        }
    };

    private final BroadcastReceiver dfuReceiver = new BroadcastReceiver() {
        @Override public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            if (BROADCAST_DFU_PROGRESS.equals(action)) {
                int progress = intent.getIntExtra(EXTRA_DFU_PROGRESS, 0);
                int stage = intent.getIntExtra(EXTRA_DFU_PROGRESS_TYPE, 0);
                lastProgress = Math.max(0, Math.min(100, progress));
                listener.onOtaProgress(lastProgress, stage);
                if (100 <= lastProgress) {
                    busy = false;
                    listener.onOtaState("升级包传输完成，等待设备安装并重启");
                }
            } else if (BROADCAST_DFU_STATE.equals(action)) {
                int state = intent.getIntExtra(EXTRA_DFU_STATE, 0);
                int result = intent.getIntExtra(EXTRA_DFU_STATE_RESULT, 0);
                if (0 != result) {
                    busy = false;
                    listener.onOtaError("OTA 失败：状态 " + state + "，错误码 " + result);
                } else {
                    listener.onOtaState("OTA 状态 " + state);
                }
            } else if (BROADCAST_DFU_LOG.equals(action)) {
                String message = intent.getStringExtra(EXTRA_LOG_MESSAGE);
                if (message != null && !message.isEmpty()) listener.onOtaState(message);
            }
        }
    };
}
