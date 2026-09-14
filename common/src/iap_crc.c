#include "iap_crc.h"

uint16_t iap_crc16_ccitt_false(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFu;
    size_t i;

    for (i = 0u; i < length; ++i) {
        uint8_t bit;
        crc ^= (uint16_t)data[i] << 8;
        for (bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 0x8000u) != 0u
                      ? (uint16_t)((crc << 1) ^ 0x1021u)
                      : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

uint32_t iap_crc32_ieee_begin(void)
{
    return 0xFFFFFFFFu;
}

uint32_t iap_crc32_ieee_update(uint32_t state,
                               const uint8_t *data,
                               size_t length)
{
    size_t i;

    for (i = 0u; i < length; ++i) {
        uint8_t bit;
        state ^= data[i];
        for (bit = 0u; bit < 8u; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(state & 1u);
            state = (state >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return state;
}

uint32_t iap_crc32_ieee_end(uint32_t state)
{
    return state ^ 0xFFFFFFFFu;
}

uint32_t iap_crc32_ieee(const uint8_t *data, size_t length)
{
    return iap_crc32_ieee_end(
        iap_crc32_ieee_update(iap_crc32_ieee_begin(), data, length));
}
