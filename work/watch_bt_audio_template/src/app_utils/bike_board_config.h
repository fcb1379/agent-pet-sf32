#ifndef BIKE_BOARD_CONFIG_H
#define BIKE_BOARD_CONFIG_H

#include <board.h>
#include <bf0_hal.h>

/* GNSS wiring is selected automatically by the board passed to scons.
 *
 * SF32LB52-DevKit-LCD:
 *   UART2 TX = PA27, RX = PA20 (the UART pins on the 40-pin header).
 *
 * Lichuang Huangshan Pi:
 *   UART3 TX = PA35, RX = PA36 (the existing GPS adapter wiring).
 */
#if defined(BIKE_GNSS_UART2) && defined(BIKE_GNSS_UART3)
#error "Only one bike GNSS UART mapping may be selected"
#elif defined(BIKE_GNSS_UART2)
#define BIKE_GNSS_UART_DEVICE_NAME "uart2"
#define BIKE_GNSS_TX_PAD PAD_PA27
#define BIKE_GNSS_TX_FUNCTION USART2_TXD
#define BIKE_GNSS_RX_PAD PAD_PA20
#define BIKE_GNSS_RX_FUNCTION USART2_RXD
#define BIKE_BOARD_HAS_ONBOARD_PEDOMETER (0)
#define BIKE_BOARD_HAS_ONBOARD_COMPASS (0)
#elif defined(BIKE_GNSS_UART3)
#define BIKE_GNSS_UART_DEVICE_NAME "uart3"
#define BIKE_GNSS_TX_PAD PAD_PA35
#define BIKE_GNSS_TX_FUNCTION USART3_TXD
#define BIKE_GNSS_RX_PAD PAD_PA36
#define BIKE_GNSS_RX_FUNCTION USART3_RXD
#define BIKE_BOARD_HAS_ONBOARD_PEDOMETER (1)
#define BIKE_BOARD_HAS_ONBOARD_COMPASS (1)
#else
#error "No GNSS UART mapping for this board; add it to Kconfig.proj and bike_board_config.h"
#endif

#endif /* BIKE_BOARD_CONFIG_H */
