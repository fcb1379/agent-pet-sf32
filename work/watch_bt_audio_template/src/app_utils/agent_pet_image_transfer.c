#include "agent_pet_image_transfer.h"

#include <fcntl.h>
#include <rthw.h>
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
#define AGENTPET_IMAGE_COMMAND_SELECT   (5U)
#define AGENTPET_IMAGE_SLOT_PROTOCOL_VERSION (2U)
#define AGENTPET_IMAGE_WIDTH            (192U)
#define AGENTPET_IMAGE_HEIGHT           (192U)
#define AGENTPET_IMAGE_GIF_MAX_DIMENSION (192U)
#define AGENTPET_IMAGE_GIF_HEADER_SIZE  (13U)
#define AGENTPET_IMAGE_GIF_MAX_FRAMES   (60U)
#define AGENTPET_IMAGE_DATA_OFFSET       (8U)
#define AGENTPET_IMAGE_DATA_MAX_SIZE      (235U)
#define AGENTPET_IMAGE_PACKET_OVERHEAD    (9U)
#define AGENTPET_IMAGE_WRITE_BUFFER_SIZE  (4096U)
#define AGENTPET_IMAGE_MD5_READ_SIZE       (512U)
#define AGENTPET_IMAGE_CRC32_INIT         (0xFFFFFFFFUL)
#define AGENTPET_IMAGE_QUEUE_DEPTH        (24U)
#define AGENTPET_IMAGE_QUEUE_RETRY_COUNT  (100U)
#define AGENTPET_IMAGE_QUEUE_RETRY_MS     (2U)
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
 *   - ucFormat: committed or in-progress wire image format, JPEG or GIF
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
    uint8_t ucFormat;
    uint8_t ucSlot;
    bool bImageAvailable;
    uint8_t aImageMd5[AGENTPET_IMAGE_MD5_SIZE];
    uint8_t aWriteBuffer[AGENTPET_IMAGE_WRITE_BUFFER_SIZE];
} AGENTPET_IMAGE_ENV;

/* AGENTPET_IMAGE_SLOT: persistent metadata for one fixed GIF/JPEG slot.
 * Members:
 *   - ulGeneration: increments when the slot is committed or reset
 *   - ucFormat: persisted image format, JPEG/GIF/NONE
 *   - bImageAvailable: true only after validation of the committed file
 *   - aImageMd5: cached 16-byte MD5 used to skip duplicate BLE transfers
 */
typedef struct _AGENTPET_IMAGE_SLOT
{
    uint32_t ulGeneration;
    uint8_t ucFormat;
    bool bImageAvailable;
    uint8_t aImageMd5[AGENTPET_IMAGE_MD5_SIZE];
} AGENTPET_IMAGE_SLOT;

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
 * Queue capacity: 24 packets (up to 5856 packet bytes); worker stack: 2048 bytes.
 * The 4 KiB coalescing buffer aligns filesystem writes and reduces Flash calls. */
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
/* Last fully processed transfer status. Readers copy this snapshot inside a
 * short interrupt-safe critical section, so a BLE read never waits for Flash. */
static AGENTPET_IMAGE_STATUS l_tImageStatusSnapshot;
/* Fixed metadata table for the base mascot and four expression GIF slots. */
static AGENTPET_IMAGE_SLOT l_aImageSlots[AGENTPET_IMAGE_SLOT_COUNT];
/* Slot selected by the host before reading the digest characteristic. */
static uint8_t l_ucDigestSlot;

static const char *l_aImagePaths[AGENTPET_IMAGE_SLOT_COUNT] =
{
    AGENTPET_IMAGE_PATH,
    "/pet1.gif",
    "/pet2.gif",
    "/pet3.gif",
    "/pet4.gif"
};

static const char *l_aImageLvglPaths[AGENTPET_IMAGE_SLOT_COUNT] =
{
    AGENTPET_IMAGE_LVGL_PATH,
    "/:/pet1.gif",
    "/:/pet2.gif",
    "/:/pet3.gif",
    "/:/pet4.gif"
};

static const char *l_aImageTempPaths[AGENTPET_IMAGE_SLOT_COUNT] =
{
    AGENTPET_IMAGE_TEMP_PATH,
    "/pet1.tmp",
    "/pet2.tmp",
    "/pet3.tmp",
    "/pet4.tmp"
};

static const char *l_aImageBackupPaths[AGENTPET_IMAGE_SLOT_COUNT] =
{
    AGENTPET_IMAGE_BACKUP_PATH,
    "/pet1.bak",
    "/pet2.bak",
    "/pet3.bak",
    "/pet4.bak"
};

static uint8_t Local_DetectImageFormat(const char *pPath);

