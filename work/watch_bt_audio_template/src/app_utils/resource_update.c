#include "resource_update.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <dfs_posix.h>
#include <mbedtls/sha256.h>
#include <rtthread.h>

#define LOG_TAG "res.update"
#include "log.h"

#ifndef LOCAL
    #define LOCAL static
#endif

#ifndef INPUT
    #define INPUT
#endif

#define RESOURCE_UPDATE_ROOT_PATH            "/ex"
#define RESOURCE_UPDATE_STAGE_PATH           "/ex/.update"
#define RESOURCE_UPDATE_VERSION_PATH         "/ex/resource.version"
#define RESOURCE_UPDATE_VERSION_TEMP_PATH    "/ex/resource.version.tmp"
#define RESOURCE_UPDATE_VERSION_BACKUP_PATH  "/ex/resource.version.bak"
#define RESOURCE_UPDATE_JOURNAL_PATH         "/ex/.update/journal"
#define RESOURCE_UPDATE_PENDING_PATH         "/ex/.update/pending.version"
#define RESOURCE_UPDATE_MAX_FILES            (16U)
#define RESOURCE_UPDATE_RELATIVE_PATH_LENGTH  (56U)
#define RESOURCE_UPDATE_WIRE_NAME_MAX_LENGTH   (121U)
#define RESOURCE_UPDATE_FULL_PATH_LENGTH      (80U)
#define RESOURCE_UPDATE_IO_RESERVE_BYTES      (32U * 1024U)
#define RESOURCE_UPDATE_CRC_INITIAL_VALUE     (0xFFFFFFFFU)
#define RESOURCE_UPDATE_SHA256_LENGTH         (32U)
#define RESOURCE_UPDATE_STATE_IDLE            (0U)
#define RESOURCE_UPDATE_STATE_PREPARED        (1U)
#define RESOURCE_UPDATE_STATE_RECEIVING       (2U)
#define RESOURCE_UPDATE_STATE_COMMITTING      (3U)
#define RESOURCE_UPDATE_STATE_COMPLETE        (4U)
#define RESOURCE_UPDATE_STATE_ERROR           (5U)

/* RESOURCE_UPDATE_FILE: one verified file staged for an atomic transaction.
 * Members:
 *   - aRelativePath: path below /ex; only resource/*.bin or font/*.ttf.
 *   - aStagePath: transaction-private temporary file path.
 *   - aExpectedSha256: SHA-256 supplied by the signed/validated App package.
 *   - ulPayloadSize: bytes written to the resource file, excluding CRC.
 *   - ucHadOriginal: non-zero when rollback must restore a .bak file.
 */
typedef struct _RESOURCE_UPDATE_FILE
{
    char aRelativePath[RESOURCE_UPDATE_RELATIVE_PATH_LENGTH];
    char aStagePath[RESOURCE_UPDATE_FULL_PATH_LENGTH];
    uint8_t aExpectedSha256[RESOURCE_UPDATE_SHA256_LENGTH];
    uint32_t ulPayloadSize;
    uint8_t ucHadOriginal;
} RESOURCE_UPDATE_FILE;

/* RESOURCE_UPDATE_ENV: fixed-memory state for one BLE resource transaction.
 * The 512 KiB payload itself is streamed to FAT; RAM use stays below 4 KiB.
 */
typedef struct _RESOURCE_UPDATE_ENV
{
    RESOURCE_UPDATE_FILE aFiles[RESOURCE_UPDATE_MAX_FILES];
    mbedtls_sha256_context tSha256;
    char aBaseVersion[RESOURCE_UPDATE_VERSION_MAX_LENGTH + 1U];
    char aTargetVersion[RESOURCE_UPDATE_VERSION_MAX_LENGTH + 1U];
    int lFileDescriptor;
    uint32_t ulSessionExpected;
    uint32_t ulSessionReceived;
    uint32_t ulFileExpected;
    uint32_t ulFileReceived;
    uint32_t ulPayloadReceived;
    uint32_t ulCrc;
    uint8_t aReceivedCrc[4];
    uint8_t aPendingSha256[RESOURCE_UPDATE_SHA256_LENGTH];
    uint8_t ucReceivedCrcLength;
    uint8_t ucFileCount;
    uint8_t ucCommittedFiles;
    uint8_t ucCurrentFile;
    uint8_t ucHasPendingInfo;
    uint8_t ucSha256Active;
    uint8_t ucState;
    int16_t sLastError;
} RESOURCE_UPDATE_ENV;

/* Module-private transaction state. File count is 0..16, payload is capped at
 * 512 KiB, and no field is shared outside the BLE worker callback context. */
LOCAL RESOURCE_UPDATE_ENV l_tResourceUpdateEnv = {
    .lFileDescriptor = -1,
    .ucState = RESOURCE_UPDATE_STATE_IDLE,
};

/* Local_Crc32Mpeg2: update the MPEG-2 CRC used by the SiFli transport.
 * Parameters: pData is input only, ulLength is bounded by the BLE frame.
 * Return value: updated 32-bit CRC.
 */
LOCAL uint32_t Local_Crc32Mpeg2(INPUT const uint8_t *pData,
                               uint32_t ulLength,
                               uint32_t ulCrc)
{
    uint32_t ulBit;

    while (0U < ulLength)
    {
        ulCrc ^= (uint32_t)(*pData) << 24;
        pData++;
        ulLength--;
        for (ulBit = 0U; ulBit < 8U; ulBit++)
        {
            if (0U != (ulCrc & 0x80000000U))
            {
                ulCrc = (ulCrc << 1) ^ 0x04C11DB7U;
            }
            else
            {
                ulCrc <<= 1;
            }
        }
    }

    return ulCrc;
}

/* Local_ReadU32BigEndian: decode the final transport CRC. */
LOCAL uint32_t Local_ReadU32BigEndian(INPUT const uint8_t *pData)
{
    uint32_t ulValue;

    ulValue = ((uint32_t)pData[0] << 24) |
              ((uint32_t)pData[1] << 16) |
              ((uint32_t)pData[2] << 8) |
              (uint32_t)pData[3];

    return ulValue;
}

