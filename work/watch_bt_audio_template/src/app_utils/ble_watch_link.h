#ifndef BLE_WATCH_LINK_H
#define BLE_WATCH_LINK_H

#include <stdbool.h>
#include <stdint.h>

void ble_link_notify_event(const char *text);
bool ble_link_request_find_wakeup(void);
bool ble_link_queue_find_end(uint16_t usSessionId, const char *pReason);

#endif /* BLE_WATCH_LINK_H */