/*
 * Local_LoadSlotIntoEnvironment
 * Function: copy persistent metadata for a selected slot into the transfer environment.
 * Parameters:
 *   - ucSlot: fixed slot index, range 0..AGENTPET_IMAGE_SLOT_COUNT-1.
 * Return: none.
 */
static void Local_LoadSlotIntoEnvironment(uint8_t ucSlot)
{
    const AGENTPET_IMAGE_SLOT *pSlot;

    if (AGENTPET_IMAGE_SLOT_COUNT <= ucSlot)
    {
        return;
    }
    pSlot = &l_aImageSlots[ucSlot];
    l_tImageEnv.ucSlot = ucSlot;
    l_tImageEnv.ulGeneration = pSlot->ulGeneration;
    l_tImageEnv.ucFormat = pSlot->ucFormat;
    l_tImageEnv.bImageAvailable = pSlot->bImageAvailable;
    (void)memcpy(
        l_tImageEnv.aImageMd5,
        pSlot->aImageMd5,
        AGENTPET_IMAGE_MD5_SIZE);

    return;
}

/*
 * Local_SaveEnvironmentToSlot
 * Function: publish committed/reset metadata from the transfer environment to its slot.
 * Parameters: none.
 * Return: none.
 */
static void Local_SaveEnvironmentToSlot(void)
{
    AGENTPET_IMAGE_SLOT *pSlot;
    rt_base_t tLevel;

    if (AGENTPET_IMAGE_SLOT_COUNT <= l_tImageEnv.ucSlot)
    {
        return;
    }
    tLevel = rt_hw_interrupt_disable();
    pSlot = &l_aImageSlots[l_tImageEnv.ucSlot];
    pSlot->ulGeneration = l_tImageEnv.ulGeneration;
    pSlot->ucFormat = l_tImageEnv.ucFormat;
    pSlot->bImageAvailable = l_tImageEnv.bImageAvailable;
    (void)memcpy(
        pSlot->aImageMd5,
        l_tImageEnv.aImageMd5,
        AGENTPET_IMAGE_MD5_SIZE);
    rt_hw_interrupt_enable(tLevel);

    return;
}

/*
 * Local_PublishSnapshot
 * Function: publish coherent transfer progress for non-blocking BLE/UI readers.
 * Parameters: none.
 * Return: none.
 */
static void Local_PublishSnapshot(void)
{
    AGENTPET_IMAGE_STATUS tStatus;
    rt_base_t tLevel;

    tStatus.eState = l_tImageEnv.eState;
    tStatus.bImageAvailable = l_tImageEnv.bImageAvailable;
    tStatus.ulReceived = l_tImageEnv.ulReceived;
    tStatus.ulTotal = l_tImageEnv.ulTotal;
    tStatus.ulGeneration = l_tImageEnv.ulGeneration;
    tStatus.eLastResult = l_tImageEnv.eLastResult;
    tStatus.ucFormat = l_tImageEnv.ucFormat;
    tStatus.ucSlot = l_tImageEnv.ucSlot;

    tLevel = rt_hw_interrupt_disable();
    l_tImageStatusSnapshot = tStatus;
    rt_hw_interrupt_enable(tLevel);

    return;
}

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
    (void)unlink(l_aImageTempPaths[l_tImageEnv.ucSlot]);
    l_tImageEnv.usPendingLength = 0U;
    Local_LoadSlotIntoEnvironment(l_tImageEnv.ucSlot);
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

