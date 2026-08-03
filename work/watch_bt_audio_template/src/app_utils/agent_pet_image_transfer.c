#include "agent_pet_image_transfer.h"

#include <fcntl.h>
#include <rtthread.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "agent_pet_protocol.h"
#include "dfs_posix.h"

#define LOG_TAG "agent_pet_img"
#include "log.h"

#define AGENTPET_IMAGE_TEMP_PATH        "/pet.tmp"
#define AGENTPET_IMAGE_BACKUP_PATH      "/pet.bak"
#define AGENTPET_IMAGE_MAGIC_FIRST      (0x41U)
#define AGENTPET_IMAGE_MAGIC_SECOND     (0x49U)
#define AGENTPET_IMAGE_PROTOCOL_VERSION (1U)
#define AGENTPET_IMAGE_COMMAND_BEGIN    (1U)
#define AGENTPET_IMAGE_COMMAND_DATA     (2U)
#define AGENTPET_IMAGE_COMMAND_COMMIT   (3U)
#define AGENTPET_IMAGE_COMMAND_RESET    (4U)
#define AGENTPET_IMAGE_FORMAT_JPEG      (1U)
#define AGENTPET_IMAGE_DATA_OFFSET      (8U)
#define AGENTPET_IMAGE_DATA_SIZE        (11U)
#define AGENTPET_IMAGE_CRC_OFFSET       (19U)
#define AGENTPET_IMAGE_WRITE_BUFFER_SIZE (512U)
#define AGENTPET_IMAGE_CRC32_INIT       (0xFFFFFFFFUL)
#define AGENTPET_IMAGE_QUEUE_DEPTH      (32U)
#define AGENTPET_IMAGE_THREAD_STACK_SIZE (2048U)
#define AGENTPET_IMAGE_THREAD_TIME_SLICE (10U)

/* AGENTPET_IMAGE_ENV: custom mascot transfer and persistent-file state.
 * Members:
 *   - lFileDescriptor: temporary file descriptor, -1 when no transfer is active
 *   - ulTotal/ulReceived: validated JPEG byte counts, range 0..128 KiB
 *   - ulExpectedCrc/ulCalculatedCrc: CRC-32/MPEG-2 sent by the host and calculated locally
 *   - ulGeneration: increments only after an atomic replacement or reset, for LVGL refresh
 *   - usPendingLength/aWriteBuffer: bounded write coalescing buffer to reduce filesystem writes
 *   - eState/eLastResult: current transfer state and last protocol/storage result
 *   - bImageAvailable: true only when the committed persistent image exists
 */
typedef struct _AGENTPET_IMAGE_ENV
{
    int lFileDescriptor;
    uint32_t ulTotal;
    uint32_t ulReceived;
    uint32_t ulExpectedCrc;
    uint32_t ulCalculatedCrc;
    uint32_t ulGeneration;
    uint16_t usPendingLength;
    AGENTPET_IMAGE_STATE eState;
    AGENTPET_IMAGE_RESULT eLastResult;
    bool bImageAvailable;
    uint8_t aWriteBuffer[AGENTPET_IMAGE_WRITE_BUFFER_SIZE];
} AGENTPET_IMAGE_ENV;

/* Module-local transfer state. The fixed 512-byte buffer bounds RAM usage and is
 * accessed only from the serialized GATT callback; LVGL reads a copied snapshot. */
/* Static RTOS resources keep GATT writes bounded and avoid heap fragmentation.
 * Queue capacity: 32 frames (640 payload bytes); worker stack: 2048 bytes. */
static struct rt_messagequeue l_tImageQueue;
static struct rt_mutex l_tImageMutex;
static struct rt_thread l_tImageThread;
static uint8_t l_aImageThreadStack[AGENTPET_IMAGE_THREAD_STACK_SIZE];
static uint8_t l_aImageQueuePool[
    (RT_ALIGN(AGENTPET_IMAGE_FRAME_SIZE, RT_ALIGN_SIZE) +
     sizeof(void *)) * AGENTPET_IMAGE_QUEUE_DEPTH];
static bool l_bImageWorkerReady;
static AGENTPET_IMAGE_ENV l_tImageEnv =
{
    .lFileDescriptor = -1,
    .eState = AGENTPET_IMAGE_IDLE,
    .eLastResult = AGENTPET_IMAGE_RESULT_ACCEPTED
};

