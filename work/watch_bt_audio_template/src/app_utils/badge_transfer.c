#include <rtthread.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "bf0_sibles_watchface.h"
#include "dfs_posix.h"
#include "badge_transfer.h"
#ifdef LV_USING_FILE_RESOURCE
#include "resource_update.h"
#endif

#define LOG_TAG "badge_xfer"
#include "log.h"

#define BADGE_TEMP_PATH "/badge.tmp"
#define BADGE_BACKUP_PATH "/badge.bak"
#define BADGE_CRC_INIT 0xffffffffU
#define BADGE_MAX_FILE_SIZE (2U * 1024U * 1024U)
#define BADGE_VALIDATE_BUFFER_SIZE 128U

typedef struct
{
    int fd;
    uint32_t total;
    uint32_t received;
    uint32_t all_files_total;
    uint32_t crc;
    uint32_t generation;
    uint32_t last_activity_tick;
    int16_t last_error;
    uint8_t state;
    uint8_t image_available;
    uint8_t session_type;
} badge_transfer_env_t;

static badge_transfer_env_t g_badge = {
    .fd = -1,
    .state = BADGE_TRANSFER_IDLE,
};

static uint32_t badge_crc32_mpeg2(const uint8_t *data, uint32_t len, uint32_t crc)
{
    uint32_t i;

    while (len--)
    {
        crc ^= (uint32_t)(*data++) << 24;
        for (i = 0; i < 8; i++)
        {
            crc = (crc & 0x80000000U) ? (crc << 1) ^ 0x04c11db7U : crc << 1;
        }
    }
    return crc;
}

static uint32_t badge_u32_be(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           data[3];
}

static void badge_close_temp(void)
{
    if (g_badge.fd >= 0)
    {
        close(g_badge.fd);
        g_badge.fd = -1;
    }
}

static void badge_fail(int error)
{
    badge_close_temp();
    unlink(BADGE_TEMP_PATH);
    g_badge.last_activity_tick = rt_tick_get();
    g_badge.state = BADGE_TRANSFER_ERROR;
    g_badge.last_error = error;
}

static int badge_write_all(int fd, const uint8_t *data, uint32_t len)
{
    uint32_t offset = 0;

    while (offset < len)
    {
        int written = write(fd, data + offset, len - offset);

        if (written <= 0)
        {
            return -RT_ERROR;
        }
        offset += written;
    }

    return RT_EOK;
}

static int badge_validate_jpeg_file(void)
{
    uint8_t buffer[BADGE_VALIDATE_BUFFER_SIZE];
    uint8_t previous = 0;
    uint8_t has_soi = 0;
    uint8_t has_eoi = 0;
    int fd;
    int read_len;

    fd = open(BADGE_TEMP_PATH, O_RDONLY | O_BINARY, 0);
    if (fd < 0)
    {
        return BLE_WATCHFACE_STATUS_FILE_OPEN_ERROR;
    }

    while ((read_len = read(fd, buffer, sizeof(buffer))) > 0)
    {
        int index;

        for (index = 0; index < read_len; index++)
        {
            if (previous == 0xff && buffer[index] == 0xd8)
            {
                has_soi = 1;
            }
            if (previous == 0xff && buffer[index] == 0xd9)
            {
                has_eoi = 1;
            }
            previous = buffer[index];
        }
    }
    close(fd);

    if (read_len < 0)
    {
        return BLE_WATCHFACE_STATUS_FILE_CLOSE_ERROR;
    }

    return (has_soi && has_eoi) ? BLE_WATCHFACE_STATUS_OK : BLE_WATCHFACE_STATUS_APP_ERROR;
}

static int badge_begin_file(uint32_t total)
{
    struct statfs fs;
    uint64_t available_bytes;

    if (total <= 4 || total > BADGE_MAX_FILE_SIZE || (total & 3U))
    {
        return BLE_WATCHFACE_STATUS_FILE_SIZE_ERROR;
    }

    if (dfs_statfs("/", &fs) != 0)
    {
        return BLE_WATCHFACE_STATUS_SPACE_ERROR;
    }
    available_bytes = (uint64_t)fs.f_bsize * fs.f_bfree;
    if (available_bytes < total)
    {
        return BLE_WATCHFACE_STATUS_SPACE_ERROR;
    }

    badge_close_temp();
    unlink(BADGE_TEMP_PATH);
    g_badge.fd = open(BADGE_TEMP_PATH, O_CREAT | O_RDWR | O_TRUNC | O_BINARY, 0);
    if (g_badge.fd < 0)
    {
        return BLE_WATCHFACE_STATUS_FILE_OPEN_ERROR;
    }

    g_badge.total = total;
    g_badge.received = 0;
    g_badge.crc = BADGE_CRC_INIT;
    g_badge.last_activity_tick = rt_tick_get();
    g_badge.last_error = 0;
    g_badge.state = BADGE_TRANSFER_RECEIVING;
    return BLE_WATCHFACE_STATUS_OK;
}

