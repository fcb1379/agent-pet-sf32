package com.huangshan.badge;

import java.util.LinkedHashMap;
import java.util.Map;

final class FindMomoState {
    private boolean available;
    private boolean active;
    private boolean pending;
    private int sessionId;
    private String status = "当前固件不支持呼叫 Momo";

    synchronized void applyDeviceState(String payload) {
        available = "1".equals(parseValues(payload).get("find"));
        if (!available) {
            active = false;
            pending = false;
            sessionId = 0;
            status = "当前固件不支持呼叫 Momo";
        } else if (!active && !pending) {
            status = "Momo 已就绪";
        }
    }

    synchronized boolean beginStart(int id) {
        if (!available || active || pending || id <= 0 || id > 65535) return false;
        pending = true;
        sessionId = id;
        status = "正在呼叫 Momo…";
        return true;
    }

    synchronized boolean beginStop() {
        if (!available || !active || pending || sessionId == 0) return false;
        pending = true;
        status = "正在停止呼叫…";
        return true;
    }

    synchronized void failPending(String message) {
        pending = false;
        status = (message == null || message.isEmpty()) ? "操作失败" : message;
    }

    synchronized void applyFindResponse(String payload) {
        Map<String, String> values = parseValues(payload);
        String state = values.getOrDefault("s", "IDLE");
        String result = values.getOrDefault("r", "STATUS");
        int responseSession = parseId(values.get("id"));

        active = "ACTIVE".equals(state) || "ARMING".equals(state)
                || "STOPPING".equals(state);
        if (responseSession != 0) sessionId = responseSession;
        if (!active && "IDLE".equals(state)) sessionId = 0;
        pending = "STOPPING".equals(state);
        status = active ? "Momo 正在等你（点击设备屏幕可停止）"
                : findResultText(result);
    }

    synchronized boolean applyEndEvent(int id, String reason) {
        if (id <= 0 || id != sessionId) return false;
        active = false;
        pending = false;
        sessionId = 0;
        status = "呼叫已结束：" + reasonText(reason);
        return true;
    }

    synchronized boolean isAvailable() { return available; }
    synchronized boolean isActive() { return active; }
    synchronized boolean isPending() { return pending; }
    synchronized int getSessionId() { return sessionId; }
    synchronized String getStatus() { return status; }

    static Map<String, String> parseValues(String payload) {
        Map<String, String> values = new LinkedHashMap<>();
        if (payload == null) return values;
        for (String field : payload.split(";")) {
            String[] pair = field.split("=", 2);
            if (pair.length == 2) values.put(pair[0], pair[1]);
        }
        return values;
    }

    private static int parseId(String value) {
        try {
            int id = Integer.parseInt(value == null ? "0" : value);
            return id >= 1 && id <= 65535 ? id : 0;
        } catch (NumberFormatException ignored) {
            return 0;
        }
    }

    private static String findResultText(String result) {
        switch (result) {
            case "BUSY_ALARM": return "闹钟或计时器正在响铃";
            case "BUSY_TRANSFER": return "图片或 GIF 正在传输";
            case "RATE_LIMIT": return "请稍后再呼叫";
            case "STALE": return "该呼叫已结束，请重新点击";
            default: return "Momo 已就绪";
        }
    }

    private static String reasonText(String reason) {
        switch (reason) {
            case "USER_STOP": return "已在设备端停止";
            case "PHONE_STOP": return "已在手机端停止";
            case "TIMEOUT": return "60 秒超时";
            case "PREEMPTED_ALARM": return "已让位给闹钟或计时器";
            default: return reason == null ? "未知原因" : reason;
        }
    }
}