static uint32_t Local_ReadLe24(const uint8_t *pData)
{
    uint32_t ulValue;

    ulValue = (uint32_t)pData[0];
    ulValue |= (uint32_t)pData[1] << 8U;
    ulValue |= (uint32_t)pData[2] << 16U;

    return ulValue;
}

static uint32_t Local_ReadLe32(const uint8_t *pData)
{
    uint32_t ulValue;

    ulValue = (uint32_t)pData[0];
    ulValue |= (uint32_t)pData[1] << 8U;
    ulValue |= (uint32_t)pData[2] << 16U;
    ulValue |= (uint32_t)pData[3] << 24U;

    return ulValue;
}

static uint32_t Local_Crc32Mpeg2(
    const uint8_t *pData,
    uint32_t ulLength,
    uint32_t ulCrc)
{
    uint32_t ulIndex;
    uint8_t ucBit;

    if (NULL == pData)
    {
        return ulCrc;
    }

    for (ulIndex = 0U; ulIndex < ulLength; ulIndex++)
    {
        ulCrc ^= (uint32_t)pData[ulIndex] << 24U;
        for (ucBit = 0U; ucBit < 8U; ucBit++)
        {
            if (0U != (ulCrc & 0x80000000UL))
            {
                ulCrc = (ulCrc << 1U) ^ 0x04C11DB7UL;
            }
            else
            {
                ulCrc <<= 1U;
            }
        }
    }

    return ulCrc;
}

static void Local_CloseTemporaryFile(void)
{
    if (0 <= l_tImageEnv.lFileDescriptor)
    {
        (void)close(l_tImageEnv.lFileDescriptor);
        l_tImageEnv.lFileDescriptor = -1;
    }

    return;
}

static AGENTPET_IMAGE_RESULT Local_WriteAll(
    int lFileDescriptor,
    const uint8_t *pData,
    uint32_t ulLength)
{
    uint32_t ulOffset;

    if ((0 > lFileDescriptor) || (NULL == pData))
    {
        return AGENTPET_IMAGE_ERROR_INVALID_PARAMETER;
    }

    ulOffset = 0U;
    while (ulOffset < ulLength)
    {
        int lWritten;

        lWritten = write(
            lFileDescriptor,
            &pData[ulOffset],
            ulLength - ulOffset);
        if (0 >= lWritten)
        {
            return AGENTPET_IMAGE_ERROR_STORAGE;
        }
        ulOffset += (uint32_t)lWritten;
    }

    return AGENTPET_IMAGE_RESULT_ACCEPTED;
}

static AGENTPET_IMAGE_RESULT Local_FlushWriteBuffer(void)
{
    AGENTPET_IMAGE_RESULT eResult;

    if (0U == l_tImageEnv.usPendingLength)
    {
        return AGENTPET_IMAGE_RESULT_ACCEPTED;
    }

    eResult = Local_WriteAll(
        l_tImageEnv.lFileDescriptor,
        l_tImageEnv.aWriteBuffer,
        l_tImageEnv.usPendingLength);
    if (AGENTPET_IMAGE_RESULT_ACCEPTED == eResult)
    {
        l_tImageEnv.usPendingLength = 0U;
    }

    return eResult;
}

static void Local_FailTransfer(AGENTPET_IMAGE_RESULT eResult)
{
    Local_CloseTemporaryFile();
    (void)unlink(AGENTPET_IMAGE_TEMP_PATH);
    l_tImageEnv.usPendingLength = 0U;
    l_tImageEnv.eState = AGENTPET_IMAGE_ERROR;
    l_tImageEnv.eLastResult = eResult;

    return;
}

