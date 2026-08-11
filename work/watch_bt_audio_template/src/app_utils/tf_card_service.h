#ifndef TF_CARD_SERVICE_H
#define TF_CARD_SERVICE_H

#include <stdbool.h>
#include <rtthread.h>

#define TF_CARD_DEVICE_NAME    "sd0"
#define TF_CARD_ROOT_PATH      "/sdcard"

bool TF_CARD_IsInserted(void);
bool TF_CARD_IsMounted(void);
rt_err_t TF_CARD_EnsureMounted(void);

#endif /* TF_CARD_SERVICE_H */
