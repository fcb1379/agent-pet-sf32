#include <rtthread.h>
#include <stdint.h>
#include <sys/stat.h>
#include <string.h>

#include "dfs_fs.h"
#include "dfs_posix.h"
#include "littlevgl2rtt.h"
#include "lv_ext_resource_manager.h"
#include "gui_app_fwk.h"

#define APP_ID                         "file_manager"
#define FILE_MANAGER_DEVICE_NAME       "sd0"
#define FILE_MANAGER_ROOT_PATH         "/sdcard"
#define FILE_MANAGER_LVGL_PREFIX       "/:"
#define FILE_MANAGER_PATH_SIZE         (256U)
#define FILE_MANAGER_IMAGE_PATH_SIZE   (FILE_MANAGER_PATH_SIZE + 3U)
#define FILE_MANAGER_NAME_SIZE         (96U)
#define FILE_MANAGER_ENTRY_MAX         (48U)
#define FILE_MANAGER_ROW_HEIGHT        (54)
#define FILE_MANAGER_HEADER_HEIGHT     (72)
#define FILE_MANAGER_STATUS_HEIGHT     (30)

typedef enum
{
    FILE_MANAGER_ENTRY_FILE = 0,
    FILE_MANAGER_ENTRY_DIRECTORY,
    FILE_MANAGER_ENTRY_IMAGE,
} FILE_MANAGER_ENTRY_TYPE;

/* FILE_MANAGER_ENTRY: bounded directory metadata used by GUI callbacks. */
typedef struct _FILE_MANAGER_ENTRY
{
    char aName[FILE_MANAGER_NAME_SIZE];
    FILE_MANAGER_ENTRY_TYPE eType;
} FILE_MANAGER_ENTRY;

/* FILE_MANAGER_UI: complete state for one file-manager application instance. */
typedef struct _FILE_MANAGER_UI
{
    lv_obj_t *pRoot;
    lv_obj_t *pPathLabel;
    lv_obj_t *pStatusLabel;
    lv_obj_t *pList;
    lv_obj_t *pViewer;
    char aCurrentPath[FILE_MANAGER_PATH_SIZE];
    char aImagePath[FILE_MANAGER_IMAGE_PATH_SIZE];
    FILE_MANAGER_ENTRY aEntries[FILE_MANAGER_ENTRY_MAX];
    uint16_t usEntryCount;
} FILE_MANAGER_UI;

/* UI state is valid only between ONSTART and ONSTOP. */
static FILE_MANAGER_UI l_tFileManagerUi;

/* The card remains mounted while switching between applications. */
static bool l_bTfMounted;

extern int rt_spi_msd_init(void);

static void FileManager_Refresh(void);

/***************************
 * FileManager_SetStatus: update the status line.
 * Parameters: pText is zero-terminated status text.
 * Return: none.
 ***************************/
static void FileManager_SetStatus(const char *pText)
{
    if ((RT_NULL != pText) && (RT_NULL != l_tFileManagerUi.pStatusLabel))
    {
        lv_label_set_text(l_tFileManagerUi.pStatusLabel, pText);
    }

    return;
}

/***************************
 * FileManager_ToLower: convert one ASCII character to lower case.
 * Parameters: cValue is the input character.
 * Return: lower-case ASCII or the original byte.
 ***************************/
static char FileManager_ToLower(char cValue)
{
    char cResult;

    cResult = cValue;
    if (('A' <= cValue) && ('Z' >= cValue))
    {
        cResult = (char)(cValue + ('a' - 'A'));
    }

    return cResult;
}

/***************************
 * FileManager_HasExtension: compare a suffix without ASCII case sensitivity.
 * Parameters: pName is a file name; pExtension includes the leading dot.
 * Return: true when the suffix matches.
 ***************************/
