/*
 * 录音和 FFmpeg 编解码包含较大的静态工作表与帧缓存。板级已配置外部
 * PSRAM，将这些非保留零初始化数据移出主 SRAM，避免挤占 RTOS、音频
 * DMA 与 GUI 的实时工作区。
 */
SECTIONS
{
    .recorder_psram_bss
        (ADDR(.RW_PSRAM_NON_RET) + SIZEOF(.RW_PSRAM_NON_RET)) (NOLOAD) :
    {
        . = ALIGN(8);
        __recorder_psram_bss_start__ = .;
        *recorder_service.o(.bss .bss.* COMMON)
        *media_player_server.o(
            .bss.ffmpeg_read_thread_stack
            .bss.ffmpeg_read_thread_control)
        *ffmpeg*(.bss .bss.* COMMON)
        . = ALIGN(8);
        __recorder_psram_bss_end__ = .;
        __RW_PSRAM_NON_RET_end__ = .;
    }
}
ASSERT(__recorder_psram_bss_end__ <= (ORIGIN(PSRAM) + LENGTH(PSRAM)),
       "Recorder PSRAM workspace overflow")
INSERT AFTER .RW_PSRAM_NON_RET;