/* Local_WriteAll: write a complete buffer and propagate short-write errors. */
LOCAL int Local_WriteAll(int lFileDescriptor,
                         INPUT const uint8_t *pData,
                         uint32_t ulLength)
{
    uint32_t ulOffset;

    if (NULL == pData && 0U != ulLength)
    {
        return -RT_EINVAL;
    }

    ulOffset = 0U;
    while (ulOffset < ulLength)
    {
        int lWritten;

        lWritten = write(lFileDescriptor, pData + ulOffset, ulLength - ulOffset);
        if (0 >= lWritten)
        {
            return -RT_ERROR;
        }
        ulOffset += (uint32_t)lWritten;
    }

    return RT_EOK;
}

/* Local_CloseCurrentFile: flush and close a staged resource file. */
LOCAL int Local_CloseCurrentFile(void)
{
    int lRetVal;

    lRetVal = RT_EOK;
    if (0 <= l_tResourceUpdateEnv.lFileDescriptor)
    {
        if (0 != fsync(l_tResourceUpdateEnv.lFileDescriptor))
        {
            lRetVal = -RT_ERROR;
        }
        if (0 != close(l_tResourceUpdateEnv.lFileDescriptor))
        {
            lRetVal = -RT_ERROR;
        }
        l_tResourceUpdateEnv.lFileDescriptor = -1;
    }
    if (0U != l_tResourceUpdateEnv.ucSha256Active)
    {
        mbedtls_sha256_free(&l_tResourceUpdateEnv.tSha256);
        l_tResourceUpdateEnv.ucSha256Active = 0U;
    }

    return lRetVal;
}

/* Local_IsVersionValid: accept short, printable version identifiers only. */
LOCAL int Local_IsVersionValid(INPUT const char *pVersion)
{
    size_t ulIndex;
    size_t ulLength;

    if (NULL == pVersion)
    {
        return 0;
    }
    ulLength = strlen(pVersion);
    if (0U == ulLength || RESOURCE_UPDATE_VERSION_MAX_LENGTH < ulLength)
    {
        return 0;
    }
    for (ulIndex = 0U; ulIndex < ulLength; ulIndex++)
    {
        char cValue;

        cValue = pVersion[ulIndex];
        if (!(('0' <= cValue && '9' >= cValue) ||
                ('A' <= cValue && 'Z' >= cValue) ||
                ('a' <= cValue && 'z' >= cValue) ||
                '.' == cValue || '_' == cValue || '-' == cValue))
        {
            return 0;
        }
    }

    return 1;
}

/* Local_ReadVersion: read the durable resource version without heap memory. */
LOCAL int Local_ReadVersion(char *pVersion, size_t ulVersionSize)
{
    int lFileDescriptor;
    int lReadLength;

    if (NULL == pVersion || 2U > ulVersionSize)
    {
        return -RT_EINVAL;
    }
    pVersion[0] = '\0';
    lFileDescriptor = open(RESOURCE_UPDATE_VERSION_PATH, O_RDONLY | O_BINARY, 0);
    if (0 > lFileDescriptor)
    {
        return -RT_ERROR;
    }
    lReadLength = read(lFileDescriptor, pVersion, ulVersionSize - 1U);
    if (0 != close(lFileDescriptor) || 0 >= lReadLength)
    {
        pVersion[0] = '\0';
        return -RT_ERROR;
    }
    pVersion[lReadLength] = '\0';
    while (0 < lReadLength && ('\r' == pVersion[lReadLength - 1] ||
                               '\n' == pVersion[lReadLength - 1]))
    {
        lReadLength--;
        pVersion[lReadLength] = '\0';
    }

    return (0 != Local_IsVersionValid(pVersion)) ? RT_EOK : -RT_ERROR;
}

/* Local_WriteTextFile: create, flush and close a small transaction file. */
LOCAL int Local_WriteTextFile(INPUT const char *pPath, INPUT const char *pText)
{
    int lFileDescriptor;
    size_t ulLength;
    int lRetVal;

    if (NULL == pPath || NULL == pText)
    {
        return -RT_EINVAL;
    }
    ulLength = strlen(pText);
    lFileDescriptor = open(pPath, O_CREAT | O_RDWR | O_TRUNC | O_BINARY, 0);
    if (0 > lFileDescriptor)
    {
        return -RT_ERROR;
    }
    lRetVal = Local_WriteAll(lFileDescriptor, (const uint8_t *)pText,
                             (uint32_t)ulLength);
    if (RT_EOK == lRetVal && 0 != fsync(lFileDescriptor))
    {
        lRetVal = -RT_ERROR;
    }
    if (0 != close(lFileDescriptor))
    {
        lRetVal = -RT_ERROR;
    }

    return lRetVal;
}

/* Local_BuildTargetPath: prepend the fixed /ex resource root. */
LOCAL int Local_BuildTargetPath(INPUT const char *pRelativePath,
                                char *pTargetPath,
                                size_t ulTargetPathSize)
{
    int lLength;

    if (NULL == pRelativePath || NULL == pTargetPath)
    {
        return -RT_EINVAL;
    }
    lLength = rt_snprintf(pTargetPath, ulTargetPathSize, "%s/%s",
                          RESOURCE_UPDATE_ROOT_PATH, pRelativePath);

    return (0 < lLength && (size_t)lLength < ulTargetPathSize) ? RT_EOK : -RT_EFULL;
}

/* Local_IsResourcePathValid: reject traversal and restrict writable targets. */
LOCAL int Local_IsResourcePathValid(INPUT const uint8_t *pPath, uint16_t usLength)
{
    uint16_t usIndex;
    uint16_t usPrefixLength;
    const char *pExtension;

    if (NULL == pPath || 0U == usLength ||
            RESOURCE_UPDATE_RELATIVE_PATH_LENGTH <= usLength)
    {
        return 0;
    }
    if (9U < usLength && 0 == memcmp(pPath, "resource/", 9U))
    {
        usPrefixLength = 9U;
        pExtension = ".bin";
    }
    else if (5U < usLength && 0 == memcmp(pPath, "font/", 5U))
    {
        usPrefixLength = 5U;
        pExtension = ".ttf";
    }
    else
    {
        return 0;
    }
    for (usIndex = usPrefixLength; usIndex < usLength; usIndex++)
    {
        uint8_t ucValue;

        ucValue = pPath[usIndex];
        if (!(('0' <= ucValue && '9' >= ucValue) ||
                ('A' <= ucValue && 'Z' >= ucValue) ||
                ('a' <= ucValue && 'z' >= ucValue) ||
                '_' == ucValue || '-' == ucValue || '.' == ucValue))
        {
            return 0;
        }
    }
    if (usLength < strlen(pExtension) ||
            0 != memcmp(pPath + usLength - strlen(pExtension),
                        pExtension, strlen(pExtension)))
    {
        return 0;
    }

    return 1;
}