static AGENTPET_IMAGE_RESULT Local_ValidateJpeg(void)
{
    uint8_t aHeader[2];
    uint8_t aFooter[2];
    int lFileDescriptor;
    int lReadLength;

    lFileDescriptor = open(AGENTPET_IMAGE_TEMP_PATH, O_RDONLY | O_BINARY, 0);
    if (0 > lFileDescriptor)
    {
        return AGENTPET_IMAGE_ERROR_STORAGE;
    }

    lReadLength = read(lFileDescriptor, aHeader, sizeof(aHeader));
    if ((int)sizeof(aHeader) != lReadLength)
    {
        (void)close(lFileDescriptor);
        return AGENTPET_IMAGE_ERROR_FORMAT;
    }
    if (0 > lseek(lFileDescriptor, -(int)sizeof(aFooter), SEEK_END))
    {
        (void)close(lFileDescriptor);
        return AGENTPET_IMAGE_ERROR_STORAGE;
    }
    lReadLength = read(lFileDescriptor, aFooter, sizeof(aFooter));
    (void)close(lFileDescriptor);
    if ((int)sizeof(aFooter) != lReadLength)
    {
        return AGENTPET_IMAGE_ERROR_FORMAT;
    }
    if (
        (0xFFU != aHeader[0]) ||
        (0xD8U != aHeader[1]) ||
        (0xFFU != aFooter[0]) ||
        (0xD9U != aFooter[1])
    )
    {
        return AGENTPET_IMAGE_ERROR_FORMAT;
    }

    return AGENTPET_IMAGE_RESULT_ACCEPTED;
}

static AGENTPET_IMAGE_RESULT Local_BeginTransfer(
    uint32_t ulTotal,
    uint32_t ulExpectedCrc,
    uint8_t ucFormat)
{
    struct statfs tFileSystem;
    uint64_t udAvailableBytes;

    if (
        (AGENTPET_IMAGE_FORMAT_JPEG != ucFormat) ||
        (4U > ulTotal) ||
        (AGENTPET_IMAGE_MAX_FILE_SIZE < ulTotal)
    )
    {
        return AGENTPET_IMAGE_ERROR_SIZE;
    }
    if (0 != dfs_statfs("/", &tFileSystem))
    {
        return AGENTPET_IMAGE_ERROR_STORAGE;
    }
    udAvailableBytes = (uint64_t)tFileSystem.f_bsize * tFileSystem.f_bfree;
    if ((uint64_t)ulTotal > udAvailableBytes)
    {
        return AGENTPET_IMAGE_ERROR_STORAGE;
    }

    Local_CloseTemporaryFile();
    (void)unlink(AGENTPET_IMAGE_TEMP_PATH);
    l_tImageEnv.lFileDescriptor = open(
        AGENTPET_IMAGE_TEMP_PATH,
        O_CREAT | O_RDWR | O_TRUNC | O_BINARY,
        0);
    if (0 > l_tImageEnv.lFileDescriptor)
    {
        return AGENTPET_IMAGE_ERROR_STORAGE;
    }

    l_tImageEnv.ulTotal = ulTotal;
    l_tImageEnv.ulReceived = 0U;
    l_tImageEnv.ulExpectedCrc = ulExpectedCrc;
    l_tImageEnv.ulCalculatedCrc = AGENTPET_IMAGE_CRC32_INIT;
    l_tImageEnv.usPendingLength = 0U;
    l_tImageEnv.eState = AGENTPET_IMAGE_RECEIVING;
    l_tImageEnv.eLastResult = AGENTPET_IMAGE_RESULT_ACCEPTED;

    return AGENTPET_IMAGE_RESULT_ACCEPTED;
}

static AGENTPET_IMAGE_RESULT Local_AppendData(
    uint32_t ulOffset,
    const uint8_t *pData,
    uint8_t ucLength)
{
    uint16_t usAvailable;
    uint16_t usCopyLength;
    uint8_t ucOffset;
    AGENTPET_IMAGE_RESULT eResult;

    if (
        (AGENTPET_IMAGE_RECEIVING != l_tImageEnv.eState) ||
        (NULL == pData) ||
        (0U == ucLength) ||
        (AGENTPET_IMAGE_DATA_SIZE < ucLength)
    )
    {
        return AGENTPET_IMAGE_ERROR_STATE;
    }
    if ((l_tImageEnv.ulReceived != ulOffset) ||
        (l_tImageEnv.ulTotal < l_tImageEnv.ulReceived + ucLength))
    {
        return AGENTPET_IMAGE_ERROR_OFFSET;
    }

    l_tImageEnv.ulCalculatedCrc = Local_Crc32Mpeg2(
        pData,
        ucLength,
        l_tImageEnv.ulCalculatedCrc);
    ucOffset = 0U;
    while (ucOffset < ucLength)
    {
        usAvailable = AGENTPET_IMAGE_WRITE_BUFFER_SIZE -
            l_tImageEnv.usPendingLength;
        usCopyLength = (uint16_t)ucLength - ucOffset;
        if (usAvailable < usCopyLength)
        {
            usCopyLength = usAvailable;
        }
        (void)memcpy(
            &l_tImageEnv.aWriteBuffer[l_tImageEnv.usPendingLength],
            &pData[ucOffset],
            usCopyLength);
        l_tImageEnv.usPendingLength += usCopyLength;
        ucOffset += (uint8_t)usCopyLength;
        if (AGENTPET_IMAGE_WRITE_BUFFER_SIZE == l_tImageEnv.usPendingLength)
        {
            eResult = Local_FlushWriteBuffer();
            if (AGENTPET_IMAGE_RESULT_ACCEPTED != eResult)
            {
                return eResult;
            }
        }
    }
    l_tImageEnv.ulReceived += ucLength;

    return AGENTPET_IMAGE_RESULT_ACCEPTED;
}

