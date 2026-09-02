package com.huangshan.badge;

import android.content.ContentResolver;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

final class ResourceUpdatePackage {
    static final int MAX_PAYLOAD_BYTES = 512 * 1024;
    private static final int MAX_FILES = 16;
    private static final int MAX_MANIFEST_BYTES = 64 * 1024;
    private static final int MAX_ARCHIVE_CONTENT_BYTES = MAX_PAYLOAD_BYTES + MAX_MANIFEST_BYTES;

    static final class FileEntry {
        final String path;
        final byte[] data;
        final String sha256;

        FileEntry(String path, byte[] data, String sha256) {
            this.path = path;
            this.data = data;
            this.sha256 = sha256;
        }
    }

    final String displayName;
    final String baseVersion;
    final String targetVersion;
    final int payloadBytes;
    final List<FileEntry> files;

    private ResourceUpdatePackage(String displayName, String baseVersion,
                                  String targetVersion, int payloadBytes,
                                  List<FileEntry> files) {
        this.displayName = displayName;
        this.baseVersion = baseVersion;
        this.targetVersion = targetVersion;
        this.payloadBytes = payloadBytes;
        this.files = Collections.unmodifiableList(files);
    }

    static ResourceUpdatePackage read(ContentResolver resolver, Uri uri) throws Exception {
        if (resolver == null || uri == null) throw new IllegalArgumentException("无效的资源升级包");
        byte[] archive;
        try (InputStream input = resolver.openInputStream(uri)) {
            if (input == null) throw new IllegalArgumentException("无法打开资源升级包");
            archive = readLimited(input, MAX_ARCHIVE_CONTENT_BYTES, "资源升级包过大");
        }
        return parse(queryDisplayName(resolver, uri), archive);
    }

    static ResourceUpdatePackage parse(String displayName, byte[] archive) throws Exception {
        Map<String, byte[]> entries = new HashMap<>();
        Set<String> seen = new HashSet<>();
        int expandedBytes = 0;
        try (ZipInputStream zip = new ZipInputStream(new ByteArrayInputStream(archive))) {
            ZipEntry entry;
            while ((entry = zip.getNextEntry()) != null) {
                String name = entry.getName();
                if (entry.isDirectory()) {
                    zip.closeEntry();
                    continue;
                }
                if (!seen.add(name)) throw new IllegalArgumentException("升级包包含重复文件: " + name);
                int limit = "manifest.json".equals(name) ? MAX_MANIFEST_BYTES : MAX_PAYLOAD_BYTES;
                byte[] data = readLimited(zip, limit, "升级包文件过大: " + name);
                expandedBytes += data.length;
                if (expandedBytes > MAX_ARCHIVE_CONTENT_BYTES) {
                    throw new IllegalArgumentException("资源升级包解压后过大");
                }
                entries.put(name, data);
                zip.closeEntry();
            }
        }

        byte[] manifestBytes = entries.remove("manifest.json");
        if (manifestBytes == null) throw new IllegalArgumentException("资源升级包缺少 manifest.json");
        JSONObject manifest = new JSONObject(new String(manifestBytes, java.nio.charset.StandardCharsets.UTF_8));
        if (manifest.optInt("format", 0) != 1) throw new IllegalArgumentException("不支持的资源包格式");
        String baseVersion = requireVersion(manifest.getString("baseVersion"));
        String targetVersion = requireVersion(manifest.getString("targetVersion"));
        if (baseVersion.equals(targetVersion)) throw new IllegalArgumentException("资源包版本没有变化");
        JSONArray deleted = manifest.optJSONArray("deleted");
        if (deleted != null && deleted.length() != 0) {
            throw new IllegalArgumentException("当前设备端不支持删除资源文件");
        }
        JSONArray records = manifest.getJSONArray("files");
        if (records.length() == 0 || records.length() > MAX_FILES) {
            throw new IllegalArgumentException("资源包文件数必须为 1 到 " + MAX_FILES);
        }

        List<FileEntry> files = new ArrayList<>();
        Set<String> paths = new HashSet<>();
        int payloadBytes = 0;
        for (int index = 0; index < records.length(); index++) {
            JSONObject record = records.getJSONObject(index);
            String path = requireResourcePath(record.getString("path"));
            if (!paths.add(path)) throw new IllegalArgumentException("清单包含重复路径: " + path);
            int size = record.getInt("size");
            String expectedSha256 = record.getString("sha256").toLowerCase(Locale.ROOT);
            if (!expectedSha256.matches("[0-9a-f]{64}")) {
                throw new IllegalArgumentException("SHA-256 格式错误: " + path);
            }
            byte[] data = entries.remove(path);
            if (data == null || size != data.length) throw new IllegalArgumentException("资源大小不匹配: " + path);
            if (!expectedSha256.equals(sha256(data))) throw new IllegalArgumentException("资源校验失败: " + path);
            payloadBytes = Math.addExact(payloadBytes, Math.addExact(data.length, 4));
            if (payloadBytes > MAX_PAYLOAD_BYTES) throw new IllegalArgumentException("差分资源超过 512 KiB 暂存上限");
            files.add(new FileEntry(path, data, expectedSha256));
        }
        if (!entries.isEmpty()) throw new IllegalArgumentException("升级包包含清单外文件: " + entries.keySet().iterator().next());
        if (manifest.getInt("payloadBytes") != payloadBytes) throw new IllegalArgumentException("升级包传输大小不匹配");
        return new ResourceUpdatePackage(displayName, baseVersion, targetVersion, payloadBytes, files);
    }

    private static String requireResourcePath(String path) {
        if (path == null || path.length() >= 56 || path.contains("..") || path.startsWith("/")
                || !(path.matches("resource/[A-Za-z0-9_.-]+\\.bin")
                || path.matches("font/[A-Za-z0-9_.-]+\\.ttf"))) {
            throw new IllegalArgumentException("不安全的资源路径: " + path);
        }
        return path;
    }

    private static String requireVersion(String version) {
        if (version == null || !version.matches("[A-Za-z0-9_.-]{1,15}")) {
            throw new IllegalArgumentException("资源版本格式错误");
        }
        return version;
    }

    private static byte[] readLimited(InputStream input, int limit, String message) throws Exception {
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        byte[] buffer = new byte[8192];
        int read;
        while ((read = input.read(buffer)) != -1) {
            if (output.size() > limit - read) throw new IllegalArgumentException(message);
            output.write(buffer, 0, read);
        }
        return output.toByteArray();
    }

    private static String sha256(byte[] data) throws Exception {
        byte[] digest = MessageDigest.getInstance("SHA-256").digest(data);
        StringBuilder value = new StringBuilder(64);
        for (byte item : digest) value.append(String.format(Locale.ROOT, "%02x", item & 0xff));
        return value.toString();
    }

    private static String queryDisplayName(ContentResolver resolver, Uri uri) {
        try (Cursor cursor = resolver.query(uri, new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                String name = cursor.getString(0);
                if (name != null && !name.isEmpty()) return name;
            }
        } catch (RuntimeException ignored) { }
        String last = uri.getLastPathSegment();
        return last == null ? "resources.apres" : last;
    }
}