/* Local_HexNibble: decode one lowercase or uppercase hexadecimal digit. */
LOCAL int Local_HexNibble(uint8_t ucValue)
{
    if ('0' <= ucValue && '9' >= ucValue)
    {
        return (int)(ucValue - '0');
    }
    if ('a' <= ucValue && 'f' >= ucValue)
    {
        return (int)(ucValue - 'a') + 10;
    }
    if ('A' <= ucValue && 'F' >= ucValue)
    {
        return (int)(ucValue - 'A') + 10;
    }

    return -1;
}

/* Local_ParseWireName: decode "relative/path|sha256hex" from FILE_START.
 * This carries a digest per file because the stock SiFli FILE_INFO state only
 * accepts one digest before a multi-file session, not before every file.
 */
LOCAL int Local_ParseWireName(INPUT const uint8_t *pWireName,
                              uint16_t usWireNameLength,
                              char *pRelativePath,
                              uint8_t *pSha256)
{
    uint16_t usPathLength;
    uint16_t usIndex;

    if (NULL == pWireName || NULL == pRelativePath || NULL == pSha256 ||
            66U > usWireNameLength ||
            RESOURCE_UPDATE_WIRE_NAME_MAX_LENGTH < usWireNameLength)
    {
        return -RT_EINVAL;
    }
    usPathLength = usWireNameLength - 65U;
    if ('|' != pWireName[usPathLength] ||
            0 == Local_IsResourcePathValid(pWireName, usPathLength))
    {
        return -RT_EINVAL;
    }
    memcpy(pRelativePath, pWireName, usPathLength);
    pRelativePath[usPathLength] = '\0';
    for (usIndex = 0U; usIndex < RESOURCE_UPDATE_SHA256_LENGTH; usIndex++)
    {
        int lHigh;
        int lLow;

        lHigh = Local_HexNibble(pWireName[usPathLength + 1U + usIndex * 2U]);
        lLow = Local_HexNibble(pWireName[usPathLength + 2U + usIndex * 2U]);
        if (0 > lHigh || 0 > lLow)
        {
            return -RT_EINVAL;
        }
        pSha256[usIndex] = (uint8_t)((lHigh << 4) | lLow);
    }

    return RT_EOK;
}

/* Local_RemoveStageFiles: close and remove every transaction artifact. */
LOCAL void Local_RemoveStageFiles(void)
{
    uint8_t ucIndex;

    (void)Local_CloseCurrentFile();
    for (ucIndex = 0U; ucIndex < RESOURCE_UPDATE_MAX_FILES; ucIndex++)
    {
        char aStagePath[RESOURCE_UPDATE_FULL_PATH_LENGTH];

        rt_snprintf(aStagePath, sizeof(aStagePath), "%s/%02u.tmp",
                    RESOURCE_UPDATE_STAGE_PATH, ucIndex);
        (void)unlink(aStagePath);
    }
    (void)unlink(RESOURCE_UPDATE_JOURNAL_PATH);
    (void)unlink(RESOURCE_UPDATE_PENDING_PATH);
}

/* Local_ResetTransaction: release resources while preserving diagnostic state. */
LOCAL void Local_ResetTransaction(uint8_t ucState, int16_t sLastError)
{
    Local_RemoveStageFiles();
    memset(l_tResourceUpdateEnv.aFiles, 0, sizeof(l_tResourceUpdateEnv.aFiles));
    l_tResourceUpdateEnv.ulSessionExpected = 0U;
    l_tResourceUpdateEnv.ulSessionReceived = 0U;
    l_tResourceUpdateEnv.ulFileExpected = 0U;
    l_tResourceUpdateEnv.ulFileReceived = 0U;
    l_tResourceUpdateEnv.ulPayloadReceived = 0U;
    l_tResourceUpdateEnv.ucReceivedCrcLength = 0U;
    l_tResourceUpdateEnv.ucFileCount = 0U;
    l_tResourceUpdateEnv.ucCommittedFiles = 0U;
    l_tResourceUpdateEnv.ucCurrentFile = 0U;
    l_tResourceUpdateEnv.ucHasPendingInfo = 0U;
    l_tResourceUpdateEnv.ucState = ucState;
    l_tResourceUpdateEnv.sLastError = sLastError;
}

/* Local_WriteJournal: persist enough information to roll back after reset. */
LOCAL int Local_WriteJournal(void)
{
    int lFileDescriptor;
    uint8_t ucIndex;
    int lRetVal;

    lFileDescriptor = open(RESOURCE_UPDATE_JOURNAL_PATH,
                           O_CREAT | O_RDWR | O_TRUNC | O_BINARY, 0);
    if (0 > lFileDescriptor)
    {
        return -RT_ERROR;
    }
    lRetVal = RT_EOK;
    for (ucIndex = 0U; ucIndex < l_tResourceUpdateEnv.ucCommittedFiles; ucIndex++)
    {
        char aLine[RESOURCE_UPDATE_RELATIVE_PATH_LENGTH + 4U];
        int lLength;

        lLength = rt_snprintf(aLine, sizeof(aLine), "%u %s\n",
                              l_tResourceUpdateEnv.aFiles[ucIndex].ucHadOriginal,
                              l_tResourceUpdateEnv.aFiles[ucIndex].aRelativePath);
        if (0 >= lLength || (size_t)lLength >= sizeof(aLine) ||
                RT_EOK != Local_WriteAll(lFileDescriptor,
                                         (const uint8_t *)aLine,
                                         (uint32_t)lLength))
        {
            lRetVal = -RT_ERROR;
            break;
        }
    }
    if (RT_EOK == lRetVal && 0 != fsync(lFileDescriptor))
    {
        lRetVal = -RT_ERROR;
    }
    if (0 != close(lFileDescriptor))
    {
        lRetVal = -RT_ERROR;
    }

    return lRetVal;
}

