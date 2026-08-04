#include "agent_pet_image_transfer.h"

#include <fcntl.h>
#include <rtthread.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "agent_pet_protocol.h"
#include "dfs_posix.h"
#include "mbedtls/md5.h"

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
#define AGENTPET_IMAGE_WIDTH            (336U)
#define AGENTPET_IMAGE_HEIGHT           (336U)
#define AGENTPET_IMAGE_DATA_OFFSET       (8U)
#define AGENTPET_IMAGE_DATA_MAX_SIZE      (235U)
#define AGENTPET_IMAGE_PACKET_OVERHEAD    (9U)
#define AGENTPET_IMAGE_WRITE_BUFFER_SIZE  (512U)
#define AGENTPET_IMAGE_MD5_READ_SIZE       (512U)
#define AGENTPET_IMAGE_CRC32_INIT         (0xFFFFFFFFUL)
#define AGENTPET_IMAGE_QUEUE_DEPTH        (8U)
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
    uint8_t aImageMd5[AGENTPET_IMAGE_MD5_SIZE];
    uint8_t aWriteBuffer[AGENTPET_IMAGE_WRITE_BUFFER_SIZE];
} AGENTPET_IMAGE_ENV;

/* AGENTPET_IMAGE_PACKET: one variable-length GATT write copied into the worker queue.
 * Members:
 *   - usLength: validated packet length, range 9..244 bytes
 *   - aData: fixed-capacity packet storage; only bytes below usLength are processed
 */
typedef struct _AGENTPET_IMAGE_PACKET
{
    uint16_t usLength;
    uint8_t aData[AGENTPET_IMAGE_MAX_PACKET_SIZE];
} AGENTPET_IMAGE_PACKET;

/* Module-local transfer state. The worker serializes filesystem writes while LVGL
 * reads copied status snapshots under the module mutex. */
/* Static RTOS resources keep GATT writes bounded and avoid heap fragmentation.
 * Queue capacity: 8 packets (up to 1952 payload bytes); worker stack: 2048 bytes. */