static AGENTPET_IMAGE_RESULT Local_CommitTransfer(
    uint32_t ulTotal,
    uint32_t ulExpectedCrc,
    uint8_t ucFormat)
{
    AGENTPET_IMAGE_RESULT eResult;

    if (
        (AGENTPET_IMAGE_RECEIVING != l_tImageEnv.eState) ||
        (AGENTPET_IMAGE_FORMAT_JPEG != ucFormat) ||
        (l_tImageEnv.ulTotal != ulTotal) ||
        (l_tImageEnv.ulReceived != ulTotal) ||
        (l_tImageEnv.ulExpectedCrc != ulExpectedCrc) ||
        (l_tImageEnv.ulCalculatedCrc != ulExpectedCrc)
    )
    {
        return AGENTPET_IMAGE_ERROR_CRC;
    }

    eResult = Local_FlushWriteBuffer();
    if (AGENTPET_IMAGE_RESULT_ACCEPTED != eResult)
    {
        return eResult;
    }
    if (0 != fsync(l_tImageEnv.lFileDescriptor))
    {
        return AGENTPET_IMAGE_ERROR_STORAGE;
    }
    Local_CloseTemporaryFile();
    eResult = Local_ValidateJpeg();
    if (AGENTPET_IMAGE_RESULT_ACCEPTED != eResult)
    {
        return eResult;
    }

    (void)unlink(AGENTPET_IMAGE_BACKUP_PATH);
    if ((0 == access(AGENTPET_IMAGE_PATH, 0)) &&
        (0 != rename(AGENTPET_IMAGE_PATH, AGENTPET_IMAGE_BACKUP_PATH)))
    {
        return AGENTPET_IMAGE_ERROR_STORAGE;
    }
    if (0 != rename(AGENTPET_IMAGE_TEMP_PATH, AGENTPET_IMAGE_PATH))
    {
        (void)rename(AGENTPET_IMAGE_BACKUP_PATH, AGENTPET_IMAGE_PATH);
        return AGENTPET_IMAGE_ERROR_STORAGE;
    }
    (void)unlink(AGENTPET_IMAGE_BACKUP_PATH);

    l_tImageEnv.bImageAvailable = true;
    l_tImageEnv.ulGeneration++;
    l_tImageEnv.eState = AGENTPET_IMAGE_READY;
    l_tImageEnv.eLastResult = AGENTPET_IMAGE_RESULT_COMMITTED;
    LOG_I("Custom mascot committed size=%lu crc=0x%08lx",
          (unsigned long)ulTotal,
          (unsigned long)ulExpectedCrc);

    return AGENTPET_IMAGE_RESULT_COMMITTED;
}