/* Local_CommitVersion: replace resource.version only after every file moved. */
LOCAL int Local_CommitVersion(void)
{
    char aVersionText[RESOURCE_UPDATE_VERSION_MAX_LENGTH + 2U];

    rt_snprintf(aVersionText, sizeof(aVersionText), "%s\n",
                l_tResourceUpdateEnv.aTargetVersion);
    if (RT_EOK != Local_WriteTextFile(RESOURCE_UPDATE_VERSION_TEMP_PATH, aVersionText))
    {
        return -RT_ERROR;
    }
    (void)unlink(RESOURCE_UPDATE_VERSION_BACKUP_PATH);
    if (0 == access(RESOURCE_UPDATE_VERSION_PATH, 0) &&
            0 != rename(RESOURCE_UPDATE_VERSION_PATH,
                        RESOURCE_UPDATE_VERSION_BACKUP_PATH))
    {
        (void)unlink(RESOURCE_UPDATE_VERSION_TEMP_PATH);
        return -RT_ERROR;
    }
    if (0 != rename(RESOURCE_UPDATE_VERSION_TEMP_PATH,
                    RESOURCE_UPDATE_VERSION_PATH))
    {
        (void)rename(RESOURCE_UPDATE_VERSION_BACKUP_PATH,
                     RESOURCE_UPDATE_VERSION_PATH);
        return -RT_ERROR;
    }

    return RT_EOK;
}

/* Local_RollbackFiles: restore all old files listed in the current RAM state. */
LOCAL void Local_RollbackFiles(void)
{
    uint8_t ucIndex;

    for (ucIndex = 0U; ucIndex < l_tResourceUpdateEnv.ucFileCount; ucIndex++)
    {
        char aTargetPath[RESOURCE_UPDATE_FULL_PATH_LENGTH];
        char aBackupPath[RESOURCE_UPDATE_FULL_PATH_LENGTH];

        if (RT_EOK != Local_BuildTargetPath(
                l_tResourceUpdateEnv.aFiles[ucIndex].aRelativePath,
                aTargetPath, sizeof(aTargetPath)))
        {
            continue;
        }
        rt_snprintf(aBackupPath, sizeof(aBackupPath), "%s.bak", aTargetPath);
        if (0U != l_tResourceUpdateEnv.aFiles[ucIndex].ucHadOriginal)
        {
            if (0 == access(aBackupPath, 0))
            {
                (void)unlink(aTargetPath);
                (void)rename(aBackupPath, aTargetPath);
            }
        }
        else
        {
            (void)unlink(aTargetPath);
        }
    }
    if (0 == access(RESOURCE_UPDATE_VERSION_BACKUP_PATH, 0))
    {
        (void)unlink(RESOURCE_UPDATE_VERSION_PATH);
        (void)rename(RESOURCE_UPDATE_VERSION_BACKUP_PATH,
                     RESOURCE_UPDATE_VERSION_PATH);
    }
}

/* Local_CommitFiles: journal, replace all resources, then commit version. */
LOCAL int Local_CommitFiles(void)
{
    uint8_t ucIndex;

    for (ucIndex = 0U; ucIndex < l_tResourceUpdateEnv.ucFileCount; ucIndex++)
    {
        char aTargetPath[RESOURCE_UPDATE_FULL_PATH_LENGTH];

        if (RT_EOK != Local_BuildTargetPath(
                l_tResourceUpdateEnv.aFiles[ucIndex].aRelativePath,
                aTargetPath, sizeof(aTargetPath)))
        {
            return -RT_ERROR;
        }
        l_tResourceUpdateEnv.aFiles[ucIndex].ucHadOriginal =
            (0 == access(aTargetPath, 0)) ? 1U : 0U;
    }
    if (RT_EOK != Local_WriteTextFile(RESOURCE_UPDATE_PENDING_PATH,
                                      l_tResourceUpdateEnv.aTargetVersion) ||
            RT_EOK != Local_WriteJournal())
    {
        return -RT_ERROR;
    }

    l_tResourceUpdateEnv.ucState = RESOURCE_UPDATE_STATE_COMMITTING;
    l_tResourceUpdateEnv.ucCommittedFiles = 0U;
    for (ucIndex = 0U; ucIndex < l_tResourceUpdateEnv.ucFileCount; ucIndex++)
    {
        char aTargetPath[RESOURCE_UPDATE_FULL_PATH_LENGTH];
        char aBackupPath[RESOURCE_UPDATE_FULL_PATH_LENGTH];

        if (RT_EOK != Local_BuildTargetPath(
                l_tResourceUpdateEnv.aFiles[ucIndex].aRelativePath,
                aTargetPath, sizeof(aTargetPath)))
        {
            Local_RollbackFiles();
            return -RT_ERROR;
        }
        rt_snprintf(aBackupPath, sizeof(aBackupPath), "%s.bak", aTargetPath);
        (void)unlink(aBackupPath);
        if (0U != l_tResourceUpdateEnv.aFiles[ucIndex].ucHadOriginal &&
                0 != rename(aTargetPath, aBackupPath))
        {
            Local_RollbackFiles();
            return -RT_ERROR;
        }
        if (0 != rename(l_tResourceUpdateEnv.aFiles[ucIndex].aStagePath,
                        aTargetPath))
        {
            Local_RollbackFiles();
            return -RT_ERROR;
        }
        l_tResourceUpdateEnv.ucCommittedFiles++;
    }
    if (RT_EOK != Local_CommitVersion())
    {
        Local_RollbackFiles();
        return -RT_ERROR;
    }

    for (ucIndex = 0U; ucIndex < l_tResourceUpdateEnv.ucFileCount; ucIndex++)
    {
        char aTargetPath[RESOURCE_UPDATE_FULL_PATH_LENGTH];
        char aBackupPath[RESOURCE_UPDATE_FULL_PATH_LENGTH];

        if (RT_EOK == Local_BuildTargetPath(
                l_tResourceUpdateEnv.aFiles[ucIndex].aRelativePath,
                aTargetPath, sizeof(aTargetPath)))
        {
            rt_snprintf(aBackupPath, sizeof(aBackupPath), "%s.bak", aTargetPath);
            (void)unlink(aBackupPath);
        }
    }
    (void)unlink(RESOURCE_UPDATE_VERSION_BACKUP_PATH);
    (void)unlink(RESOURCE_UPDATE_JOURNAL_PATH);
    (void)unlink(RESOURCE_UPDATE_PENDING_PATH);

    return RT_EOK;
}

