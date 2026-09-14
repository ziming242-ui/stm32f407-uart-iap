#ifndef IAP_FLASH_IF_H
#define IAP_FLASH_IF_H

#include "iap_config.h"

#include <stddef.h>
#include <stdint.h>

uint32_t iap_slot_base(IapSlot slot);
int iap_flash_range_valid(uint32_t address, uint32_t length);
IapError iap_flash_erase_region(uint32_t address, uint32_t length);
IapError iap_flash_program(uint32_t address,
                           const uint8_t *data,
                           uint32_t length);
int iap_flash_equal(uint32_t address,
                    const uint8_t *data,
                    uint32_t length);
uint32_t iap_flash_crc32(uint32_t address, uint32_t length);

#endif
