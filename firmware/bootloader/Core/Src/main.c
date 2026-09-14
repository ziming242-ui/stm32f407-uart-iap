#include "main.h"

#include "board.h"
#include "boot_flow.h"
#include "boot_service.h"
#include "iap_config.h"
#include "iap_protocol.h"

#include <string.h>

static IapBootService service;
static uint8_t receive_buffer[IAP_FRAME_WIRE_MAX];
static uint8_t response_buffer[IAP_FRAME_WIRE_MAX];

static void discard_first_byte(size_t *length)
{
    if (*length > 1u) {
        memmove(receive_buffer, &receive_buffer[1], *length - 1u);
    }
    if (*length != 0u) {
        --(*length);
    }
}

static int header_is_plausible(size_t length, uint16_t *declared_length)
{
    uint16_t command;

    if (length < 4u) {
        return 0;
    }
    command = iap_get_u16_be(&receive_buffer[0]);
    *declared_length = iap_get_u16_be(&receive_buffer[2]);
    return iap_is_request_command(command) &&
           *declared_length >= IAP_FRAME_OVERHEAD &&
           *declared_length <= IAP_FRAME_WIRE_MAX;
}

int main(void)
{
    size_t receive_length = 0u;
    uint32_t last_byte_ms = 0u;
    uint32_t boot_deadline;
    int automatic_boot_tried = 0;

    board_init();
    if (iap_boot_service_init(&service) != IAP_ERR_OK) {
        Error_Handler();
    }
    boot_deadline = board_millis() + IAP_BOOT_WAIT_MS;

    for (;;) {
        uint8_t byte;
        uint32_t now = board_millis();

        if (receive_length != 0u &&
            (uint32_t)(now - last_byte_ms) > IAP_FRAME_TIMEOUT_MS) {
            receive_length = 0u;
        }

        if (board_uart_read_byte(&byte, 1u)) {
            last_byte_ms = board_millis();
            if (receive_length == sizeof(receive_buffer)) {
                receive_length = 0u;
            }
            receive_buffer[receive_length++] = byte;

            for (;;) {
                uint16_t frame_length;
                size_t response_length;

                if (receive_length < 4u) {
                    break;
                }
                if (!header_is_plausible(receive_length, &frame_length)) {
                    discard_first_byte(&receive_length);
                    continue;
                }
                if (receive_length < frame_length) {
                    break;
                }

                response_length = iap_boot_process_wire(&service,
                                                        receive_buffer,
                                                        frame_length,
                                                        response_buffer,
                                                        sizeof(response_buffer));
                if (response_length != 0u) {
                    (void)board_uart_write(response_buffer, response_length);
                }
                if (receive_length > frame_length) {
                    memmove(receive_buffer,
                            &receive_buffer[frame_length],
                            receive_length - frame_length);
                }
                receive_length -= frame_length;

                if (service.boot_requested != 0u) {
                    service.boot_requested = 0u;
                    (void)iap_boot_try_start_application();
                }
            }
        }

        now = board_millis();
        if (!automatic_boot_tried && service.permanent_hold == 0u &&
            service.download.active == 0u && receive_length == 0u &&
            (int32_t)(now - boot_deadline) >= 0) {
            automatic_boot_tried = 1;
            (void)iap_boot_try_start_application();
        }
    }
}