/* Local_BeginFile: validate path/size and open a fixed-name staging file. */
LOCAL int Local_BeginFile(INPUT const ble_watchface_file_start_ind_t *pInfo)
{
    RESOURCE_UPDATE_FILE *pFile;
    int lLength;
    uint8_t ucIndex;
    char aRelativePath[RESOURCE_UPDATE_RELATIVE_PATH_LENGTH];
    uint8_t aExpectedSha256[RESOURCE_UPDATE_SHA256_LENGTH];

    if (NULL == pInfo ||
            RESOURCE_UPDATE_MAX_FILES <= l_tResourceUpdateEnv.ucFileCount ||
            4U >= pInfo->file_len ||
            l_tResourceUpdateEnv.ulSessionExpected <
                l_tResourceUpdateEnv.ulSessionReceived + pInfo->file_len)
    {
        return BLE_WATCHFACE_STATUS_FILE_INFO_ERROR;
    }

    if (0U != l_tResourceUpdateEnv.ucHasPendingInfo &&
            0 != Local_IsResourcePathValid(pInfo->file_name,
                                           pInfo->file_name_len))
    {
        memcpy(aRelativePath, pInfo->file_name, pInfo->file_name_len);
        aRelativePath[pInfo->file_name_len] = '\0';
        memcpy(aExpectedSha256, l_tResourceUpdateEnv.aPendingSha256,
               sizeof(aExpectedSha256));
    }
    else if (RT_EOK != Local_ParseWireName(pInfo->file_name,
                                           pInfo->file_name_len,
                                           aRelativePath,
                                           aExpectedSha256))
    {
        return BLE_WATCHFACE_STATUS_FILE_INFO_ERROR;
    }

    for (ucIndex = 0U; ucIndex < l_tResourceUpdateEnv.ucFileCount; ucIndex++)
    {
        if (0 == strcmp(l_tResourceUpdateEnv.aFiles[ucIndex].aRelativePath,
                        aRelativePath))
        {
            return BLE_WATCHFACE_STATUS_FILE_PATH_ERROR;
        }
    }

    pFile = &l_tResourceUpdateEnv.aFiles[l_tResourceUpdateEnv.ucFileCount];
    memset(pFile, 0, sizeof(*pFile));
    rt_strncpy(pFile->aRelativePath, aRelativePath,
               sizeof(pFile->aRelativePath) - 1U);
    memcpy(pFile->aExpectedSha256, aExpectedSha256,
           sizeof(pFile->aExpectedSha256));
    pFile->ulPayloadSize = pInfo->file_len - 4U;
    lLength = rt_snprintf(pFile->aStagePath, sizeof(pFile->aStagePath),
                          "%s/%02u.tmp", RESOURCE_UPDATE_STAGE_PATH,
                          l_tResourceUpdateEnv.ucFileCount);
    if (0 >= lLength || (size_t)lLength >= sizeof(pFile->aStagePath))
    {
        return BLE_WATCHFACE_STATUS_FILE_PATH_ERROR;
    }
    (void)unlink(pFile->aStagePath);
    l_tResourceUpdateEnv.lFileDescriptor = open(
        pFile->aStagePath, O_CREAT | O_RDWR | O_TRUNC | O_BINARY, 0);
    if (0 > l_tResourceUpdateEnv.lFileDescriptor)
    {
        return BLE_WATCHFACE_STATUS_FILE_OPEN_ERROR;
    }

    mbedtls_sha256_init(&l_tResourceUpdateEnv.tSha256);
    mbedtls_sha256_starts(&l_tResourceUpdateEnv.tSha256, 0);
    l_tResourceUpdateEnv.ucSha256Active = 1U;
    l_tResourceUpdateEnv.ulFileExpected = pInfo->file_len;
    l_tResourceUpdateEnv.ulFileReceived = 0U;
    l_tResourceUpdateEnv.ulPayloadReceived = 0U;
    l_tResourceUpdateEnv.ulCrc = RESOURCE_UPDATE_CRC_INITIAL_VALUE;
    l_tResourceUpdateEnv.ucReceivedCrcLength = 0U;
    l_tResourceUpdateEnv.ucCurrentFile = l_tResourceUpdateEnv.ucFileCount;
    l_tResourceUpdateEnv.ucHasPendingInfo = 0U;

    return BLE_WATCHFACE_STATUS_OK;
}