static AGENTPET_IMAGE_RESULT Local_ResetImage(void)
{
    int lResult;

    Local_CloseTemporaryFile();
    lResult = 0;
    if ((0 == access(AGENTPET_IMAGE_TEMP_PATH, 0)) &&
        (0 != unlink(AGENTPET_IMAGE_TEMP_PATH)))
    {
        lResult = -1;
    }
    if ((0 == access(AGENTPET_IMAGE_BACKUP_PATH, 0)) &&
        (0 != unlink(AGENTPET_IMAGE_BACKUP_PATH)))
    {
        lResult = -1;
    }
    if ((0 == access(AGENTPET_IMAGE_PATH, 0)) &&
        (0 != unlink(AGENTPET_IMAGE_PATH)))
    {
        lResult = -1;
    }
    if (0 != lResult)
    {
        return AGENTPET_IMAGE_ERROR_STORAGE;
    }

    l_tImageEnv.ulTotal = 0U;
    l_tImageEnv.ulReceived = 0U;
    l_tImageEnv.usPendingLength = 0U;
    l_tImageEnv.bImageAvailable = false;
    l_tImageEnv.ulGeneration++;
    l_tImageEnv.eState = AGENTPET_IMAGE_IDLE;
    l_tImageEnv.eLastResult = AGENTPET_IMAGE_RESULT_RESET;
    LOG_I("Custom mascot reset to built-in image");

    return AGENTPET_IMAGE_RESULT_RESET;
}

static void Local_ImageWorker(void *pParameter)
{
    uint8_t aFrame[AGENTPET_IMAGE_FRAME_SIZE];

    (void)pParameter;
    while (true)
    {
        AGENTPET_IMAGE_RESULT eResult;
        rt_err_t tResult;

        tResult = rt_mq_recv(
            &l_tImageQueue,
            aFrame,
            sizeof(aFrame),
            RT_WAITING_FOREVER);
        if (RT_EOK != tResult)
        {
            continue;
        }

        (void)rt_mutex_take(&l_tImageMutex, RT_WAITING_FOREVER);
        eResult = AGENTPETIMAGE_ProcessFrame(aFrame, sizeof(aFrame));
        (void)rt_mutex_release(&l_tImageMutex);
        if (AGENTPET_IMAGE_ERROR_INVALID_PARAMETER <= eResult)
        {
            LOG_E("Custom mascot worker rejected frame result=%d", eResult);
        }
    }
}
/*
 * AGENTPETIMAGE_Init
 * Function: recover the last committed custom mascot and discard interrupted temporary data.
 * Parameters: none.
 * Return: none.
 */
void AGENTPETIMAGE_Init(void)
{
    struct stat tStatus;
    rt_err_t tResult;

    if (l_bImageWorkerReady)
    {
        return;
    }

    Local_CloseTemporaryFile();
    if ((0 != stat(AGENTPET_IMAGE_PATH, &tStatus)) &&
        (0 == stat(AGENTPET_IMAGE_BACKUP_PATH, &tStatus)))
    {
        (void)rename(AGENTPET_IMAGE_BACKUP_PATH, AGENTPET_IMAGE_PATH);
    }
    (void)unlink(AGENTPET_IMAGE_TEMP_PATH);
    l_tImageEnv.ulTotal = 0U;
    l_tImageEnv.ulReceived = 0U;
    l_tImageEnv.ulExpectedCrc = 0U;
    l_tImageEnv.ulCalculatedCrc = AGENTPET_IMAGE_CRC32_INIT;
    l_tImageEnv.usPendingLength = 0U;
    l_tImageEnv.bImageAvailable = (0 == stat(AGENTPET_IMAGE_PATH, &tStatus));
    l_tImageEnv.ulGeneration = l_tImageEnv.bImageAvailable ? 1U : 0U;
    l_tImageEnv.eState = l_tImageEnv.bImageAvailable ?
        AGENTPET_IMAGE_READY : AGENTPET_IMAGE_IDLE;
    l_tImageEnv.eLastResult = AGENTPET_IMAGE_RESULT_ACCEPTED;

    tResult = rt_mutex_init(&l_tImageMutex, "pet_img", RT_IPC_FLAG_PRIO);
    if (RT_EOK != tResult)
    {
        LOG_E("Custom mascot mutex init failed result=%d", tResult);
        return;
    }
    tResult = rt_mq_init(
        &l_tImageQueue,
        "pet_img_q",
        l_aImageQueuePool,
        AGENTPET_IMAGE_FRAME_SIZE,
        sizeof(l_aImageQueuePool),
        RT_IPC_FLAG_FIFO);
    if (RT_EOK != tResult)
    {
        LOG_E("Custom mascot queue init failed result=%d", tResult);
        (void)rt_mutex_detach(&l_tImageMutex);
        return;
    }
    tResult = rt_thread_init(
        &l_tImageThread,
        "pet_img",
        Local_ImageWorker,
        NULL,
        l_aImageThreadStack,
        sizeof(l_aImageThreadStack),
        RT_THREAD_PRIORITY_MIDDLE + 3U,
        AGENTPET_IMAGE_THREAD_TIME_SLICE);
    if (RT_EOK != tResult)
    {
        LOG_E("Custom mascot worker init failed result=%d", tResult);
        (void)rt_mq_detach(&l_tImageQueue);
        (void)rt_mutex_detach(&l_tImageMutex);
        return;
    }
    tResult = rt_thread_startup(&l_tImageThread);
    if (RT_EOK != tResult)
    {
        LOG_E("Custom mascot worker start failed result=%d", tResult);
        return;
    }
    l_bImageWorkerReady = true;

    return;
}

