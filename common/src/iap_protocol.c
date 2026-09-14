#include "iap_protocol.h"

#include "iap_crc.h"

#include <string.h>

void iap_put_u16_be(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value >> 8);
    dst[1] = (uint8_t)value;
}

uint16_t iap_get_u16_be(const uint8_t *src)
{
    return (uint16_t)(((uint16_t)src[0] << 8) | src[1]);
}

void iap_put_u32_be(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value >> 24);
    dst[1] = (uint8_t)(value >> 16);
    dst[2] = (uint8_t)(value >> 8);
    dst[3] = (uint8_t)value;
}

uint32_t iap_get_u32_be(const uint8_t *src)
{
    return ((uint32_t)src[0] << 24) |
           ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) |
           (uint32_t)src[3];
}

size_t iap_frame_encode(const IapFrame *frame,
                        uint8_t *wire,
                        size_t capacity)
{
    size_t total;
    size_t reserved_offset;

    if (frame == NULL || wire == NULL ||
        frame->data_length > IAP_FRAME_DATA_MAX) {
        return 0u;
    }

    total = IAP_FRAME_OVERHEAD + frame->data_length;
    if (capacity < total) {
        return 0u;
    }

    iap_put_u16_be(&wire[0], frame->command);
    iap_put_u16_be(&wire[2], (uint16_t)total);
    if (frame->data_length != 0u) {
        memcpy(&wire[4], frame->data, frame->data_length);
    }
    reserved_offset = 4u + frame->data_length;
    iap_put_u32_be(&wire[reserved_offset], frame->reserved);
    iap_put_u16_be(&wire[total - 2u],
                   iap_crc16_ccitt_false(wire, total - 2u));
    return total;
}

IapError iap_frame_decode(const uint8_t *wire,
                          size_t length,
                          IapFrame *frame)
{
    uint16_t declared_length;
    uint16_t received_crc;
    size_t data_length;
    size_t reserved_offset;

    if (wire == NULL || frame == NULL || length < IAP_FRAME_OVERHEAD ||
        length > IAP_FRAME_WIRE_MAX) {
        return IAP_ERR_FRAME_LENGTH;
    }

    declared_length = iap_get_u16_be(&wire[2]);
    if ((size_t)declared_length != length) {
        return IAP_ERR_FRAME_LENGTH;
    }

    received_crc = iap_get_u16_be(&wire[length - 2u]);
    if (received_crc != iap_crc16_ccitt_false(wire, length - 2u)) {
        return IAP_ERR_FRAME_CRC;
    }

    data_length = length - IAP_FRAME_OVERHEAD;
    reserved_offset = 4u + data_length;
    if (iap_get_u32_be(&wire[reserved_offset]) != 0u) {
        return IAP_ERR_RESERVED;
    }

    memset(frame, 0, sizeof(*frame));
    frame->command = iap_get_u16_be(&wire[0]);
    frame->data_length = (uint16_t)data_length;
    if (data_length != 0u) {
        memcpy(frame->data, &wire[4], data_length);
    }
    return IAP_ERR_OK;
}

int iap_is_request_command(uint16_t command)
{
    switch (command) {
    case IAP_CMD_HELLO:
    case IAP_CMD_HOLD:
    case IAP_CMD_START:
    case IAP_CMD_DATA:
    case IAP_CMD_END:
    case IAP_CMD_ABORT:
    case IAP_CMD_STATUS:
    case IAP_CMD_BOOT:
        return 1;
    default:
        return 0;
    }
}