static bool FileManager_HasExtension(const char *pName, const char *pExtension)
{
    size_t ulNameLength;
    size_t ulExtensionLength;
    size_t ulIndex;

    if ((RT_NULL == pName) || (RT_NULL == pExtension))
    {
        return false;
    }

    ulNameLength = rt_strlen(pName);
    ulExtensionLength = rt_strlen(pExtension);
    if (ulNameLength < ulExtensionLength)
    {
        return false;
    }

    for (ulIndex = 0U; ulIndex < ulExtensionLength; ulIndex++)
    {
        if (FileManager_ToLower(pName[ulNameLength - ulExtensionLength + ulIndex]) !=
                FileManager_ToLower(pExtension[ulIndex]))
        {
            return false;
        }
    }

    return true;
}

/***************************
 * FileManager_IsImage: identify formats enabled in LVGL.
 * Parameters: pName is the file name.
 * Return: true for JPG/JPEG or BMP.
 ***************************/
static bool FileManager_IsImage(const char *pName)
{
    return FileManager_HasExtension(pName, ".jpg") ||
           FileManager_HasExtension(pName, ".jpeg") ||
           FileManager_HasExtension(pName, ".bmp");
}

/***************************
 * FileManager_BuildPath: append a child name to a checked absolute path.
 * Parameters: output buffer/capacity, parent directory, and child name.
 * Return: true on success; false if invalid or too long.
 ***************************/
static bool FileManager_BuildPath(char *pOutput,
                                  size_t ulOutputSize,
                                  const char *pDirectory,
                                  const char *pName)
{
    int lResult;

    if ((RT_NULL == pOutput) || (0U == ulOutputSize) ||
            (RT_NULL == pDirectory) || (RT_NULL == pName))
    {
        return false;
    }

    lResult = rt_snprintf(pOutput, ulOutputSize, "%s/%s", pDirectory, pName);
    if ((0 > lResult) || ((size_t)lResult >= ulOutputSize))
    {
        pOutput[0] = '\0';
        return false;
    }

    return true;
}

/***************************
 * FileManager_MountTf: initialize SPI TF and mount its FAT volume.
 * The card is never formatted automatically, protecting user data.
 * Parameters: none.
 * Return: true when /sdcard is ready.
 ***************************/
static bool FileManager_MountTf(void)
{
#ifndef BSP_USING_PC_SIMULATOR
    rt_device_t pDevice;
    int lResult;

    if (l_bTfMounted)
    {
        return true;
    }

    pDevice = rt_device_find(FILE_MANAGER_DEVICE_NAME);
    if (RT_NULL == pDevice)
    {
        lResult = rt_spi_msd_init();
        if (RT_EOK != lResult)
        {
            rt_kprintf("[TF] SPI card initialization failed: %d\n", lResult);
            return false;
        }

        pDevice = rt_device_find(FILE_MANAGER_DEVICE_NAME);
        if (RT_NULL == pDevice)
        {
            rt_kprintf("[TF] sd0 was not registered\n");
            return false;
        }
    }

    (void)mkdir(FILE_MANAGER_ROOT_PATH, 0);
    lResult = dfs_mount(FILE_MANAGER_DEVICE_NAME,
                        FILE_MANAGER_ROOT_PATH,
                        "elm",
                        0,
                        RT_NULL);
    if (0 != lResult)
    {
        rt_kprintf("[TF] mount failed: %d errno=%d\n", lResult, rt_get_errno());
        return false;
    }

    l_bTfMounted = true;
    rt_kprintf("[TF] mounted %s on %s\n",
               FILE_MANAGER_DEVICE_NAME,
               FILE_MANAGER_ROOT_PATH);
    return true;
#else
    (void)l_bTfMounted;
    return false;
#endif
}

/***************************
 * FileManager_CloseViewer: close the image preview.
 * Parameters: pEvent is optional when called internally.
 * Return: none.
 ***************************/
static void FileManager_CloseViewer(lv_event_t *pEvent)
{
    if ((RT_NULL != pEvent) && (LV_EVENT_CLICKED != lv_event_get_code(pEvent)))
    {
        return;
    }

    if (RT_NULL != l_tFileManagerUi.pViewer)
    {
        lv_obj_del(l_tFileManagerUi.pViewer);
        l_tFileManagerUi.pViewer = RT_NULL;
        l_tFileManagerUi.aImagePath[0] = '\0';
    }

    return;
}