static uint16_t Local_ReadLittleEndian16(const uint8_t *pData)
{
    if (NULL == pData)
    {
        return 0U;
    }

    return (uint16_t)((uint16_t)pData[0] | ((uint16_t)pData[1] << 8U));
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

/*
 * Local_SkipGifSubBlocks
 * Function: validate and skip one bounded GIF data-sub-block chain.
 * Parameters:
 *   - lFileDescriptor: open GIF file descriptor.
 *   - ulFileSize: validated file size in bytes.
 *   - pOffset: current file offset, updated through the zero terminator.
 * Return: true for a complete bounded chain, otherwise false.
 */
static bool Local_SkipGifSubBlocks(
    int lFileDescriptor,
    uint32_t ulFileSize,
    uint32_t *pOffset)
{
    uint8_t ucBlockSize;

    if ((0 > lFileDescriptor) || (NULL == pOffset))
    {
        return false;
    }

    while (*pOffset < ulFileSize)
    {
        if (!Local_ReadExact(lFileDescriptor, &ucBlockSize, 1U))
        {
            return false;
        }
        (*pOffset)++;
        if (0U == ucBlockSize)
        {
            return true;
        }
        if ((uint32_t)ucBlockSize > ulFileSize - *pOffset)
        {
            return false;
        }
        if (0 > lseek(lFileDescriptor, ucBlockSize, SEEK_CUR))
        {
            return false;
        }
        *pOffset += ucBlockSize;
    }

    return false;
}

/*
 * Local_ValidateGif
 * Function: validate a bounded GIF89a stream before it reaches LVGL gifdec.
 * Parameters:
 *   - pPath: persistent GIF path; must not be NULL.
 * Return: accepted only for a complete 192x192-or-smaller animated GIF.
 */
static AGENTPET_IMAGE_RESULT Local_ValidateGif(const char *pPath)
{
    struct stat tStatus;
    uint8_t aHeader[AGENTPET_IMAGE_GIF_HEADER_SIZE];
    uint8_t aDescriptor[9];
    uint8_t ucSeparator;
    uint32_t ulFileSize;
    uint32_t ulOffset;
    uint32_t ulColorTableSize;
    uint16_t usCanvasWidth;
    uint16_t usCanvasHeight;
    uint16_t usFrameCount;
    int lFileDescriptor;
    bool bValid;

    if (NULL == pPath)
    {
        return AGENTPET_IMAGE_ERROR_INVALID_PARAMETER;
    }
    if ((0 != stat(pPath, &tStatus)) ||
        ((off_t)(AGENTPET_IMAGE_GIF_HEADER_SIZE + 1U) > tStatus.st_size) ||
        (AGENTPET_IMAGE_MAX_FILE_SIZE < (uint32_t)tStatus.st_size))
    {
        return AGENTPET_IMAGE_ERROR_SIZE;
    }

    lFileDescriptor = open(pPath, O_RDONLY | O_BINARY, 0);
    if (0 > lFileDescriptor)
    {
        return AGENTPET_IMAGE_ERROR_STORAGE;
    }

    bValid = false;
    ulFileSize = (uint32_t)tStatus.st_size;
    ulOffset = sizeof(aHeader);
    usFrameCount = 0U;
    if (!Local_ReadExact(lFileDescriptor, aHeader, sizeof(aHeader)) ||
        (0 != memcmp(aHeader, "GIF89a", 6U)) ||
        (0U == (aHeader[10] & 0x80U)))
    {
        goto cleanup;
    }
    usCanvasWidth = Local_ReadLittleEndian16(&aHeader[6]);
    usCanvasHeight = Local_ReadLittleEndian16(&aHeader[8]);
    if ((0U == usCanvasWidth) || (0U == usCanvasHeight) ||
        (AGENTPET_IMAGE_GIF_MAX_DIMENSION < usCanvasWidth) ||
        (AGENTPET_IMAGE_GIF_MAX_DIMENSION < usCanvasHeight))
    {
        goto cleanup;
    }

    ulColorTableSize = 3UL << ((aHeader[10] & 0x07U) + 1U);
    if (ulColorTableSize > ulFileSize - ulOffset)
    {
        goto cleanup;
    }
    if (0 > lseek(lFileDescriptor, (off_t)ulColorTableSize, SEEK_CUR))
    {
        goto cleanup;
    }
    ulOffset += ulColorTableSize;

    while (ulOffset < ulFileSize)
    {
        if (!Local_ReadExact(lFileDescriptor, &ucSeparator, 1U))
        {
            break;
        }
        ulOffset++;
        if (0x3BU == ucSeparator)
        {
            bValid = (1U < usFrameCount) && (ulOffset == ulFileSize);
            break;
        }
        if (0x21U == ucSeparator)
        {
            if ((ulOffset >= ulFileSize) ||
                !Local_ReadExact(lFileDescriptor, &ucSeparator, 1U))
            {
                break;
            }
            ulOffset++;
            if (!Local_SkipGifSubBlocks(
                    lFileDescriptor,
                    ulFileSize,
                    &ulOffset))
            {
                break;
            }
            continue;
        }
        if ((0x2CU != ucSeparator) ||
            (sizeof(aDescriptor) > ulFileSize - ulOffset) ||
            !Local_ReadExact(lFileDescriptor, aDescriptor, sizeof(aDescriptor)))
        {
            break;
        }
        ulOffset += sizeof(aDescriptor);
        if ((0U == Local_ReadLittleEndian16(&aDescriptor[4])) ||
            (0U == Local_ReadLittleEndian16(&aDescriptor[6])) ||
            ((uint32_t)Local_ReadLittleEndian16(&aDescriptor[0]) +
             Local_ReadLittleEndian16(&aDescriptor[4]) > usCanvasWidth) ||
            ((uint32_t)Local_ReadLittleEndian16(&aDescriptor[2]) +
             Local_ReadLittleEndian16(&aDescriptor[6]) > usCanvasHeight))
        {
            break;
        }
        if (0U != (aDescriptor[8] & 0x80U))
        {
            ulColorTableSize = 3UL << ((aDescriptor[8] & 0x07U) + 1U);
            if (ulColorTableSize > ulFileSize - ulOffset)
            {
                break;
            }
            if (0 > lseek(lFileDescriptor, (off_t)ulColorTableSize, SEEK_CUR))
            {
                break;
            }
            ulOffset += ulColorTableSize;
        }
        if ((ulOffset >= ulFileSize) ||
            !Local_ReadExact(lFileDescriptor, &ucSeparator, 1U))
        {
            break;
        }
        ulOffset++;
        if ((2U > ucSeparator) || (8U < ucSeparator))
        {
            break;
        }
        if (!Local_SkipGifSubBlocks(lFileDescriptor, ulFileSize, &ulOffset))
        {
            break;
        }
        usFrameCount++;
        if (AGENTPET_IMAGE_GIF_MAX_FRAMES < usFrameCount)
        {
            break;
        }
    }

cleanup:
    (void)close(lFileDescriptor);
    return bValid ? AGENTPET_IMAGE_RESULT_ACCEPTED :
        AGENTPET_IMAGE_ERROR_FORMAT;
}

static uint8_t Local_DetectImageFormat(const char *pPath)
{
    uint8_t aSignature[6];
    int lFileDescriptor;
    int lReadLength;

    if (NULL == pPath)
    {
        return AGENTPET_IMAGE_FORMAT_NONE;
    }
    lFileDescriptor = open(pPath, O_RDONLY | O_BINARY, 0);
    if (0 > lFileDescriptor)
    {
        return AGENTPET_IMAGE_FORMAT_NONE;
    }
    lReadLength = read(lFileDescriptor, aSignature, sizeof(aSignature));
    (void)close(lFileDescriptor);
    if ((2 <= lReadLength) && (0xFFU == aSignature[0]) &&
        (0xD8U == aSignature[1]))
    {
        return AGENTPET_IMAGE_FORMAT_JPEG;
    }
    if ((int)sizeof(aSignature) == lReadLength &&
        (0 == memcmp(aSignature, "GIF89a", sizeof(aSignature))))
    {
        return AGENTPET_IMAGE_FORMAT_GIF;
    }

    return AGENTPET_IMAGE_FORMAT_NONE;
}

static AGENTPET_IMAGE_RESULT Local_ValidateImage(
    const char *pPath,
    uint8_t ucFormat)
{
    if (AGENTPET_IMAGE_FORMAT_JPEG == ucFormat)
    {
        return Local_ValidateJpeg(pPath);
    }
    if (AGENTPET_IMAGE_FORMAT_GIF == ucFormat)
    {
        return Local_ValidateGif(pPath);
    }

    return AGENTPET_IMAGE_ERROR_FORMAT;
}

static AGENTPET_IMAGE_RESULT Local_BeginTransfer(
    uint32_t ulTotal,
    uint32_t ulExpectedCrc,
    uint8_t ucFormat,
    uint8_t ucSlot)
{
    struct statfs tFileSystem;
    uint64_t udAvailableBytes;

    if (
        ((AGENTPET_IMAGE_FORMAT_JPEG != ucFormat) &&
         (AGENTPET_IMAGE_FORMAT_GIF != ucFormat)) ||
        (4U > ulTotal) ||
        (AGENTPET_IMAGE_MAX_FILE_SIZE < ulTotal) ||
        (AGENTPET_IMAGE_SLOT_COUNT <= ucSlot)
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
    Local_LoadSlotIntoEnvironment(ucSlot);
    (void)unlink(l_aImageTempPaths[ucSlot]);
    l_tImageEnv.lFileDescriptor = open(
        l_aImageTempPaths[ucSlot],
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
    l_tImageEnv.ucFormat = ucFormat;
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
    uint8_t ucFormat,
    uint8_t ucSlot)
{
    AGENTPET_IMAGE_RESULT eResult;
    uint8_t aImageMd5[AGENTPET_IMAGE_MD5_SIZE];

    if (
        (AGENTPET_IMAGE_RECEIVING != l_tImageEnv.eState) ||
        (l_tImageEnv.ucFormat != ucFormat) ||
        (l_tImageEnv.ulTotal != ulTotal) ||
        (l_tImageEnv.ulReceived != ulTotal) ||
        (l_tImageEnv.ulExpectedCrc != ulExpectedCrc) ||
        (l_tImageEnv.ulCalculatedCrc != ulExpectedCrc) ||
        (l_tImageEnv.ucSlot != ucSlot)
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
    eResult = Local_ValidateImage(l_aImageTempPaths[ucSlot], ucFormat);
    if (AGENTPET_IMAGE_RESULT_ACCEPTED != eResult)
    {
        return eResult;
    }
    if (!Local_CalculateFileMd5(l_aImageTempPaths[ucSlot], aImageMd5))
    {
        return AGENTPET_IMAGE_ERROR_STORAGE;
    }

    (void)unlink(l_aImageBackupPaths[ucSlot]);
    if ((0 == access(l_aImagePaths[ucSlot], 0)) &&
        (0 != rename(l_aImagePaths[ucSlot], l_aImageBackupPaths[ucSlot])))
    {
        return AGENTPET_IMAGE_ERROR_STORAGE;
    }
    if (0 != rename(l_aImageTempPaths[ucSlot], l_aImagePaths[ucSlot]))
    {
        (void)rename(l_aImageBackupPaths[ucSlot], l_aImagePaths[ucSlot]);
        return AGENTPET_IMAGE_ERROR_STORAGE;
    }
    (void)unlink(l_aImageBackupPaths[ucSlot]);

    (void)memcpy(l_tImageEnv.aImageMd5, aImageMd5, AGENTPET_IMAGE_MD5_SIZE);
    l_tImageEnv.bImageAvailable = true;
    l_tImageEnv.ulGeneration++;
    l_tImageEnv.eState = AGENTPET_IMAGE_READY;
    l_tImageEnv.eLastResult = AGENTPET_IMAGE_RESULT_COMMITTED;
    Local_SaveEnvironmentToSlot();
    LOG_I("Custom mascot committed slot=%u format=%u size=%lu crc=0x%08lx",
          ucSlot,
          ucFormat,
          (unsigned long)ulTotal,
          (unsigned long)ulExpectedCrc);

    return AGENTPET_IMAGE_RESULT_COMMITTED;
}

static AGENTPET_IMAGE_RESULT Local_ResetImage(uint8_t ucSlot)
{
    int lResult;

    if (AGENTPET_IMAGE_SLOT_COUNT <= ucSlot)
    {
        return AGENTPET_IMAGE_ERROR_SIZE;
    }
    Local_CloseTemporaryFile();
    Local_LoadSlotIntoEnvironment(ucSlot);
    lResult = 0;
    if ((0 == access(l_aImageTempPaths[ucSlot], 0)) &&
        (0 != unlink(l_aImageTempPaths[ucSlot])))
    {
        lResult = -1;
    }
    if ((0 == access(l_aImageBackupPaths[ucSlot], 0)) &&
        (0 != unlink(l_aImageBackupPaths[ucSlot])))
    {
        lResult = -1;
    }
    if ((0 == access(l_aImagePaths[ucSlot], 0)) &&
        (0 != unlink(l_aImagePaths[ucSlot])))
    {
        lResult = -1;
    }
    if ((AGENTPET_IMAGE_BASE_SLOT == ucSlot) &&
        (0 == access(AGENTPET_IMAGE_LEGACY_PATH, 0)) &&
        (0 != unlink(AGENTPET_IMAGE_LEGACY_PATH)))
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
    l_tImageEnv.ucFormat = AGENTPET_IMAGE_FORMAT_NONE;
    l_tImageEnv.bImageAvailable = false;
    (void)memset(l_tImageEnv.aImageMd5, 0, AGENTPET_IMAGE_MD5_SIZE);
    l_tImageEnv.ulGeneration++;
    l_tImageEnv.eState = AGENTPET_IMAGE_IDLE;
    l_tImageEnv.eLastResult = AGENTPET_IMAGE_RESULT_RESET;
    Local_SaveEnvironmentToSlot();
    LOG_I("Custom mascot slot %u reset", ucSlot);

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
        Local_PublishSnapshot();
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
    uint8_t ucSlot;

    if (l_bImageWorkerReady)
    {
        return;
    }

    Local_CloseTemporaryFile();
    if ((0 != stat(AGENTPET_IMAGE_PATH, &tStatus)) &&
        (0 == stat(AGENTPET_IMAGE_LEGACY_PATH, &tStatus)))
    {
        (void)rename(AGENTPET_IMAGE_LEGACY_PATH, AGENTPET_IMAGE_PATH);
    }
    (void)memset(l_aImageSlots, 0, sizeof(l_aImageSlots));
    for (ucSlot = 0U; ucSlot < AGENTPET_IMAGE_SLOT_COUNT; ucSlot++)
    {
        AGENTPET_IMAGE_SLOT *pSlot;

        if ((0 != stat(l_aImagePaths[ucSlot], &tStatus)) &&
            (0 == stat(l_aImageBackupPaths[ucSlot], &tStatus)))
        {
            (void)rename(l_aImageBackupPaths[ucSlot], l_aImagePaths[ucSlot]);
        }
        else if (0 == stat(l_aImagePaths[ucSlot], &tStatus))
        {
            (void)unlink(l_aImageBackupPaths[ucSlot]);
        }
        (void)unlink(l_aImageTempPaths[ucSlot]);
        pSlot = &l_aImageSlots[ucSlot];
        pSlot->bImageAvailable = (0 == stat(l_aImagePaths[ucSlot], &tStatus));
        pSlot->ucFormat = pSlot->bImageAvailable ?
            Local_DetectImageFormat(l_aImagePaths[ucSlot]) :
            AGENTPET_IMAGE_FORMAT_NONE;
        if (pSlot->bImageAvailable &&
            (AGENTPET_IMAGE_RESULT_ACCEPTED !=
             Local_ValidateImage(l_aImagePaths[ucSlot], pSlot->ucFormat)))
        {
            LOG_E("Persistent mascot slot %u is unsafe; removing it", ucSlot);
            (void)unlink(l_aImagePaths[ucSlot]);
            pSlot->bImageAvailable = false;
            pSlot->ucFormat = AGENTPET_IMAGE_FORMAT_NONE;
        }
        if (pSlot->bImageAvailable &&
            !Local_CalculateFileMd5(l_aImagePaths[ucSlot], pSlot->aImageMd5))
        {
            LOG_E("Persistent mascot slot %u MD5 calculation failed", ucSlot);
        }
        pSlot->ulGeneration = pSlot->bImageAvailable ? 1U : 0U;
    }
    l_ucDigestSlot = AGENTPET_IMAGE_BASE_SLOT;
    Local_LoadSlotIntoEnvironment(AGENTPET_IMAGE_BASE_SLOT);
    l_tImageEnv.ulTotal = 0U;
    l_tImageEnv.ulReceived = 0U;
    l_tImageEnv.ulExpectedCrc = 0U;
    l_tImageEnv.ulCalculatedCrc = AGENTPET_IMAGE_CRC32_INIT;
    l_tImageEnv.usPendingLength = 0U;
    l_tImageEnv.eState = l_tImageEnv.bImageAvailable ?
        AGENTPET_IMAGE_READY : AGENTPET_IMAGE_IDLE;
    l_tImageEnv.eLastResult = AGENTPET_IMAGE_RESULT_ACCEPTED;
    Local_PublishSnapshot();

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
 * Function: copy one variable-length packet into the bounded worker queue with
 *           a short, bounded retry that yields CPU time to the Flash worker.
 * Parameters:
 *   - pFrame: read-only image packet, 9..244 bytes.
 *   - ulLength: actual packet length, 9..244 bytes.
 * Return: true when queued; false for invalid input, unavailable worker, or a
 *         queue that remains full for 200 ms.
 */
bool AGENTPETIMAGE_QueueFrame(const uint8_t *pFrame, size_t ulLength)
{
    AGENTPET_IMAGE_PACKET tPacket;
    rt_err_t tResult;
    uint16_t usAttempt;

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

    for (usAttempt = 0U;
         usAttempt < AGENTPET_IMAGE_QUEUE_RETRY_COUNT;
         usAttempt++)
    {
        tResult = rt_mq_send(
            &l_tImageQueue,
            &tPacket,
            sizeof(tPacket));
        if (RT_EOK == tResult)
        {
            return true;
        }
        if ((0U != rt_interrupt_get_nest()) ||
            ((AGENTPET_IMAGE_QUEUE_RETRY_COUNT - 1U) == usAttempt))
        {
            break;
        }
        rt_thread_mdelay(AGENTPET_IMAGE_QUEUE_RETRY_MS);
    }
    LOG_E("Custom mascot queue timeout len=%u entries=%u result=%d",
          (unsigned int)ulLength,
          (unsigned int)l_tImageQueue.entry,
          tResult);

    return false;
}

/*
 * AGENTPETIMAGE_AbortTransfer
 * Function: discard queued/incomplete image data and publish an explicit error
 *           state so BLE readers and LVGL cannot remain in RECEIVING forever.
 * Parameters:
 *   - eResult: bounded transfer error; non-error values map to STATE error.
 * Return: none.
 */
void AGENTPETIMAGE_AbortTransfer(AGENTPET_IMAGE_RESULT eResult)
{
    if (AGENTPET_IMAGE_ERROR_INVALID_PARAMETER > eResult)
    {
        eResult = AGENTPET_IMAGE_ERROR_STATE;
    }
    if (l_bImageWorkerReady)
    {
        (void)rt_mq_control(&l_tImageQueue, RT_IPC_CMD_RESET, NULL);
        (void)rt_mutex_take(&l_tImageMutex, RT_WAITING_FOREVER);
    }
    if (AGENTPET_IMAGE_RECEIVING == l_tImageEnv.eState)
    {
        Local_FailTransfer(eResult);
    }
    else
    {
        l_tImageEnv.eLastResult = eResult;
    }
    Local_PublishSnapshot();
    if (l_bImageWorkerReady)
    {
        (void)rt_mutex_release(&l_tImageMutex);
    }

    return;
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
        (void)unlink(l_aImageTempPaths[l_tImageEnv.ucSlot]);
        l_tImageEnv.ulTotal = 0U;
        l_tImageEnv.ulReceived = 0U;
        l_tImageEnv.usPendingLength = 0U;
        Local_LoadSlotIntoEnvironment(l_tImageEnv.ucSlot);
        l_tImageEnv.eState = l_tImageEnv.bImageAvailable ?
            AGENTPET_IMAGE_READY : AGENTPET_IMAGE_IDLE;
        l_tImageEnv.eLastResult = AGENTPET_IMAGE_RESULT_ACCEPTED;
    }
    Local_PublishSnapshot();
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
    uint8_t ucProtocolVersion;
    uint8_t ucSlot;

    if (NULL == pFrame)
    {
        return AGENTPET_IMAGE_ERROR_INVALID_PARAMETER;
    }
    if (
        (AGENTPET_IMAGE_PACKET_OVERHEAD > ulLength) ||
        (AGENTPET_IMAGE_MAX_PACKET_SIZE < ulLength) ||
        (AGENTPET_IMAGE_MAGIC_FIRST != pFrame[0]) ||
        (AGENTPET_IMAGE_MAGIC_SECOND != pFrame[1]) ||
        ((AGENTPET_IMAGE_PROTOCOL_VERSION != pFrame[2]) &&
         (AGENTPET_IMAGE_SLOT_PROTOCOL_VERSION != pFrame[2]))
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
    ucProtocolVersion = pFrame[2];
    ucSlot = AGENTPET_IMAGE_BASE_SLOT;
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
        if (AGENTPET_IMAGE_SLOT_PROTOCOL_VERSION == ucProtocolVersion)
        {
            ucSlot = pFrame[12];
        }
        for (ucIndex = (AGENTPET_IMAGE_SLOT_PROTOCOL_VERSION == ucProtocolVersion) ?
             13U : 12U;
             ucIndex < AGENTPET_IMAGE_CONTROL_FRAME_SIZE - 1U;
             ucIndex++)
        {
            if (0U != pFrame[ucIndex])
            {
                return AGENTPET_IMAGE_ERROR_FRAME;
            }
        }
        ulCrc = Local_ReadLe32(&pFrame[8]);
        eResult = (AGENTPET_IMAGE_COMMAND_BEGIN == ucCommand) ?
            Local_BeginTransfer(ulValue, ulCrc, pFrame[7], ucSlot) :
            Local_CommitTransfer(ulValue, ulCrc, pFrame[7], ucSlot);
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
        if (AGENTPET_IMAGE_SLOT_PROTOCOL_VERSION == ucProtocolVersion)
        {
            ucSlot = pFrame[12];
        }
        for (ucIndex = 4U; ucIndex < 12U; ucIndex++)
        {
            if (0U != pFrame[ucIndex])
            {
                return AGENTPET_IMAGE_ERROR_FRAME;
            }
        }
        for (ucIndex = 13U; ucIndex < AGENTPET_IMAGE_CONTROL_FRAME_SIZE - 1U; ucIndex++)
        {
            if (0U != pFrame[ucIndex])
            {
                return AGENTPET_IMAGE_ERROR_FRAME;
            }
        }
        if ((AGENTPET_IMAGE_PROTOCOL_VERSION == ucProtocolVersion) &&
            (0U != pFrame[12]))
        {
            return AGENTPET_IMAGE_ERROR_FRAME;
        }
        eResult = Local_ResetImage(ucSlot);
    }
    else if (AGENTPET_IMAGE_COMMAND_SELECT == ucCommand)
    {
        if ((AGENTPET_IMAGE_SLOT_PROTOCOL_VERSION != ucProtocolVersion) ||
            (AGENTPET_IMAGE_CONTROL_FRAME_SIZE != ulLength))
        {
            return AGENTPET_IMAGE_ERROR_FRAME;
        }
        for (ucIndex = 5U; ucIndex < AGENTPET_IMAGE_CONTROL_FRAME_SIZE - 1U; ucIndex++)
        {
            if (0U != pFrame[ucIndex])
            {
                return AGENTPET_IMAGE_ERROR_FRAME;
            }
        }
        eResult = AGENTPETIMAGE_SelectDigestSlot(pFrame[4]) ?
            AGENTPET_IMAGE_RESULT_ACCEPTED : AGENTPET_IMAGE_ERROR_SIZE;
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
    rt_base_t tLevel;

    if (NULL == pStatus)
    {
        return false;
    }

    tLevel = rt_hw_interrupt_disable();
    *pStatus = l_tImageStatusSnapshot;
    rt_hw_interrupt_enable(tLevel);

    return true;
}

/*
 * AGENTPETIMAGE_GetSlotStatus
 * Function: copy persistent metadata and active-transfer progress for one image slot.
 * Parameters:
 *   - ucSlot: fixed slot index.
 *   - pStatus: output status; must not be NULL.
 * Return: true for a valid slot and output pointer.
 */
bool AGENTPETIMAGE_GetSlotStatus(
    uint8_t ucSlot,
    AGENTPET_IMAGE_STATUS *pStatus)
{
    AGENTPET_IMAGE_SLOT tSlot;
    AGENTPET_IMAGE_STATUS tActiveStatus;
    rt_base_t tLevel;

    if ((AGENTPET_IMAGE_SLOT_COUNT <= ucSlot) || (NULL == pStatus))
    {
        return false;
    }

    tLevel = rt_hw_interrupt_disable();
    tSlot = l_aImageSlots[ucSlot];
    tActiveStatus = l_tImageStatusSnapshot;
    rt_hw_interrupt_enable(tLevel);
    if (ucSlot == tActiveStatus.ucSlot)
    {
        *pStatus = tActiveStatus;
        return true;
    }

    (void)memset(pStatus, 0, sizeof(*pStatus));
    pStatus->eState = tSlot.bImageAvailable ?
        AGENTPET_IMAGE_READY : AGENTPET_IMAGE_IDLE;
    pStatus->bImageAvailable = tSlot.bImageAvailable;
    pStatus->ulGeneration = tSlot.ulGeneration;
    pStatus->eLastResult = AGENTPET_IMAGE_RESULT_ACCEPTED;
    pStatus->ucFormat = tSlot.ucFormat;
    pStatus->ucSlot = ucSlot;

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
    AGENTPET_IMAGE_SLOT tSlot;
    rt_base_t tLevel;

    if ((NULL == pAvailable) || (NULL == pDigest))
    {
        return false;
    }

    tLevel = rt_hw_interrupt_disable();
    tSlot = l_aImageSlots[l_ucDigestSlot];
    rt_hw_interrupt_enable(tLevel);
    *pAvailable = tSlot.bImageAvailable;
    (void)memcpy(pDigest, tSlot.aImageMd5, AGENTPET_IMAGE_MD5_SIZE);

    return true;
}

/*
 * AGENTPETIMAGE_SelectDigestSlot
 * Function: select which fixed slot is returned by subsequent digest reads.
 * Parameters:
 *   - ucSlot: fixed slot index.
 * Return: true when selected, false for an out-of-range slot.
 */
bool AGENTPETIMAGE_SelectDigestSlot(uint8_t ucSlot)
{
    rt_base_t tLevel;

    if (AGENTPET_IMAGE_SLOT_COUNT <= ucSlot)
    {
        return false;
    }
    tLevel = rt_hw_interrupt_disable();
    l_ucDigestSlot = ucSlot;
    rt_hw_interrupt_enable(tLevel);

    return true;
}

/*
 * AGENTPETIMAGE_GetSelectedDigestSlot
 * Function: read the slot currently selected for digest queries.
 * Parameters: none.
 * Return: fixed slot index.
 */
uint8_t AGENTPETIMAGE_GetSelectedDigestSlot(void)
{
    uint8_t ucSlot;
    rt_base_t tLevel;

    tLevel = rt_hw_interrupt_disable();
    ucSlot = l_ucDigestSlot;
    rt_hw_interrupt_enable(tLevel);

    return ucSlot;
}

/*
 * AGENTPETIMAGE_GetPath
 * Function: obtain the RT-Thread filesystem path for a fixed image slot.
 * Parameters:
 *   - ucSlot: fixed slot index.
 * Return: static path pointer, or NULL for an invalid slot.
 */
const char *AGENTPETIMAGE_GetPath(uint8_t ucSlot)
{
    return (AGENTPET_IMAGE_SLOT_COUNT > ucSlot) ? l_aImagePaths[ucSlot] : NULL;
}

/*
 * AGENTPETIMAGE_GetLvglPath
 * Function: obtain the LVGL filesystem path for a fixed image slot.
 * Parameters:
 *   - ucSlot: fixed slot index.
 * Return: static path pointer, or NULL for an invalid slot.
 */
const char *AGENTPETIMAGE_GetLvglPath(uint8_t ucSlot)
{
    return (AGENTPET_IMAGE_SLOT_COUNT > ucSlot) ?
        l_aImageLvglPaths[ucSlot] : NULL;
}