/*
 * AGENTPETIMAGE_QueueFrame
 * Function: copy one fixed frame into the bounded worker queue without blocking the GATT callback.
 * Parameters:
 *   - pFrame: read-only 20-byte image frame.
 *   - ulLength: frame length, must equal 20 bytes.
 * Return: true when queued; false for invalid input, unavailable worker, or queue exhaustion.
 */
bool AGENTPETIMAGE_QueueFrame(const uint8_t *pFrame, size_t ulLength)
{
    if (
        (!l_bImageWorkerReady) ||
        (NULL == pFrame) ||
        (AGENTPET_IMAGE_FRAME_SIZE != ulLength)
    )
    {
        return false;
    }

    return (RT_EOK == rt_mq_send(
        &l_tImageQueue,
        (void *)pFrame,
        AGENTPET_IMAGE_FRAME_SIZE));
}

/*
 * AGENTPETIMAGE_ResetTransfer
 * Function: discard only an incomplete transfer while preserving the last committed image.
 * Parameters: none.
 * Return: none.
 */
void AGENTPETIMAGE_ResetTransfer(void)
{
    if (l_bImageWorkerReady)
    {
        (void)rt_mq_control(&l_tImageQueue, RT_IPC_CMD_RESET, NULL);
        (void)rt_mutex_take(&l_tImageMutex, RT_WAITING_FOREVER);
    }
    if (AGENTPET_IMAGE_RECEIVING == l_tImageEnv.eState)
    {
        Local_CloseTemporaryFile();
        (void)unlink(AGENTPET_IMAGE_TEMP_PATH);
        l_tImageEnv.ulTotal = 0U;
        l_tImageEnv.ulReceived = 0U;
        l_tImageEnv.usPendingLength = 0U;
        l_tImageEnv.eState = l_tImageEnv.bImageAvailable ?
            AGENTPET_IMAGE_READY : AGENTPET_IMAGE_IDLE;
        l_tImageEnv.eLastResult = AGENTPET_IMAGE_RESULT_ACCEPTED;
    }
    if (l_bImageWorkerReady)
    {
        (void)rt_mutex_release(&l_tImageMutex);
    }

    return;
}

/*
 * AGENTPETIMAGE_ProcessFrame
 * Function: validate and process one fixed 20-byte custom mascot transfer frame.
 * Parameters:
 *   - pFrame: read-only frame data.
 *   - ulLength: frame length, must equal 20 bytes.
 * Return: accepted/committed/reset or a bounded protocol/storage error.
 */