/* Local_WriteChunk: stream payload to FAT and retain the final four CRC bytes. */
LOCAL int Local_WriteChunk(INPUT const uint8_t *pData, uint32_t ulLength)
{
    RESOURCE_UPDATE_FILE *pFile;
    uint32_t ulPayloadLength;
    uint32_t ulCrcLength;

    if (NULL == pData || 0U == ulLength ||
            0 > l_tResourceUpdateEnv.lFileDescriptor ||
            l_tResourceUpdateEnv.ulFileExpected <
                l_tResourceUpdateEnv.ulFileReceived + ulLength)
    {
        return BLE_WATCHFACE_STATUS_FILE_SIZE_ERROR;
    }
    pFile = &l_tResourceUpdateEnv.aFiles[l_tResourceUpdateEnv.ucCurrentFile];
    ulPayloadLength = pFile->ulPayloadSize -
                      l_tResourceUpdateEnv.ulPayloadReceived;
    if (ulPayloadLength > ulLength)
    {
        ulPayloadLength = ulLength;
    }
    if (0U < ulPayloadLength)
    {
        if (RT_EOK != Local_WriteAll(l_tResourceUpdateEnv.lFileDescriptor,
                                     pData, ulPayloadLength))
        {
            return BLE_WATCHFACE_STATUS_FILE_WRITE_ERROR;
        }
        mbedtls_sha256_update(&l_tResourceUpdateEnv.tSha256,
                              pData, ulPayloadLength);
        l_tResourceUpdateEnv.ulCrc = Local_Crc32Mpeg2(
            pData, ulPayloadLength, l_tResourceUpdateEnv.ulCrc);
        l_tResourceUpdateEnv.ulPayloadReceived += ulPayloadLength;
    }
    ulCrcLength = ulLength - ulPayloadLength;
    if (sizeof(l_tResourceUpdateEnv.aReceivedCrc) <
            l_tResourceUpdateEnv.ucReceivedCrcLength + ulCrcLength)
    {
        return BLE_WATCHFACE_STATUS_FILE_SIZE_ERROR;
    }
    if (0U < ulCrcLength)
    {
        memcpy(l_tResourceUpdateEnv.aReceivedCrc +
                   l_tResourceUpdateEnv.ucReceivedCrcLength,
               pData + ulPayloadLength, ulCrcLength);
        l_tResourceUpdateEnv.ucReceivedCrcLength += (uint8_t)ulCrcLength;
    }
    l_tResourceUpdateEnv.ulFileReceived += ulLength;
    l_tResourceUpdateEnv.ulSessionReceived += ulLength;

    return BLE_WATCHFACE_STATUS_OK;
}

/* Local_EndFile: flush, finish SHA-256 and validate both independent checks. */
LOCAL int Local_EndFile(INPUT const ble_watchface_file_end_ind_t *pInfo)
{
    RESOURCE_UPDATE_FILE *pFile;
    uint8_t aActualSha256[RESOURCE_UPDATE_SHA256_LENGTH];

    pFile = &l_tResourceUpdateEnv.aFiles[l_tResourceUpdateEnv.ucCurrentFile];
    if (NULL == pInfo || BLE_WATCHFACE_STATUS_OK != pInfo->end_status ||
            l_tResourceUpdateEnv.ulFileReceived !=
                l_tResourceUpdateEnv.ulFileExpected ||
            l_tResourceUpdateEnv.ulPayloadReceived != pFile->ulPayloadSize ||
            sizeof(l_tResourceUpdateEnv.aReceivedCrc) !=
                l_tResourceUpdateEnv.ucReceivedCrcLength)
    {
        (void)Local_CloseCurrentFile();
        return BLE_WATCHFACE_STATUS_FILE_SIZE_ERROR;
    }
    if (Local_ReadU32BigEndian(l_tResourceUpdateEnv.aReceivedCrc) !=
            l_tResourceUpdateEnv.ulCrc)
    {
        (void)Local_CloseCurrentFile();
        return BLE_WATCHFACE_STATUS_CRC_CALCULATE_ERROR;
    }
    mbedtls_sha256_finish(&l_tResourceUpdateEnv.tSha256, aActualSha256);
    l_tResourceUpdateEnv.ucSha256Active = 0U;
    mbedtls_sha256_free(&l_tResourceUpdateEnv.tSha256);
    if (0 != memcmp(aActualSha256, pFile->aExpectedSha256,
                    sizeof(aActualSha256)))
    {
        (void)Local_CloseCurrentFile();
        return BLE_WATCHFACE_STATUS_CRC_CALCULATE_ERROR;
    }
    if (RT_EOK != Local_CloseCurrentFile())
    {
        return BLE_WATCHFACE_STATUS_FILE_CLOSE_ERROR;
    }
    l_tResourceUpdateEnv.ucFileCount++;

    return BLE_WATCHFACE_STATUS_OK;
}

/* RESUPDATE_Begin: validate base version and prepare an empty staging area. */
int RESUPDATE_Begin(INPUT const char *pBaseVersion,
                    INPUT const char *pTargetVersion)
{
    char aCurrentVersion[RESOURCE_UPDATE_VERSION_MAX_LENGTH + 1U];

    if (0 == Local_IsVersionValid(pBaseVersion) ||
            0 == Local_IsVersionValid(pTargetVersion) ||
            0 == strcmp(pBaseVersion, pTargetVersion))
    {
        return -RT_EINVAL;
    }
    if (RESOURCE_UPDATE_STATE_RECEIVING == l_tResourceUpdateEnv.ucState ||
            RESOURCE_UPDATE_STATE_COMMITTING == l_tResourceUpdateEnv.ucState)
    {
        return -RT_EBUSY;
    }
    if (RT_EOK != Local_ReadVersion(aCurrentVersion,
                                    sizeof(aCurrentVersion)) ||
            0 != strcmp(aCurrentVersion, pBaseVersion))
    {
        return -RT_EINVAL;
    }
    if (0 != mkdir(RESOURCE_UPDATE_STAGE_PATH, 0777) &&
            0 != access(RESOURCE_UPDATE_STAGE_PATH, 0))
    {
        return -RT_ERROR;
    }

    Local_ResetTransaction(RESOURCE_UPDATE_STATE_PREPARED, 0);
    rt_strncpy(l_tResourceUpdateEnv.aBaseVersion, pBaseVersion,
               sizeof(l_tResourceUpdateEnv.aBaseVersion) - 1U);
    rt_strncpy(l_tResourceUpdateEnv.aTargetVersion, pTargetVersion,
               sizeof(l_tResourceUpdateEnv.aTargetVersion) - 1U);
    l_tResourceUpdateEnv.aBaseVersion[
        sizeof(l_tResourceUpdateEnv.aBaseVersion) - 1U] = '\0';
    l_tResourceUpdateEnv.aTargetVersion[
        sizeof(l_tResourceUpdateEnv.aTargetVersion) - 1U] = '\0';

    return RT_EOK;
}

