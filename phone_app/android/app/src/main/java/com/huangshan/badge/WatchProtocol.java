package com.huangshan.badge;

import java.io.ByteArrayOutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

final class WatchProtocol {
    static final UUID LINK_SERVICE_UUID = UUID.fromString("01000000-4b4e-494c-5f48-435441575348");
    static final UUID LINK_CHARACTERISTIC_UUID = UUID.fromString("02000000-4b4e-494c-5f48-435441575348");
    static final UUID SERIAL_SERVICE_UUID = UUID.fromString("00000000-0000-0000-6473-5f696c666973");
    static final UUID SERIAL_DATA_UUID = UUID.fromString("00000000-0000-0200-6473-5f696c666973");

    static final int SERIAL_CATEGORY_WATCHFACE = 0x04;
    static final int BACKGROUND_FILE_TYPE = 2;
    static final int PHONE_TYPE_ANDROID = 2;
    static final int CHUNK_SIZE = 180;
    static final int MAX_JPEG_SIZE = 2 * 1024 * 1024 - 4;

    private WatchProtocol() { }

    static byte[] u16(int value) {
        return ByteBuffer.allocate(2).order(ByteOrder.LITTLE_ENDIAN).putShort((short) value).array();
    }

    static byte[] u32(long value) {
        return ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN).putInt((int) value).array();
    }

    static int readU16(byte[] bytes, int offset) {
        return (bytes[offset] & 0xff) | ((bytes[offset + 1] & 0xff) << 8);
    }

    static byte[] join(byte[]... parts) {
        int length = 0;
        for (byte[] part : parts) length += part.length;
        ByteArrayOutputStream output = new ByteArrayOutputStream(length);
        for (byte[] part : parts) output.write(part, 0, part.length);
        return output.toByteArray();
    }

    static byte[] watchfaceMessage(int id, byte[] data) {
        return join(u16(id), u16(data.length), data);
    }

    static List<byte[]> serialFrames(byte[] payload) {
        List<byte[]> frames = new ArrayList<>();
        if (payload.length <= 16) {
            frames.add(join(new byte[] { SERIAL_CATEGORY_WATCHFACE, 0 }, u16(payload.length), payload));
            return frames;
        }

        frames.add(join(new byte[] { SERIAL_CATEGORY_WATCHFACE, 1 }, u16(payload.length), slice(payload, 0, 16)));
        for (int offset = 16; offset < payload.length; offset += 18) {
            int end = Math.min(offset + 18, payload.length);
            int flag = end == payload.length ? 3 : 2;
            frames.add(join(new byte[] { SERIAL_CATEGORY_WATCHFACE, (byte) flag }, slice(payload, offset, end)));
        }
        return frames;
    }

    static byte[] makeUpload(byte[] jpeg) {
        int padding = (4 - jpeg.length % 4) % 4;
        byte[] padded = join(jpeg, new byte[padding]);
        return join(padded, u32(crc32Mpeg2(padded)));
    }

    static long crc32Mpeg2(byte[] bytes) {
        long crc = 0xffffffffL;
        for (byte value : bytes) {
            crc ^= (long) (value & 0xff) << 24;
            for (int bit = 0; bit < 8; bit++) {
                crc = (crc & 0x80000000L) != 0 ? ((crc << 1) ^ 0x04c11db7L) : (crc << 1);
                crc &= 0xffffffffL;
            }
        }
        return crc;
    }

    private static byte[] slice(byte[] source, int start, int end) {
        byte[] copy = new byte[end - start];
        System.arraycopy(source, start, copy, 0, copy.length);
        return copy;
    }
}