AGENTPET_IMAGE_RESULT AGENTPETIMAGE_ProcessFrame(
    const uint8_t *pFrame,
    size_t ulLength)
{
    AGENTPET_IMAGE_RESULT eResult;
    uint32_t ulValue;
    uint32_t ulCrc;
    uint8_t ucCommand;
    uint8_t ucIndex;
    uint8_t ucPayloadLength;

    if (NULL == pFrame)
    {
        return AGENTPET_IMAGE_ERROR_INVALID_PARAMETER;
    }
    if (
        (AGENTPET_IMAGE_FRAME_SIZE != ulLength) ||
        (AGENTPET_IMAGE_MAGIC_FIRST != pFrame[0]) ||
        (AGENTPET_IMAGE_MAGIC_SECOND != pFrame[1]) ||
        (AGENTPET_IMAGE_PROTOCOL_VERSION != pFrame[2])
    )
    {
        return AGENTPET_IMAGE_ERROR_FRAME;
    }
    if (pFrame[AGENTPET_IMAGE_CRC_OFFSET] !=
        AGENTPET_Crc8Atm(pFrame, AGENTPET_IMAGE_CRC_OFFSET))
    {
        return AGENTPET_IMAGE_ERROR_CRC;
    }

    ucCommand = pFrame[3];
    ulValue = Local_ReadLe24(&pFrame[4]);
    ulCrc = Local_ReadLe32(&pFrame[8]);
    eResult = AGENTPET_IMAGE_ERROR_FRAME;
    if (AGENTPET_IMAGE_COMMAND_BEGIN == ucCommand)
    {
        for (ucIndex = 12U; ucIndex < AGENTPET_IMAGE_CRC_OFFSET; ucIndex++)
        {
            if (0U != pFrame[ucIndex])
            {
                return AGENTPET_IMAGE_ERROR_FRAME;
            }
        }
        eResult = Local_BeginTransfer(ulValue, ulCrc, pFrame[7]);
    }
    else if (AGENTPET_IMAGE_COMMAND_DATA == ucCommand)
    {
        ucPayloadLength = pFrame[7];
        if ((0U == ucPayloadLength) ||
            (AGENTPET_IMAGE_DATA_SIZE < ucPayloadLength))
        {
            return AGENTPET_IMAGE_ERROR_SIZE;
        }
        for (ucIndex = (uint8_t)(AGENTPET_IMAGE_DATA_OFFSET + ucPayloadLength);
             ucIndex < AGENTPET_IMAGE_CRC_OFFSET;
             ucIndex++)
        {
            if (0U != pFrame[ucIndex])
            {
                return AGENTPET_IMAGE_ERROR_FRAME;
            }
        }
        eResult = Local_AppendData(
            ulValue,
            &pFrame[AGENTPET_IMAGE_DATA_OFFSET],
            ucPayloadLength);
    }
    else if (AGENTPET_IMAGE_COMMAND_COMMIT == ucCommand)
    {
        for (ucIndex = 12U; ucIndex < AGENTPET_IMAGE_CRC_OFFSET; ucIndex++)
        {
            if (0U != pFrame[ucIndex])
            {
                return AGENTPET_IMAGE_ERROR_FRAME;
            }
        }
        eResult = Local_CommitTransfer(ulValue, ulCrc, pFrame[7]);
    }
    else if (AGENTPET_IMAGE_COMMAND_RESET == ucCommand)
    {
        for (ucIndex = 4U; ucIndex < AGENTPET_IMAGE_CRC_OFFSET; ucIndex++)
        {
            if (0U != pFrame[ucIndex])
            {
                return AGENTPET_IMAGE_ERROR_FRAME;
            }
        }
        eResult = Local_ResetImage();
    }
    else
    {
        eResult = AGENTPET_IMAGE_ERROR_FRAME;
    }

    if ((AGENTPET_IMAGE_ERROR_INVALID_PARAMETER <= eResult) &&
        (AGENTPET_IMAGE_RECEIVING == l_tImageEnv.eState))
    {
        Local_FailTransfer(eResult);
    }
    else
    {
        l_tImageEnv.eLastResult = eResult;
    }

    return eResult;
}

/*
 * AGENTPETIMAGE_GetStatus
 * Function: copy persistent-image and transfer progress for BLE diagnostics and LVGL refresh.
 * Parameters:
 *   - pStatus: output status; must not be NULL.
 * Return: true when copied, otherwise false.
 */
bool AGENTPETIMAGE_GetStatus(AGENTPET_IMAGE_STATUS *pStatus)
{
    if (NULL == pStatus)
    {
        return false;
    }

    if (l_bImageWorkerReady)
    {
        (void)rt_mutex_take(&l_tImageMutex, RT_WAITING_FOREVER);
    }
    pStatus->eState = l_tImageEnv.eState;
    pStatus->bImageAvailable = l_tImageEnv.bImageAvailable;
    pStatus->ulReceived = l_tImageEnv.ulReceived;
    pStatus->ulTotal = l_tImageEnv.ulTotal;
    pStatus->ulGeneration = l_tImageEnv.ulGeneration;
    pStatus->eLastResult = l_tImageEnv.eLastResult;
    if (l_bImageWorkerReady)
    {
        (void)rt_mutex_release(&l_tImageMutex);
    }

    return true;
}
