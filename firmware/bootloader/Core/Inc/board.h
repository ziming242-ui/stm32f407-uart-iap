#ifndef BOOT_BOARD_H
#define BOOT_BOARD_H

#include <stddef.h>
#include <stdint.h>

void board_init(void);
uint32_t board_millis(void);
int board_uart_read_byte(uint8_t *byte, uint32_t timeout_ms);
int board_uart_write(const uint8_t *data, size_t length);
void board_prepare_for_app(void);
void board_system_reset(void);

#endif
