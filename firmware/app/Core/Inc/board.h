#ifndef APP_BOARD_H
#define APP_BOARD_H

#include <stddef.h>
#include <stdint.h>

void board_init(void);
int board_uart_write(const uint8_t *data, size_t length);
void board_log(const char *text);

#endif
