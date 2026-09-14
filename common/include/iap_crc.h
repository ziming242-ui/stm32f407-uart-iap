#ifndef IAP_CRC_H
#define IAP_CRC_H

#include <stddef.h>
#include <stdint.h>

uint16_t iap_crc16_ccitt_false(const uint8_t *data, size_t length);

uint32_t iap_crc32_ieee_begin(void);
uint32_t iap_crc32_ieee_update(uint32_t state,
                               const uint8_t *data,
                               size_t length);
uint32_t iap_crc32_ieee_end(uint32_t state);
uint32_t iap_crc32_ieee(const uint8_t *data, size_t length);

#endif
