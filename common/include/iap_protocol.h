#ifndef IAP_PROTOCOL_H
#define IAP_PROTOCOL_H

#include "iap_config.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint16_t command;
    uint16_t data_length;
    uint8_t data[IAP_FRAME_DATA_MAX];
    uint32_t reserved;
} IapFrame;

void iap_put_u16_be(uint8_t *dst, uint16_t value);
uint16_t iap_get_u16_be(const uint8_t *src);
void iap_put_u32_be(uint8_t *dst, uint32_t value);
uint32_t iap_get_u32_be(const uint8_t *src);

size_t iap_frame_encode(const IapFrame *frame,
                        uint8_t *wire,
                        size_t capacity);
IapError iap_frame_decode(const uint8_t *wire,
                          size_t length,
                          IapFrame *frame);
int iap_is_request_command(uint16_t command);

#endif
