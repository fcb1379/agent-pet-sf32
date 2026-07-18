package com.huangshan.badge;

import android.Manifest;
import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.util.List;

public final class MainActivity extends Activity implements WatchBleClient.Listener {
    private static final int REQUEST_BLUETOOTH = 100;
    private static final int REQUEST_IMAGE = 101;

    private WatchBleClient watch;
    private ImageView preview;
    private TextView deviceValue;
    private TextView imageValue;
    private TextView transferValue;
    private TextView timeValue;
    private TextView progressValue;
    private ProgressBar progress;
    private LinearLayout deviceList;
    private Button sendButton;
    private Button syncButton;
    private Button refreshButton;
    private Button cancelButton;
    private Button clearButton;
    private byte[] preparedJpeg;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        watch = new WatchBleClient(this, this);
        setContentView(buildContent());
        updateControls();
    }

    @Override protected void onDestroy() {
        watch.close();
        super.onDestroy();
    }

    private View buildContent() {
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(20), dp(18), dp(20), dp(28));
        root.setBackgroundColor(color("app_background"));
        scroll.addView(root, new ScrollView.LayoutParams(-1, -1));

        TextView title = text("电子吧唧", 28, color("app_text"));
        title.setGravity(Gravity.CENTER_HORIZONTAL);
        root.addView(title, params(-1, -2, 0));

        TextView subtitle = text("黄山手表", 14, color("app_muted"));
        subtitle.setGravity(Gravity.CENTER_HORIZONTAL);
        root.addView(subtitle, params(-1, -2, 14));

        preview = new ImageView(this);
        preview.setBackgroundColor(Color.BLACK);
        preview.setScaleType(ImageView.ScaleType.CENTER_CROP);
        preview.setImageResource(android.R.drawable.ic_menu_gallery);
        LinearLayout.LayoutParams previewParams = params(-1, dp(320), 20);
        previewParams.gravity = Gravity.CENTER_HORIZONTAL;
        root.addView(preview, previewParams);

        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setMax(100);
        root.addView(progress, params(-1, dp(5), 14));
        progressValue = text("0%", 13, color("app_muted"));
        progressValue.setGravity(Gravity.END);
        root.addView(progressValue, params(-1, -2, 4));

        Button scanButton = button("扫描手表");
        scanButton.setOnClickListener(view -> beginScan());
        root.addView(scanButton, params(-1, -2, 18));

        deviceList = new LinearLayout(this);
        deviceList.setOrientation(LinearLayout.VERTICAL);
        root.addView(deviceList, params(-1, -2, 6));

        Button chooseButton = button("选择图片");
        chooseButton.setOnClickListener(view -> selectImage());
        root.addView(chooseButton, params(-1, -2, 12));

        sendButton = button("发送到手表");
        sendButton.setOnClickListener(view -> {
            if (preparedJpeg != null) watch.uploadImage(preparedJpeg);
        });
        root.addView(sendButton, params(-1, -2, 8));

        LinearLayout actions = new LinearLayout(this);
        actions.setOrientation(LinearLayout.HORIZONTAL);
        syncButton = smallButton("同步时间");
        syncButton.setOnClickListener(view -> watch.synchronizeTime());
        refreshButton = smallButton("刷新状态");
        refreshButton.setOnClickListener(view -> watch.refreshState());
        actions.addView(syncButton, weightParams(0, -2, 1, 8));
        actions.addView(refreshButton, weightParams(0, -2, 1, 0));
        root.addView(actions, params(-1, -2, 8));

        LinearLayout maintenance = new LinearLayout(this);
        maintenance.setOrientation(LinearLayout.HORIZONTAL);
        cancelButton = smallButton("取消传输");
        cancelButton.setOnClickListener(view -> watch.cancelTransfer());
        clearButton = smallButton("清除图片");
        clearButton.setOnClickListener(view -> watch.clearBadge());
        maintenance.addView(cancelButton, weightParams(0, -2, 1, 8));
        maintenance.addView(clearButton, weightParams(0, -2, 1, 0));
        root.addView(maintenance, params(-1, -2, 8));

        root.addView(divider(), params(-1, dp(1), 20));
        deviceValue = statusRow(root, "设备", "未连接");
        imageValue = statusRow(root, "图片", "尚未选择");
        transferValue = statusRow(root, "传输", "等待连接");
        timeValue = statusRow(root, "时间", "等待同步");
        return scroll;
    }

    private void beginScan() {
        if (!hasBluetoothPermission()) {
            requestBluetoothPermission();
            return;
        }
        if (!watch.isBluetoothEnabled()) {
            startActivity(new Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE));
            return;
        }
        watch.startScan();
    }

    private void selectImage() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("image/*");
        startActivityForResult(intent, REQUEST_IMAGE);
    }

    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_IMAGE || resultCode != RESULT_OK || data == null || data.getData() == null) return;
        try {
            prepareImage(data.getData());
        } catch (Exception error) {
            onError("无法处理这张图片");
        }
    }

    private void prepareImage(Uri uri) throws Exception {
        Bitmap source;
        try (InputStream input = getContentResolver().openInputStream(uri)) {
            source = BitmapFactory.decodeStream(input);
        }
        if (source == null) throw new IllegalArgumentException("invalid bitmap");
        int side = Math.min(source.getWidth(), source.getHeight());
        int left = (source.getWidth() - side) / 2;
        int top = (source.getHeight() - side) / 2;
        Bitmap badge = Bitmap.createBitmap(240, 240, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(badge);
        canvas.drawBitmap(source, new android.graphics.Rect(left, top, left + side, top + side),
                new android.graphics.Rect(0, 0, 240, 240), null);
        source.recycle();

        int quality = 90;
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        do {
            output.reset();
            badge.compress(Bitmap.CompressFormat.JPEG, quality, output);
            quality -= 10;
        } while (output.size() > WatchProtocol.MAX_JPEG_SIZE && quality >= 35);
        if (output.size() == 0 || output.size() > WatchProtocol.MAX_JPEG_SIZE) throw new IllegalArgumentException("image too large");
        preparedJpeg = output.toByteArray();
        preview.setImageBitmap(badge);
        imageValue.setText((preparedJpeg.length / 1024) + " KB JPEG");
        updateControls();
    }

    private boolean hasBluetoothPermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            return checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED
                    && checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED;
        }
        return checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED;
    }

    private void requestBluetoothPermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            requestPermissions(new String[] { Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT }, REQUEST_BLUETOOTH);
        } else {
            requestPermissions(new String[] { Manifest.permission.ACCESS_FINE_LOCATION }, REQUEST_BLUETOOTH);
        }
    }

    @Override public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(requestCode, permissions, results);
        if (requestCode == REQUEST_BLUETOOTH && hasBluetoothPermission()) beginScan();
        else if (requestCode == REQUEST_BLUETOOTH) onError("需要蓝牙附近设备权限才能连接手表");
    }

    @Override public void onDevices(List<WatchBleClient.Device> devices) {
        deviceList.removeAllViews();
        for (WatchBleClient.Device device : devices) {
            Button candidate = smallButton(device.name);
            candidate.setOnClickListener(view -> watch.connect(device));
            deviceList.addView(candidate, params(-1, -2, 4));
        }
    }

    @Override public void onConnection(String text, boolean connected) {
        deviceValue.setText(text);
        updateControls();
    }

    @Override public void onTransfer(String text) { transferValue.setText(text); }
    @Override public void onTime(String text) { timeValue.setText(text); }
    @Override public void onProgress(int percent) {
        progress.setProgress(percent);
        progressValue.setText(percent + "%");
    }
    @Override public void onError(String text) {
        transferValue.setText(text);
        Toast.makeText(this, text, Toast.LENGTH_LONG).show();
        updateControls();
    }

    private void updateControls() {
        boolean connected = watch != null && watch.isReady();
        if (sendButton != null) sendButton.setEnabled(connected && preparedJpeg != null);
        if (syncButton != null) syncButton.setEnabled(connected);
        if (refreshButton != null) refreshButton.setEnabled(connected);
        if (cancelButton != null) cancelButton.setEnabled(connected);
        if (clearButton != null) clearButton.setEnabled(connected);
    }

    private TextView statusRow(LinearLayout root, String label, String value) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        TextView left = text(label, 15, color("app_muted"));
        TextView right = text(value, 15, color("app_text"));
        right.setGravity(Gravity.END);
        row.addView(left, weightParams(0, -2, 1, 0));
        row.addView(right, weightParams(0, -2, 2, 0));
        root.addView(row, params(-1, -2, 12));
        return right;
    }

    private TextView text(String value, int size, int textColor) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextSize(size);
        view.setTextColor(textColor);
        return view;
    }

    private Button button(String value) {
        Button view = new Button(this);
        view.setText(value);
        view.setAllCaps(false);
        view.setTextColor(color("app_text"));
        view.setBackgroundColor(color("app_surface"));
        view.setMinHeight(dp(48));
        return view;
    }

    private Button smallButton(String value) {
        Button view = button(value);
        view.setTextSize(14);
        return view;
    }

    private View divider() {
        View view = new View(this);
        view.setBackgroundColor(color("app_surface"));
        return view;
    }

    private LinearLayout.LayoutParams params(int width, int height, int top) {
        LinearLayout.LayoutParams result = new LinearLayout.LayoutParams(width, height);
        result.topMargin = dp(top);
        return result;
    }

    private LinearLayout.LayoutParams weightParams(int width, int height, float weight, int end) {
        LinearLayout.LayoutParams result = new LinearLayout.LayoutParams(width, height, weight);
        result.setMarginEnd(dp(end));
        return result;
    }

    private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }
    private int color(String name) { return getColor(getResources().getIdentifier(name, "color", getPackageName())); }
}