static int badge_write_chunk(const uint8_t *data, uint32_t len)
{
    uint32_t payload_len = len;

    if (g_badge.fd < 0 || !data || len == 0 || g_badge.received + len > g_badge.total)
    {
        return BLE_WATCHFACE_STATUS_FILE_SIZE_ERROR;
    }

    if (g_badge.received + len == g_badge.total)
    {
        uint32_t expected_crc;

        if (len < 4)
        {
            return BLE_WATCHFACE_STATUS_FILE_SIZE_ERROR;
        }
        payload_len -= 4;
        g_badge.crc = badge_crc32_mpeg2(data, payload_len, g_badge.crc);
        expected_crc = badge_u32_be(data + payload_len);
        if (expected_crc != g_badge.crc)
        {
            return BLE_WATCHFACE_STATUS_CRC_CALCULATE_ERROR;
        }
    }
    else
    {
        g_badge.crc = badge_crc32_mpeg2(data, len, g_badge.crc);
    }

    if (payload_len && badge_write_all(g_badge.fd, data, payload_len) != RT_EOK)
    {
        return BLE_WATCHFACE_STATUS_FILE_WRITE_ERROR;
    }

    g_badge.received += len;
    g_badge.last_activity_tick = rt_tick_get();
    return BLE_WATCHFACE_STATUS_OK;
}

static int badge_commit_file(void)
{
    badge_close_temp();
    if (badge_validate_jpeg_file() != BLE_WATCHFACE_STATUS_OK)
    {
        return BLE_WATCHFACE_STATUS_APP_ERROR;
    }

    unlink(BADGE_BACKUP_PATH);
    if (access(BADGE_IMAGE_PATH, 0) == 0 &&
            rename(BADGE_IMAGE_PATH, BADGE_BACKUP_PATH) != 0)
    {
        return BLE_WATCHFACE_STATUS_FILE_WRITE_ERROR;
    }
    if (rename(BADGE_TEMP_PATH, BADGE_IMAGE_PATH) != 0)
    {
        rename(BADGE_BACKUP_PATH, BADGE_IMAGE_PATH);
        return BLE_WATCHFACE_STATUS_FILE_WRITE_ERROR;
    }
    unlink(BADGE_BACKUP_PATH);

    g_badge.image_available = 1;
    g_badge.generation++;
    g_badge.last_activity_tick = rt_tick_get();
    g_badge.state = BADGE_TRANSFER_READY;
    g_badge.last_error = 0;
    return BLE_WATCHFACE_STATUS_OK;
}

static watchface_event_ack_t badge_watchface_event(uint16_t event, uint16_t length, void *param)
{
    int result = BLE_WATCHFACE_STATUS_OK;

#ifdef LV_USING_FILE_RESOURCE
    if (event == WATCHFACE_APP_START)
    {
        ble_watchface_start_ind_t *info = param;

        if (info && info->type == WATCHFACE_FILE_TYPE_CUSTOMIZED)
        {
            g_badge.session_type = WATCHFACE_FILE_TYPE_CUSTOMIZED;
            return RESUPDATE_HandleWatchfaceEvent(event, length, param);
        }
        g_badge.session_type = WATCHFACE_FILE_TYPE_BACKGROUND_PIC;
    }
    else if (g_badge.session_type == WATCHFACE_FILE_TYPE_CUSTOMIZED)
    {
        return RESUPDATE_HandleWatchfaceEvent(event, length, param);
    }
#endif

    (void)length;
    switch (event)
    {
    case WATCHFACE_APP_START:
    {
        ble_watchface_start_ind_t *info = param;
        struct statfs fs;

        if (dfs_statfs("/", &fs) != 0)
        {
            result = BLE_WATCHFACE_STATUS_SPACE_ERROR;
            ble_watchface_send_start_rsp_file_info(result, 0, 0);
            break;
        }
        g_badge.all_files_total = info->all_files_len;
        ble_watchface_send_start_rsp_file_info(result, fs.f_bsize, fs.f_bfree);
        break;
    }
    case WATCHFACE_APP_FILE_INFO:
        ble_watchface_file_info_rsp(result);
        break;
    case WATCHFACE_APP_FILE_START:
        result = badge_begin_file(((ble_watchface_file_start_ind_t *)param)->file_len);
        ble_watchface_file_start_rsp(result);
        break;
    case WATCHFACE_APP_FILE_DOWNLOAD:
    {
        ble_watchface_file_download_ind_t *chunk = param;
        result = badge_write_chunk(chunk->data, chunk->data_len);
        ble_watchface_file_download_rsp(result);
        break;
    }
    case WATCHFACE_APP_FILE_END:
    {
        ble_watchface_file_end_ind_t *info = param;
        if (info->end_status != BLE_WATCHFACE_STATUS_OK ||
                g_badge.received != g_badge.total)
        {
            result = BLE_WATCHFACE_STATUS_FILE_SIZE_ERROR;
        }
        else
        {
            result = badge_commit_file();
        }
        ble_watchface_file_end_rsp(result);
        break;
    }
    case WATCHFACE_APP_END:
        ble_watchface_end_rsp(g_badge.received == g_badge.all_files_total ?
                              BLE_WATCHFACE_STATUS_OK :
                              BLE_WATCHFACE_STATUS_FILE_SIZE_ERROR);
        break;
    case WATCHFACE_APP_ERROR:
        result = ((ble_watchface_error_ind_t *)param)->error_type;
        break;
    default:
        return WATCHFACE_EVENT_SUCCESSED;
    }

    if (result != BLE_WATCHFACE_STATUS_OK)
    {
        LOG_E("badge transfer event=%d error=%d", event, result);
        badge_fail(result);
        ble_watchface_abort();
        return WATCHFACE_EVENT_FAILED;
    }

    return WATCHFACE_EVENT_SUCCESSED;
}