/* RESUPDATE_Cancel: abort and delete uncommitted transaction data. */
int RESUPDATE_Cancel(void)
{
    if (RESOURCE_UPDATE_STATE_COMMITTING == l_tResourceUpdateEnv.ucState)
    {
        return -RT_EBUSY;
    }
    if (RESOURCE_UPDATE_STATE_RECEIVING == l_tResourceUpdateEnv.ucState)
    {
        (void)ble_watchface_abort();
    }
    Local_ResetTransaction(RESOURCE_UPDATE_STATE_IDLE,
                           BLE_WATCHFACE_STATUS_USER_ABORT);

    return RT_EOK;
}

/* RESUPDATE_Status: report version, state, received bytes and last error. */
int RESUPDATE_Status(char *pResult, size_t ulResultSize)
{
    char aVersion[RESOURCE_UPDATE_VERSION_MAX_LENGTH + 1U];

    if (NULL == pResult || 0U == ulResultSize)
    {
        return -RT_EINVAL;
    }
    if (RT_EOK != Local_ReadVersion(aVersion, sizeof(aVersion)))
    {
        rt_strncpy(aVersion, "unknown", sizeof(aVersion) - 1U);
        aVersion[sizeof(aVersion) - 1U] = '\0';
    }
    rt_snprintf(pResult, ulResultSize, "v=%s;s=%u;r=%lu;e=%d",
                aVersion,
                l_tResourceUpdateEnv.ucState,
                (unsigned long)l_tResourceUpdateEnv.ulSessionReceived,
                l_tResourceUpdateEnv.sLastError);

    return RT_EOK;
}

/* RESUPDATE_IsPrepared: expose whether customized transfer may start. */
uint8_t RESUPDATE_IsPrepared(void)
{
    return (RESOURCE_UPDATE_STATE_PREPARED == l_tResourceUpdateEnv.ucState) ?
           1U : 0U;
}

/* RESUPDATE_HandleWatchfaceEvent: handle one customized WFPUSH event. */
watchface_event_ack_t RESUPDATE_HandleWatchfaceEvent(uint16_t usEvent,
                                                     uint16_t usLength,
                                                     void *pParameter)
{
    int lResult;

    (void)usLength;
    lResult = BLE_WATCHFACE_STATUS_OK;
    switch (usEvent)
    {
    case WATCHFACE_APP_START:
    {
        ble_watchface_start_ind_t *pInfo;
        struct statfs tFileSystem;
        uint64_t udAvailableBytes;

        pInfo = (ble_watchface_start_ind_t *)pParameter;
        if (NULL == pInfo || WATCHFACE_FILE_TYPE_CUSTOMIZED != pInfo->type ||
                RESOURCE_UPDATE_STATE_PREPARED != l_tResourceUpdateEnv.ucState ||
                4U >= pInfo->all_files_len ||
                RESOURCE_UPDATE_MAX_PAYLOAD_BYTES < pInfo->all_files_len ||
                0 != dfs_statfs(RESOURCE_UPDATE_ROOT_PATH, &tFileSystem))
        {
            lResult = BLE_WATCHFACE_STATUS_FILE_TYPE_ERROR;
            ble_watchface_send_start_rsp_file_info(lResult, 0U, 0U);
            break;
        }
        udAvailableBytes = (uint64_t)tFileSystem.f_bsize *
                           (uint64_t)tFileSystem.f_bfree;
        if (udAvailableBytes < (uint64_t)pInfo->all_files_len +
                               RESOURCE_UPDATE_IO_RESERVE_BYTES)
        {
            lResult = BLE_WATCHFACE_STATUS_SPACE_ERROR;
            ble_watchface_send_start_rsp_file_info(lResult,
                                                   tFileSystem.f_bsize,
                                                   tFileSystem.f_bfree);
            break;
        }
        l_tResourceUpdateEnv.ulSessionExpected = pInfo->all_files_len;
        l_tResourceUpdateEnv.ulSessionReceived = 0U;
        l_tResourceUpdateEnv.ucState = RESOURCE_UPDATE_STATE_RECEIVING;
        ble_watchface_send_start_rsp_file_info(lResult,
                                               tFileSystem.f_bsize,
                                               tFileSystem.f_bfree);
        break;
    }
    case WATCHFACE_APP_FILE_INFO:
    {
        ble_watchface_file_info_ind_t *pInfo;

        pInfo = (ble_watchface_file_info_ind_t *)pParameter;
        if (NULL == pInfo || RESOURCE_UPDATE_STATE_RECEIVING !=
                l_tResourceUpdateEnv.ucState || 0U == pInfo->file_blocks ||
                0 <= l_tResourceUpdateEnv.lFileDescriptor)
        {
            lResult = BLE_WATCHFACE_STATUS_FILE_INFO_ERROR;
        }
        else
        {
            memcpy(l_tResourceUpdateEnv.aPendingSha256, pInfo->md5,
                   sizeof(l_tResourceUpdateEnv.aPendingSha256));
            l_tResourceUpdateEnv.ucHasPendingInfo = 1U;
        }
        ble_watchface_file_info_rsp(lResult);
        break;
    }
    case WATCHFACE_APP_FILE_START:
        lResult = Local_BeginFile(
            (const ble_watchface_file_start_ind_t *)pParameter);
        ble_watchface_file_start_rsp(lResult);
        break;
    case WATCHFACE_APP_FILE_DOWNLOAD:
    {
        ble_watchface_file_download_ind_t *pChunk;

        pChunk = (ble_watchface_file_download_ind_t *)pParameter;
        if (NULL == pChunk)
        {
            lResult = BLE_WATCHFACE_STATUS_BLE_PARAMETERS_NULL;
        }
        else
        {
            lResult = Local_WriteChunk(pChunk->data, pChunk->data_len);
        }
        ble_watchface_file_download_rsp(lResult);
        break;
    }
    case WATCHFACE_APP_FILE_END:
        lResult = Local_EndFile(
            (const ble_watchface_file_end_ind_t *)pParameter);
        ble_watchface_file_end_rsp(lResult);
        break;
    case WATCHFACE_APP_END:
        if (0U == l_tResourceUpdateEnv.ucFileCount ||
                l_tResourceUpdateEnv.ulSessionReceived !=
                    l_tResourceUpdateEnv.ulSessionExpected ||
                0 <= l_tResourceUpdateEnv.lFileDescriptor)
        {
            lResult = BLE_WATCHFACE_STATUS_FILE_SIZE_ERROR;
        }
        else if (RT_EOK != Local_CommitFiles())
        {
            lResult = BLE_WATCHFACE_STATUS_FILE_WRITE_ERROR;
        }
        ble_watchface_end_rsp(lResult);
        if (BLE_WATCHFACE_STATUS_OK == lResult)
        {
            Local_ResetTransaction(RESOURCE_UPDATE_STATE_COMPLETE, 0);
        }
        break;
    case WATCHFACE_APP_ERROR:
        if (NULL != pParameter)
        {
            lResult = ((ble_watchface_error_ind_t *)pParameter)->error_type;
        }
        else
        {
            lResult = BLE_WATCHFACE_STATUS_GENERAL_ERROR;
        }
        break;
    default:
        return WATCHFACE_EVENT_SUCCESSED;
    }

    if (BLE_WATCHFACE_STATUS_OK != lResult)
    {
        LOG_E("resource update event=%u error=%d", usEvent, lResult);
        Local_ResetTransaction(RESOURCE_UPDATE_STATE_ERROR, (int16_t)lResult);
        (void)ble_watchface_abort();
        return WATCHFACE_EVENT_FAILED;
    }

    return WATCHFACE_EVENT_SUCCESSED;
}