static struct rt_messagequeue l_tImageQueue;
static struct rt_mutex l_tImageMutex;
static struct rt_thread l_tImageThread;
static uint8_t l_aImageThreadStack[AGENTPET_IMAGE_THREAD_STACK_SIZE];
static uint8_t l_aImageQueuePool[
    (RT_ALIGN(sizeof(AGENTPET_IMAGE_PACKET), RT_ALIGN_SIZE) +
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

/*
 * Local_CalculateFileMd5
 * Function: calculate the MD5 digest of one bounded persistent image file.
 * Parameters:
 *   - pPath: input file path; must not be NULL.
 *   - pDigest: 16-byte output buffer; must not be NULL.
 * Return: true when the complete file was hashed, otherwise false.
 */
static bool Local_CalculateFileMd5(
    const char *pPath,
    uint8_t *pDigest)
{
    mbedtls_md5_context tContext;
    uint8_t aReadBuffer[AGENTPET_IMAGE_MD5_READ_SIZE];
    int lFileDescriptor;
    int lReadLength;
    bool bSuccess;

    if ((NULL == pPath) || (NULL == pDigest))
    {
        return false;
    }

    lFileDescriptor = open(pPath, O_RDONLY | O_BINARY, 0);
    if (0 > lFileDescriptor)
    {
        return false;
    }

    bSuccess = true;
    mbedtls_md5_init(&tContext);
    mbedtls_md5_starts(&tContext);
    while (true)
    {
        lReadLength = read(lFileDescriptor, aReadBuffer, sizeof(aReadBuffer));
        if (0 > lReadLength)
        {
            bSuccess = false;
            break;
        }
        if (0 == lReadLength)
        {
            break;
        }
        mbedtls_md5_update(&tContext, aReadBuffer, (size_t)lReadLength);
    }
    (void)close(lFileDescriptor);
    if (bSuccess)
    {
        mbedtls_md5_finish(&tContext, pDigest);
    }
    else
    {
        (void)memset(pDigest, 0, AGENTPET_IMAGE_MD5_SIZE);
    }
    mbedtls_md5_free(&tContext);

    return bSuccess;
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

static bool Local_ReadExact(
    int lFileDescriptor,
    uint8_t *pData,
    uint16_t usLength)
{
    uint16_t usOffset;

    if ((0 > lFileDescriptor) || (NULL == pData) || (0U == usLength))
    {
        return false;
    }

    usOffset = 0U;
    while (usOffset < usLength)
    {
        int lReadLength;

        lReadLength = read(
            lFileDescriptor,
            &pData[usOffset],
            (uint16_t)(usLength - usOffset));
        if (0 >= lReadLength)
        {
            return false;
        }
        usOffset += (uint16_t)lReadLength;
    }

    return true;
}

static uint16_t Local_ReadBigEndian16(const uint8_t *pData)
{
    if (NULL == pData)
    {
        return 0U;
    }

    return (uint16_t)(((uint16_t)pData[0] << 8U) | pData[1]);
}

static bool Local_IsStartOfFrameMarker(uint8_t ucMarker)
{
    return (
        (0xC0U <= ucMarker) &&
        (0xCFU >= ucMarker) &&
        (0xC4U != ucMarker) &&
        (0xC8U != ucMarker) &&
        (0xCCU != ucMarker)
    );
}

static AGENTPET_IMAGE_RESULT Local_ValidateJpeg(const char *pPath)
{
    struct stat tStatus;
    uint8_t aMarker[2];
    uint8_t aLength[2];
    uint8_t aSofPayload[15];
    int lFileDescriptor;
    bool bFoundFrame;
    AGENTPET_IMAGE_RESULT eResult;

    if (NULL == pPath)
    {
        return AGENTPET_IMAGE_ERROR_INVALID_PARAMETER;
    }
    if ((0 != stat(pPath, &tStatus)) ||
        (32 > tStatus.st_size) ||
        (AGENTPET_IMAGE_MAX_FILE_SIZE < (uint32_t)tStatus.st_size))
    {
        return AGENTPET_IMAGE_ERROR_SIZE;
    }

    lFileDescriptor = open(pPath, O_RDONLY | O_BINARY, 0);
    if (0 > lFileDescriptor)
    {
        return AGENTPET_IMAGE_ERROR_STORAGE;
    }

    eResult = AGENTPET_IMAGE_ERROR_FORMAT;
    bFoundFrame = false;
    if (!Local_ReadExact(lFileDescriptor, aMarker, sizeof(aMarker)) ||
        (0xFFU != aMarker[0]) ||
        (0xD8U != aMarker[1]))
    {
        goto cleanup;
    }
    if (0 > lseek(lFileDescriptor, -2, SEEK_END))
    {
        eResult = AGENTPET_IMAGE_ERROR_STORAGE;
        goto cleanup;
    }
    if (!Local_ReadExact(lFileDescriptor, aMarker, sizeof(aMarker)) ||
        (0xFFU != aMarker[0]) ||
        (0xD9U != aMarker[1]))
    {
        goto cleanup;
    }
    if (0 > lseek(lFileDescriptor, 2, SEEK_SET))
    {
        eResult = AGENTPET_IMAGE_ERROR_STORAGE;
        goto cleanup;
    }

    while (true)
    {
        uint16_t usSegmentLength;
        uint16_t usPayloadLength;
        uint8_t ucMarker;
        off_t tCurrentOffset;

        if (!Local_ReadExact(lFileDescriptor, aMarker, 1U) ||
            (0xFFU != aMarker[0]))
        {
            break;
        }
        do
        {
            if (!Local_ReadExact(lFileDescriptor, &ucMarker, 1U))
            {
                goto cleanup;
            }
        } while (0xFFU == ucMarker);

        if ((0x00U == ucMarker) || (0xD9U == ucMarker))
        {
            break;
        }
        if ((0xD8U == ucMarker) || (0x01U == ucMarker) ||
            ((0xD0U <= ucMarker) && (0xD7U >= ucMarker)))
        {
            continue;
        }
        if (!Local_ReadExact(lFileDescriptor, aLength, sizeof(aLength)))
        {
            break;
        }
        usSegmentLength = Local_ReadBigEndian16(aLength);
        if (2U > usSegmentLength)
        {
            break;
        }
        usPayloadLength = (uint16_t)(usSegmentLength - 2U);
        tCurrentOffset = lseek(lFileDescriptor, 0, SEEK_CUR);
        if ((0 > tCurrentOffset) ||
            ((off_t)usPayloadLength > tStatus.st_size - tCurrentOffset))
        {
            break;
        }

        if (0xC0U == ucMarker)
        {
            if (bFoundFrame ||
                (sizeof(aSofPayload) != usPayloadLength) ||
                !Local_ReadExact(
                    lFileDescriptor,
                    aSofPayload,
                    sizeof(aSofPayload)))
            {
                break;
            }
            if ((8U != aSofPayload[0]) ||
                (AGENTPET_IMAGE_HEIGHT != Local_ReadBigEndian16(&aSofPayload[1])) ||
                (AGENTPET_IMAGE_WIDTH != Local_ReadBigEndian16(&aSofPayload[3])) ||
                (3U != aSofPayload[5]) ||
                (0x22U != aSofPayload[7]) ||
                (0x11U != aSofPayload[10]) ||
                (0x11U != aSofPayload[13]))
            {
                break;
            }
            bFoundFrame = true;
            continue;
        }
        if (Local_IsStartOfFrameMarker(ucMarker))
        {
            break;
        }
        if (0xDAU == ucMarker)
        {
            if (bFoundFrame)
            {
                eResult = AGENTPET_IMAGE_RESULT_ACCEPTED;
            }
            break;
        }
        if (0 > lseek(lFileDescriptor, usPayloadLength, SEEK_CUR))
        {
            eResult = AGENTPET_IMAGE_ERROR_STORAGE;
            break;
        }
    }

cleanup:
    (void)close(lFileDescriptor);
    return eResult;
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
        (AGENTPET_IMAGE_DATA_MAX_SIZE < ucLength)
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
    uint8_t aImageMd5[AGENTPET_IMAGE_MD5_SIZE];

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
    eResult = Local_ValidateJpeg(AGENTPET_IMAGE_TEMP_PATH);
    if (AGENTPET_IMAGE_RESULT_ACCEPTED != eResult)
    {
        return eResult;
    }
    if (!Local_CalculateFileMd5(AGENTPET_IMAGE_TEMP_PATH, aImageMd5))
    {
        return AGENTPET_IMAGE_ERROR_STORAGE;
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

    (void)memcpy(l_tImageEnv.aImageMd5, aImageMd5, AGENTPET_IMAGE_MD5_SIZE);
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
    (void)memset(l_tImageEnv.aImageMd5, 0, AGENTPET_IMAGE_MD5_SIZE);
    l_tImageEnv.ulGeneration++;
    l_tImageEnv.eState = AGENTPET_IMAGE_IDLE;
    l_tImageEnv.eLastResult = AGENTPET_IMAGE_RESULT_RESET;
    LOG_I("Custom mascot reset to built-in image");

    return AGENTPET_IMAGE_RESULT_RESET;
}

static void Local_ImageWorker(void *pParameter)
{
    AGENTPET_IMAGE_PACKET tPacket;

    (void)pParameter;
    while (true)
    {
        AGENTPET_IMAGE_RESULT eResult;
        rt_err_t tResult;

        tResult = rt_mq_recv(
            &l_tImageQueue,
            &tPacket,
            sizeof(tPacket),
            RT_WAITING_FOREVER);
        if (RT_EOK != tResult)
        {
            continue;
        }

        (void)rt_mutex_take(&l_tImageMutex, RT_WAITING_FOREVER);
        eResult = AGENTPETIMAGE_ProcessFrame(tPacket.aData, tPacket.usLength);
        (void)rt_mutex_release(&l_tImageMutex);
        if (AGENTPET_IMAGE_ERROR_INVALID_PARAMETER <= eResult)
        {
            LOG_E("Custom mascot worker rejected packet result=%d", eResult);
        }
    }

    return;
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
    if (l_tImageEnv.bImageAvailable &&
        (AGENTPET_IMAGE_RESULT_ACCEPTED !=
         Local_ValidateJpeg(AGENTPET_IMAGE_PATH)))
    {
        LOG_E("Persistent mascot JPEG is unsafe; removing it");
        (void)unlink(AGENTPET_IMAGE_PATH);
        l_tImageEnv.bImageAvailable = false;
    }
    (void)memset(l_tImageEnv.aImageMd5, 0, AGENTPET_IMAGE_MD5_SIZE);
    if (l_tImageEnv.bImageAvailable &&
        !Local_CalculateFileMd5(AGENTPET_IMAGE_PATH, l_tImageEnv.aImageMd5))
    {
        LOG_E("Persistent mascot MD5 calculation failed");
    }
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
        sizeof(AGENTPET_IMAGE_PACKET),
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
 * Function: copy one variable-length packet into the bounded worker queue without blocking the GATT callback.
 * Parameters:
 *   - pFrame: read-only image packet, 9..244 bytes.
 *   - ulLength: actual packet length, 9..244 bytes.
 * Return: true when queued; false for invalid input, unavailable worker, or queue exhaustion.
 */
bool AGENTPETIMAGE_QueueFrame(const uint8_t *pFrame, size_t ulLength)
{
    AGENTPET_IMAGE_PACKET tPacket;

    if (
        (!l_bImageWorkerReady) ||
        (NULL == pFrame) ||
        (AGENTPET_IMAGE_PACKET_OVERHEAD > ulLength) ||
        (AGENTPET_IMAGE_MAX_PACKET_SIZE < ulLength)
    )
    {
        return false;
    }

    (void)memset(&tPacket, 0, sizeof(tPacket));
    tPacket.usLength = (uint16_t)ulLength;
    (void)memcpy(tPacket.aData, pFrame, ulLength);

    return (RT_EOK == rt_mq_send(
        &l_tImageQueue,
        &tPacket,
        sizeof(tPacket)));
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
 * Function: validate and process one variable-length custom mascot transfer packet.
 * Parameters:
 *   - pFrame: read-only frame data.
 *   - ulLength: actual packet length, 9..244 bytes.
 * Return: accepted/committed/reset or a bounded protocol/storage error.
 */
AGENTPET_IMAGE_RESULT AGENTPETIMAGE_ProcessFrame(
    const uint8_t *pFrame,
    size_t ulLength)
{
    AGENTPET_IMAGE_RESULT eResult;
    uint32_t ulValue;
    uint32_t ulCrc;
    size_t ulCrcOffset;
    uint8_t ucCommand;
    uint8_t ucIndex;
    uint8_t ucPayloadLength;

    if (NULL == pFrame)
    {
        return AGENTPET_IMAGE_ERROR_INVALID_PARAMETER;
    }
    if (
        (AGENTPET_IMAGE_PACKET_OVERHEAD > ulLength) ||
        (AGENTPET_IMAGE_MAX_PACKET_SIZE < ulLength) ||
        (AGENTPET_IMAGE_MAGIC_FIRST != pFrame[0]) ||
        (AGENTPET_IMAGE_MAGIC_SECOND != pFrame[1]) ||
        (AGENTPET_IMAGE_PROTOCOL_VERSION != pFrame[2])
    )
    {
        return AGENTPET_IMAGE_ERROR_FRAME;
    }

    ulCrcOffset = ulLength - 1U;
    if (pFrame[ulCrcOffset] != AGENTPET_Crc8Atm(pFrame, ulCrcOffset))
    {
        return AGENTPET_IMAGE_ERROR_CRC;
    }

    ucCommand = pFrame[3];
    ulValue = Local_ReadLe24(&pFrame[4]);
    ulCrc = 0U;
    eResult = AGENTPET_IMAGE_ERROR_FRAME;
    if ((AGENTPET_IMAGE_COMMAND_BEGIN == ucCommand) ||
        (AGENTPET_IMAGE_COMMAND_COMMIT == ucCommand))
    {
        if (AGENTPET_IMAGE_CONTROL_FRAME_SIZE != ulLength)
        {
            return AGENTPET_IMAGE_ERROR_FRAME;
        }
        for (ucIndex = 12U; ucIndex < AGENTPET_IMAGE_CONTROL_FRAME_SIZE - 1U; ucIndex++)
        {
            if (0U != pFrame[ucIndex])
            {
                return AGENTPET_IMAGE_ERROR_FRAME;
            }
        }
        ulCrc = Local_ReadLe32(&pFrame[8]);
        eResult = (AGENTPET_IMAGE_COMMAND_BEGIN == ucCommand) ?
            Local_BeginTransfer(ulValue, ulCrc, pFrame[7]) :
            Local_CommitTransfer(ulValue, ulCrc, pFrame[7]);
    }
    else if (AGENTPET_IMAGE_COMMAND_DATA == ucCommand)
    {
        ucPayloadLength = pFrame[7];
        if ((0U == ucPayloadLength) ||
            (AGENTPET_IMAGE_DATA_MAX_SIZE < ucPayloadLength))
        {
            return AGENTPET_IMAGE_ERROR_SIZE;
        }
        if ((size_t)(AGENTPET_IMAGE_PACKET_OVERHEAD + ucPayloadLength) != ulLength)
        {
            if ((AGENTPET_IMAGE_CONTROL_FRAME_SIZE != ulLength) ||
                (11U < ucPayloadLength))
            {
                return AGENTPET_IMAGE_ERROR_SIZE;
            }
            for (ucIndex = (uint8_t)(AGENTPET_IMAGE_DATA_OFFSET + ucPayloadLength);
                 ucIndex < AGENTPET_IMAGE_CONTROL_FRAME_SIZE - 1U;
                 ucIndex++)
            {
                if (0U != pFrame[ucIndex])
                {
                    return AGENTPET_IMAGE_ERROR_FRAME;
                }
            }
        }
        eResult = Local_AppendData(
            ulValue,
            &pFrame[AGENTPET_IMAGE_DATA_OFFSET],
            ucPayloadLength);
    }
    else if (AGENTPET_IMAGE_COMMAND_RESET == ucCommand)
    {
        if (AGENTPET_IMAGE_CONTROL_FRAME_SIZE != ulLength)
        {
            return AGENTPET_IMAGE_ERROR_FRAME;
        }
        for (ucIndex = 4U; ucIndex < AGENTPET_IMAGE_CONTROL_FRAME_SIZE - 1U; ucIndex++)
        {
            if (0U != pFrame[ucIndex])
            {
                return AGENTPET_IMAGE_ERROR_FRAME;
            }
        }
        eResult = Local_ResetImage();
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
/*
 * AGENTPETIMAGE_GetDigest
 * Function: copy the availability flag and cached MD5 of the committed persistent image.
 * Parameters:
 *   - pAvailable: output availability flag; must not be NULL.
 *   - pDigest: 16-byte output digest buffer; must not be NULL.
 * Return: true when copied, otherwise false.
 */
bool AGENTPETIMAGE_GetDigest(bool *pAvailable, uint8_t *pDigest)
{
    if ((NULL == pAvailable) || (NULL == pDigest))
    {
        return false;
    }

    if (l_bImageWorkerReady)
    {
        (void)rt_mutex_take(&l_tImageMutex, RT_WAITING_FOREVER);
    }
    *pAvailable = l_tImageEnv.bImageAvailable;
    (void)memcpy(pDigest, l_tImageEnv.aImageMd5, AGENTPET_IMAGE_MD5_SIZE);
    if (l_bImageWorkerReady)
    {
        (void)rt_mutex_release(&l_tImageMutex);
    }

    return true;
}