void badge_transfer_get_snapshot(badge_transfer_snapshot_t *snapshot)
{
    struct stat st;

    if (!snapshot)
    {
        return;
    }

    snapshot->state = (badge_transfer_state_t)g_badge.state;
    snapshot->received = g_badge.received;
    snapshot->total = g_badge.total;
    snapshot->generation = g_badge.generation;
    snapshot->last_activity_tick = g_badge.last_activity_tick;
    snapshot->last_error = g_badge.last_error;
    snapshot->image_available = g_badge.image_available || stat(BADGE_IMAGE_PATH, &st) == 0;
}

int badge_transfer_clear(void)
{
    int ret = RT_EOK;

    if (g_badge.state == BADGE_TRANSFER_RECEIVING)
    {
        ble_watchface_abort();
    }
    badge_close_temp();
    if (access(BADGE_TEMP_PATH, 0) == 0 && unlink(BADGE_TEMP_PATH) != 0)
    {
        ret = -RT_ERROR;
    }
    if (access(BADGE_BACKUP_PATH, 0) == 0 && unlink(BADGE_BACKUP_PATH) != 0)
    {
        ret = -RT_ERROR;
    }
    if (access(BADGE_IMAGE_PATH, 0) == 0 && unlink(BADGE_IMAGE_PATH) != 0)
    {
        ret = -RT_ERROR;
    }

    g_badge.total = 0;
    g_badge.received = 0;
    g_badge.all_files_total = 0;
    g_badge.crc = BADGE_CRC_INIT;
    g_badge.generation++;
    g_badge.last_activity_tick = rt_tick_get();
    g_badge.last_error = (ret == RT_EOK) ? 0 : BLE_WATCHFACE_STATUS_FILE_WRITE_ERROR;
    g_badge.state = (ret == RT_EOK) ? BADGE_TRANSFER_IDLE : BADGE_TRANSFER_ERROR;
    g_badge.image_available = 0;
    return ret;
}

int badge_transfer_cancel(void)
{
    if (g_badge.state != BADGE_TRANSFER_RECEIVING)
    {
        return -RT_EEMPTY;
    }

    ble_watchface_abort();
    badge_fail(BLE_WATCHFACE_STATUS_USER_ABORT);
    return RT_EOK;
}

static int badge_transfer_init(void)
{
    struct stat st;

    if (stat(BADGE_IMAGE_PATH, &st) != 0 && stat(BADGE_BACKUP_PATH, &st) == 0)
    {
        rename(BADGE_BACKUP_PATH, BADGE_IMAGE_PATH);
    }
    g_badge.image_available = stat(BADGE_IMAGE_PATH, &st) == 0;
    if (g_badge.image_available)
    {
        g_badge.state = BADGE_TRANSFER_READY;
    }
    watchface_register(badge_watchface_event);
    return RT_EOK;
}
INIT_APP_EXPORT(badge_transfer_init);

__ROM_USED void badge(int argc, char **argv)
{
    badge_transfer_snapshot_t snapshot;

    if (argc >= 2 && strcmp(argv[1], "clear") == 0)
    {
        rt_kprintf("badge clear ret=%d\n", badge_transfer_clear());
        return;
    }

    if (argc >= 2 && strcmp(argv[1], "cancel") == 0)
    {
        rt_kprintf("badge cancel ret=%d\n", badge_transfer_cancel());
        return;
    }

    badge_transfer_get_snapshot(&snapshot);
    rt_kprintf("badge state=%d image=%d received=%lu/%lu generation=%lu active=%lu error=%d path=%s\n",
               snapshot.state,
               snapshot.image_available,
               (unsigned long)snapshot.received,
               (unsigned long)snapshot.total,
               (unsigned long)snapshot.generation,
               (unsigned long)snapshot.last_activity_tick,
               snapshot.last_error,
               BADGE_IMAGE_PATH);
}
MSH_CMD_EXPORT(badge, electronic badge transfer status clear or cancel);