/* Local_RecoverTransaction: roll back a reset during the commit window. */
LOCAL int Local_RecoverTransaction(void)
{
    FILE *pJournal;
    char aCurrentVersion[RESOURCE_UPDATE_VERSION_MAX_LENGTH + 1U];
    char aPendingVersion[RESOURCE_UPDATE_VERSION_MAX_LENGTH + 1U];
    int lPendingDescriptor;
    int lReadLength;
    int lCommitComplete;
    char aLine[RESOURCE_UPDATE_RELATIVE_PATH_LENGTH + 4U];

    if (0 != access(RESOURCE_UPDATE_JOURNAL_PATH, 0))
    {
        if (0 == access(RESOURCE_UPDATE_VERSION_BACKUP_PATH, 0) &&
                0 != access(RESOURCE_UPDATE_VERSION_PATH, 0))
        {
            (void)rename(RESOURCE_UPDATE_VERSION_BACKUP_PATH,
                         RESOURCE_UPDATE_VERSION_PATH);
        }
        return RT_EOK;
    }
    memset(aPendingVersion, 0, sizeof(aPendingVersion));
    lPendingDescriptor = open(RESOURCE_UPDATE_PENDING_PATH,
                              O_RDONLY | O_BINARY, 0);
    if (0 > lPendingDescriptor)
    {
        return -RT_ERROR;
    }
    lReadLength = read(lPendingDescriptor, aPendingVersion,
                       sizeof(aPendingVersion) - 1U);
    (void)close(lPendingDescriptor);
    if (0 >= lReadLength)
    {
        return -RT_ERROR;
    }
    aPendingVersion[lReadLength] = '\0';
    lCommitComplete = (RT_EOK == Local_ReadVersion(
        aCurrentVersion, sizeof(aCurrentVersion)) &&
        0 == strcmp(aCurrentVersion, aPendingVersion));

    pJournal = fopen(RESOURCE_UPDATE_JOURNAL_PATH, "rb");
    if (NULL == pJournal)
    {
        return -RT_ERROR;
    }
    while (NULL != fgets(aLine, sizeof(aLine), pJournal))
    {
        char *pRelativePath;
        char *pLineEnd;
        int lHadOriginal;
        char aTargetPath[RESOURCE_UPDATE_FULL_PATH_LENGTH];
        char aBackupPath[RESOURCE_UPDATE_FULL_PATH_LENGTH];

        lHadOriginal = ('1' == aLine[0]) ? 1 : 0;
        pRelativePath = aLine + 2;
        pLineEnd = strchr(pRelativePath, '\n');
        if (NULL != pLineEnd)
        {
            *pLineEnd = '\0';
        }
        if (0 == Local_IsResourcePathValid((const uint8_t *)pRelativePath,
                                           (uint16_t)strlen(pRelativePath)) ||
                RT_EOK != Local_BuildTargetPath(pRelativePath,
                                                aTargetPath,
                                                sizeof(aTargetPath)))
        {
            continue;
        }
        rt_snprintf(aBackupPath, sizeof(aBackupPath), "%s.bak", aTargetPath);
        if (0 != lCommitComplete)
        {
            (void)unlink(aBackupPath);
        }
        else if (0 != lHadOriginal && 0 == access(aBackupPath, 0))
        {
            (void)unlink(aTargetPath);
            (void)rename(aBackupPath, aTargetPath);
        }
        else if (0 == lHadOriginal)
        {
            (void)unlink(aTargetPath);
        }
    }
    (void)fclose(pJournal);
    if (0 == lCommitComplete &&
            0 == access(RESOURCE_UPDATE_VERSION_BACKUP_PATH, 0))
    {
        (void)unlink(RESOURCE_UPDATE_VERSION_PATH);
        (void)rename(RESOURCE_UPDATE_VERSION_BACKUP_PATH,
                     RESOURCE_UPDATE_VERSION_PATH);
    }
    else
    {
        (void)unlink(RESOURCE_UPDATE_VERSION_BACKUP_PATH);
    }
    Local_RemoveStageFiles();

    return RT_EOK;
}

/* RESUPDATE_Init: recover any power-loss window before accepting App traffic. */
LOCAL int RESUPDATE_Init(void)
{
    if (0 != mkdir(RESOURCE_UPDATE_STAGE_PATH, 0777) &&
            0 != access(RESOURCE_UPDATE_STAGE_PATH, 0))
    {
        return -RT_ERROR;
    }
    if (RT_EOK != Local_RecoverTransaction())
    {
        LOG_E("resource transaction recovery failed");
        return -RT_ERROR;
    }
    Local_ResetTransaction(RESOURCE_UPDATE_STATE_IDLE, 0);

    return RT_EOK;
}
INIT_APP_EXPORT(RESUPDATE_Init);