/***************************
 * FileManager_OpenImage: validate, decode, and display a TF image.
 * Parameters: pFilePath is an absolute path below /sdcard.
 * Return: none; failures are shown on the status line.
 ***************************/
static void FileManager_OpenImage(const char *pFilePath)
{
    lv_img_header_t tHeader;
    lv_obj_t *pCloseButton;
    lv_obj_t *pCloseLabel;
    lv_obj_t *pImage;
    lv_obj_t *pNameLabel;
    uint32_t ulZoomWidth;
    uint32_t ulZoomHeight;
    uint32_t ulZoom;
    int lResult;

    if (RT_NULL == pFilePath)
    {
        FileManager_SetStatus("Invalid image path");
        return;
    }

    lResult = rt_snprintf(l_tFileManagerUi.aImagePath,
                          sizeof(l_tFileManagerUi.aImagePath),
                          "%s%s",
                          FILE_MANAGER_LVGL_PREFIX,
                          pFilePath);
    if ((0 > lResult) || ((size_t)lResult >= sizeof(l_tFileManagerUi.aImagePath)))
    {
        FileManager_SetStatus("Image path is too long");
        return;
    }

    rt_memset(&tHeader, 0, sizeof(tHeader));
    if (LV_RES_OK != lv_img_decoder_get_info(l_tFileManagerUi.aImagePath, &tHeader))
    {
        FileManager_SetStatus("Image decode failed");
        l_tFileManagerUi.aImagePath[0] = '\0';
        return;
    }

    FileManager_CloseViewer(RT_NULL);
    l_tFileManagerUi.pViewer = lv_obj_create(l_tFileManagerUi.pRoot);
    lv_obj_set_size(l_tFileManagerUi.pViewer, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_set_pos(l_tFileManagerUi.pViewer, 0, 0);
    lv_obj_set_style_bg_color(l_tFileManagerUi.pViewer, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(l_tFileManagerUi.pViewer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(l_tFileManagerUi.pViewer, 0, 0);
    lv_obj_set_style_pad_all(l_tFileManagerUi.pViewer, 0, 0);
    lv_obj_clear_flag(l_tFileManagerUi.pViewer, LV_OBJ_FLAG_SCROLLABLE);

    pCloseButton = lv_btn_create(l_tFileManagerUi.pViewer);
    lv_obj_set_size(pCloseButton, 52, 44);
    lv_obj_set_pos(pCloseButton, 8, 8);
    lv_obj_add_event_cb(pCloseButton, FileManager_CloseViewer, LV_EVENT_CLICKED, RT_NULL);
    pCloseLabel = lv_label_create(pCloseButton);
    lv_label_set_text(pCloseLabel, LV_SYMBOL_CLOSE);
    lv_obj_center(pCloseLabel);

    pNameLabel = lv_label_create(l_tFileManagerUi.pViewer);
    lv_obj_set_size(pNameLabel, LV_HOR_RES_MAX - 76, 44);
    lv_obj_set_pos(pNameLabel, 68, 12);
    lv_label_set_long_mode(pNameLabel, LV_LABEL_LONG_DOT);
    lv_label_set_text(pNameLabel, pFilePath);
    lv_obj_set_style_text_color(pNameLabel, lv_color_white(), 0);
    lv_obj_set_style_text_font(pNameLabel, &lv_font_montserrat_20, 0);

    pImage = lv_img_create(l_tFileManagerUi.pViewer);
    lv_img_set_src(pImage, l_tFileManagerUi.aImagePath);
    ulZoomWidth = (0U < tHeader.w) ?
                  ((uint32_t)(LV_HOR_RES_MAX - 16) * 256U / tHeader.w) : 256U;
    ulZoomHeight = (0U < tHeader.h) ?
                   ((uint32_t)(LV_VER_RES_MAX - 72) * 256U / tHeader.h) : 256U;
    ulZoom = (ulZoomWidth < ulZoomHeight) ? ulZoomWidth : ulZoomHeight;
    if (256U < ulZoom)
    {
        ulZoom = 256U;
    }
    if (16U > ulZoom)
    {
        ulZoom = 16U;
    }
    lv_img_set_zoom(pImage, (uint16_t)ulZoom);
    lv_obj_align(pImage, LV_ALIGN_CENTER, 0, 28);
    lv_obj_move_foreground(pCloseButton);
    lv_obj_move_foreground(pNameLabel);
    FileManager_SetStatus("Image opened");

    return;
}

/***************************
 * FileManager_OpenEntry: enter a directory or preview an image.
 * Parameters: pEvent carries stable entry metadata.
 * Return: none.
 ***************************/
static void FileManager_OpenEntry(lv_event_t *pEvent)
{
    FILE_MANAGER_ENTRY *pEntry;
    char aPath[FILE_MANAGER_PATH_SIZE];

    if ((RT_NULL == pEvent) || (LV_EVENT_CLICKED != lv_event_get_code(pEvent)))
    {
        return;
    }

    pEntry = (FILE_MANAGER_ENTRY *)lv_event_get_user_data(pEvent);
    if (RT_NULL == pEntry)
    {
        return;
    }

    if (!FileManager_BuildPath(aPath,
                               sizeof(aPath),
                               l_tFileManagerUi.aCurrentPath,
                               pEntry->aName))
    {
        FileManager_SetStatus("Path is too long");
        return;
    }

    if (FILE_MANAGER_ENTRY_DIRECTORY == pEntry->eType)
    {
        rt_snprintf(l_tFileManagerUi.aCurrentPath,
                    sizeof(l_tFileManagerUi.aCurrentPath),
                    "%s",
                    aPath);
        FileManager_Refresh();
    }
    else if (FILE_MANAGER_ENTRY_IMAGE == pEntry->eType)
    {
        FileManager_OpenImage(aPath);
    }
    else
    {
        FileManager_SetStatus("This file type cannot be previewed");
    }

    return;
}

/***************************
 * FileManager_AddRow: create one file-list row.
 * Parameters: pEntry points to stable bounded metadata.
 * Return: none.
 ***************************/
static void FileManager_AddRow(FILE_MANAGER_ENTRY *pEntry)
{
    lv_obj_t *pButton;
    lv_obj_t *pLabel;
    const char *pSymbol;

    if ((RT_NULL == pEntry) || (RT_NULL == l_tFileManagerUi.pList))
    {
        return;
    }

    if (FILE_MANAGER_ENTRY_DIRECTORY == pEntry->eType)
    {
        pSymbol = LV_SYMBOL_DIRECTORY;
    }
    else if (FILE_MANAGER_ENTRY_IMAGE == pEntry->eType)
    {
        pSymbol = LV_SYMBOL_IMAGE;
    }
    else
    {
        pSymbol = LV_SYMBOL_FILE;
    }

    pButton = lv_btn_create(l_tFileManagerUi.pList);
    lv_obj_set_width(pButton, LV_PCT(100));
    lv_obj_set_height(pButton, FILE_MANAGER_ROW_HEIGHT);
    lv_obj_set_style_bg_color(pButton, lv_color_hex(0x182332), 0);
    lv_obj_set_style_bg_opa(pButton, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(pButton, 0, 0);
    lv_obj_set_style_radius(pButton, 6, 0);
    lv_obj_set_style_pad_hor(pButton, 12, 0);
    lv_obj_add_event_cb(pButton, FileManager_OpenEntry, LV_EVENT_CLICKED, pEntry);

    pLabel = lv_label_create(pButton);
    lv_obj_set_width(pLabel, LV_PCT(100));
    lv_label_set_long_mode(pLabel, LV_LABEL_LONG_DOT);
    lv_label_set_text_fmt(pLabel, "%s  %s", pSymbol, pEntry->aName);
    lv_obj_set_style_text_font(pLabel, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(pLabel, lv_color_white(), 0);
    lv_obj_align(pLabel, LV_ALIGN_LEFT_MID, 0, 0);

    return;
}

/***************************
 * FileManager_Refresh: mount TF, enumerate the current folder, rebuild the list.
 * File contents are never loaded during enumeration.
 * Parameters: none.
 * Return: none.
 ***************************/
static void FileManager_Refresh(void)
{
    DIR *pDirectory;
    struct dirent *pDirectoryEntry;
    struct stat tFileStat;
    FILE_MANAGER_ENTRY *pEntry;
    char aFullPath[FILE_MANAGER_PATH_SIZE];
    char aStatus[64];
    uint16_t usSkippedCount;

    if ((RT_NULL == l_tFileManagerUi.pList) ||
            (RT_NULL == l_tFileManagerUi.pPathLabel))
    {
        return;
    }

    lv_obj_clean(l_tFileManagerUi.pList);
    l_tFileManagerUi.usEntryCount = 0U;
    usSkippedCount = 0U;
    if (!FileManager_MountTf())
    {
        FileManager_SetStatus("TF unavailable - insert card and refresh");
        return;
    }

    pDirectory = opendir(l_tFileManagerUi.aCurrentPath);
    if (RT_NULL == pDirectory)
    {
        FileManager_SetStatus("Cannot open directory");
        return;
    }

    while (RT_NULL != (pDirectoryEntry = readdir(pDirectory)))
    {
        if ((0 == rt_strcmp(pDirectoryEntry->d_name, ".")) ||
                (0 == rt_strcmp(pDirectoryEntry->d_name, "..")))
        {
            continue;
        }

        if (FILE_MANAGER_ENTRY_MAX <= l_tFileManagerUi.usEntryCount)
        {
            usSkippedCount++;
            continue;
        }

        if (!FileManager_BuildPath(aFullPath,
                                   sizeof(aFullPath),
                                   l_tFileManagerUi.aCurrentPath,
                                   pDirectoryEntry->d_name) ||
                (0 != stat(aFullPath, &tFileStat)))
        {
            usSkippedCount++;
            continue;
        }

        pEntry = &l_tFileManagerUi.aEntries[l_tFileManagerUi.usEntryCount];
        rt_memset(pEntry, 0, sizeof(*pEntry));
        rt_snprintf(pEntry->aName, sizeof(pEntry->aName), "%s", pDirectoryEntry->d_name);
        if (S_ISDIR(tFileStat.st_mode))
        {
            pEntry->eType = FILE_MANAGER_ENTRY_DIRECTORY;
        }
        else if (FileManager_IsImage(pEntry->aName))
        {
            pEntry->eType = FILE_MANAGER_ENTRY_IMAGE;
        }
        else
        {
            pEntry->eType = FILE_MANAGER_ENTRY_FILE;
        }

        FileManager_AddRow(pEntry);
        l_tFileManagerUi.usEntryCount++;
    }

    (void)closedir(pDirectory);
    lv_label_set_text(l_tFileManagerUi.pPathLabel, l_tFileManagerUi.aCurrentPath);
    if (0U < usSkippedCount)
    {
        rt_snprintf(aStatus,
                    sizeof(aStatus),
                    "%u shown, %u skipped",
                    l_tFileManagerUi.usEntryCount,
                    usSkippedCount);
    }
    else if (0U == l_tFileManagerUi.usEntryCount)
    {
        rt_snprintf(aStatus, sizeof(aStatus), "Directory is empty");
    }
    else
    {
        rt_snprintf(aStatus,
                    sizeof(aStatus),
                    "%u items",
                    l_tFileManagerUi.usEntryCount);
    }
    FileManager_SetStatus(aStatus);

    return;
}

/***************************
 * FileManager_Back: navigate upward without escaping /sdcard.
 * Parameters: pEvent is the click event.
 * Return: none.
 ***************************/
static void FileManager_Back(lv_event_t *pEvent)
{
    char *pSeparator;

    if ((RT_NULL == pEvent) || (LV_EVENT_CLICKED != lv_event_get_code(pEvent)))
    {
        return;
    }
    if (0 == rt_strcmp(l_tFileManagerUi.aCurrentPath, FILE_MANAGER_ROOT_PATH))
    {
        FileManager_SetStatus("Already at TF card root");
        return;
    }

    pSeparator = strrchr(l_tFileManagerUi.aCurrentPath, '/');
    if ((RT_NULL == pSeparator) ||
            ((size_t)(pSeparator - l_tFileManagerUi.aCurrentPath) <
             rt_strlen(FILE_MANAGER_ROOT_PATH)))
    {
        rt_snprintf(l_tFileManagerUi.aCurrentPath,
                    sizeof(l_tFileManagerUi.aCurrentPath),
                    "%s",
                    FILE_MANAGER_ROOT_PATH);
    }
    else
    {
        *pSeparator = '\0';
    }
    FileManager_Refresh();

    return;
}

/***************************
 * FileManager_RefreshEvent: refresh button callback.
 * Parameters: pEvent is the click event.
 * Return: none.
 ***************************/
static void FileManager_RefreshEvent(lv_event_t *pEvent)
{
    if ((RT_NULL != pEvent) && (LV_EVENT_CLICKED == lv_event_get_code(pEvent)))
    {
        FileManager_Refresh();
    }

    return;
}

/***************************
 * FileManager_CreateHeaderButton: create an icon button in the header.
 * Parameters: parent, symbol, horizontal coordinate, and callback.
 * Return: the created button or NULL for invalid input.
 ***************************/
static lv_obj_t *FileManager_CreateHeaderButton(lv_obj_t *pParent,
                                                 const char *pSymbol,
                                                 lv_coord_t lX,
                                                 lv_event_cb_t pCallback)
{
    lv_obj_t *pButton;
    lv_obj_t *pLabel;

    if ((RT_NULL == pParent) || (RT_NULL == pSymbol) || (RT_NULL == pCallback))
    {
        return RT_NULL;
    }

    pButton = lv_btn_create(pParent);
    lv_obj_set_size(pButton, 52, 46);
    lv_obj_set_pos(pButton, lX, 8);
    lv_obj_set_style_radius(pButton, 8, 0);
    lv_obj_add_event_cb(pButton, pCallback, LV_EVENT_CLICKED, RT_NULL);
    pLabel = lv_label_create(pButton);
    lv_label_set_text(pLabel, pSymbol);
    lv_obj_center(pLabel);

    return pButton;
}

/***************************
 * FileManager_OnStart: create the UI and enumerate the TF root.
 * Parameters: none.
 * Return: none.
 ***************************/
static void FileManager_OnStart(void)
{
    lv_obj_t *pTitle;

    rt_memset(&l_tFileManagerUi, 0, sizeof(l_tFileManagerUi));
    rt_snprintf(l_tFileManagerUi.aCurrentPath,
                sizeof(l_tFileManagerUi.aCurrentPath),
                "%s",
                FILE_MANAGER_ROOT_PATH);

    l_tFileManagerUi.pRoot = lv_obj_create(lv_scr_act());
    lv_obj_set_size(l_tFileManagerUi.pRoot, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_set_pos(l_tFileManagerUi.pRoot, 0, 0);
    lv_obj_set_style_bg_color(l_tFileManagerUi.pRoot, lv_color_hex(0x080d14), 0);
    lv_obj_set_style_bg_opa(l_tFileManagerUi.pRoot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(l_tFileManagerUi.pRoot, 0, 0);
    lv_obj_set_style_pad_all(l_tFileManagerUi.pRoot, 0, 0);
    lv_obj_clear_flag(l_tFileManagerUi.pRoot, LV_OBJ_FLAG_SCROLLABLE);

    (void)FileManager_CreateHeaderButton(l_tFileManagerUi.pRoot,
                                         LV_SYMBOL_LEFT,
                                         8,
                                         FileManager_Back);
    (void)FileManager_CreateHeaderButton(l_tFileManagerUi.pRoot,
                                         LV_SYMBOL_REFRESH,
                                         LV_HOR_RES_MAX - 60,
                                         FileManager_RefreshEvent);
    pTitle = lv_label_create(l_tFileManagerUi.pRoot);
    lv_label_set_text(pTitle, "TF Files");
    lv_obj_set_style_text_font(pTitle, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(pTitle, lv_color_white(), 0);
    lv_obj_align(pTitle, LV_ALIGN_TOP_MID, 0, 18);

    l_tFileManagerUi.pPathLabel = lv_label_create(l_tFileManagerUi.pRoot);
    lv_obj_set_size(l_tFileManagerUi.pPathLabel, LV_HOR_RES_MAX - 24, 26);
    lv_obj_set_pos(l_tFileManagerUi.pPathLabel, 12, FILE_MANAGER_HEADER_HEIGHT);
    lv_label_set_long_mode(l_tFileManagerUi.pPathLabel, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(l_tFileManagerUi.pPathLabel, lv_color_hex(0x85bfff), 0);
    lv_obj_set_style_text_font(l_tFileManagerUi.pPathLabel, &lv_font_montserrat_20, 0);

    l_tFileManagerUi.pStatusLabel = lv_label_create(l_tFileManagerUi.pRoot);
    lv_obj_set_size(l_tFileManagerUi.pStatusLabel,
                    LV_HOR_RES_MAX - 24,
                    FILE_MANAGER_STATUS_HEIGHT);
    lv_obj_set_pos(l_tFileManagerUi.pStatusLabel, 12, FILE_MANAGER_HEADER_HEIGHT + 28);
    lv_label_set_long_mode(l_tFileManagerUi.pStatusLabel, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(l_tFileManagerUi.pStatusLabel, lv_color_hex(0x99a7b8), 0);
    lv_obj_set_style_text_font(l_tFileManagerUi.pStatusLabel, &lv_font_montserrat_20, 0);

    l_tFileManagerUi.pList = lv_obj_create(l_tFileManagerUi.pRoot);
    lv_obj_set_size(l_tFileManagerUi.pList,
                    LV_HOR_RES_MAX - 16,
                    LV_VER_RES_MAX - FILE_MANAGER_HEADER_HEIGHT -
                    FILE_MANAGER_STATUS_HEIGHT - 38);
    lv_obj_set_pos(l_tFileManagerUi.pList,
                   8,
                   FILE_MANAGER_HEADER_HEIGHT + FILE_MANAGER_STATUS_HEIGHT + 32);
    lv_obj_set_style_bg_opa(l_tFileManagerUi.pList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(l_tFileManagerUi.pList, 0, 0);
    lv_obj_set_style_pad_all(l_tFileManagerUi.pList, 4, 0);
    lv_obj_set_style_pad_row(l_tFileManagerUi.pList, 5, 0);
    lv_obj_set_flex_flow(l_tFileManagerUi.pList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(l_tFileManagerUi.pList,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_label_set_text(l_tFileManagerUi.pPathLabel, l_tFileManagerUi.aCurrentPath);
    FileManager_SetStatus("Opening TF card...");
    FileManager_Refresh();

    return;
}

/***************************
 * FileManager_OnStop: delete UI while keeping the TF volume mounted.
 * Parameters: none.
 * Return: none.
 ***************************/
static void FileManager_OnStop(void)
{
    if (RT_NULL != l_tFileManagerUi.pRoot)
    {
        lv_obj_del(l_tFileManagerUi.pRoot);
    }
    rt_memset(&l_tFileManagerUi, 0, sizeof(l_tFileManagerUi));

    return;
}

/***************************
 * FileManager_MsgHandler: lifecycle message handler.
 * Parameters: application message and optional data.
 * Return: none.
 ***************************/
static void FileManager_MsgHandler(gui_app_msg_type_t eMessage, void *pParameter)
{
    (void)pParameter;

    if (GUI_APP_MSG_ONSTART == eMessage)
    {
        FileManager_OnStart();
    }
    else if (GUI_APP_MSG_ONSTOP == eMessage)
    {
        FileManager_OnStop();
    }

    return;
}

/***************************
 * FileManager_AppMain: register the lifecycle handler.
 * Parameters: tIntent is currently unused.
 * Return: zero on success.
 ***************************/
static int FileManager_AppMain(intent_t tIntent)
{
    (void)tIntent;

    gui_app_regist_msg_handler(APP_ID, FileManager_MsgHandler);
    return 0;
}

LV_IMG_DECLARE(img_photos);
BUILTIN_APP_EXPORT(LV_EXT_STR_ID(file_manager),
                   LV_EXT_IMG_GET(img_photos),
                   APP_ID,
                   FileManager_AppMain);
