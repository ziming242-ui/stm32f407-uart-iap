#include "flash_if.h"

#include "iap_crc.h"
#include "stm32f4xx_hal.h"

#include <string.h>

typedef struct {
    uint32_t start;
    uint32_t end;
    uint32_t sector;
} FlashSector;

static const FlashSector flash_sectors[] = {
    {0x08000000u, 0x08004000u, FLASH_SECTOR_0},
    {0x08004000u, 0x08008000u, FLASH_SECTOR_1},
    {0x08008000u, 0x0800C000u, FLASH_SECTOR_2},
    {0x0800C000u, 0x08010000u, FLASH_SECTOR_3},
    {0x08010000u, 0x08020000u, FLASH_SECTOR_4},
    {0x08020000u, 0x08040000u, FLASH_SECTOR_5},
    {0x08040000u, 0x08060000u, FLASH_SECTOR_6},
    {0x08060000u, 0x08080000u, FLASH_SECTOR_7},
    {0x08080000u, 0x080A0000u, FLASH_SECTOR_8},
    {0x080A0000u, 0x080C0000u, FLASH_SECTOR_9},
    {0x080C0000u, 0x080E0000u, FLASH_SECTOR_10},
    {0x080E0000u, 0x08100000u, FLASH_SECTOR_11}
};

static int flash_sector_index(uint32_t address)
{
    uint32_t index;

    for (index = 0u;
         index < (uint32_t)(sizeof(flash_sectors) / sizeof(flash_sectors[0]));
         ++index) {
        if (address >= flash_sectors[index].start &&
            address < flash_sectors[index].end) {
            return (int)index;
        }
    }
    return -1;
}

static void flash_reset_caches(void)
{
    __HAL_FLASH_INSTRUCTION_CACHE_DISABLE();
    __HAL_FLASH_DATA_CACHE_DISABLE();
    __HAL_FLASH_INSTRUCTION_CACHE_RESET();
    __HAL_FLASH_DATA_CACHE_RESET();
    __HAL_FLASH_INSTRUCTION_CACHE_ENABLE();
    __HAL_FLASH_DATA_CACHE_ENABLE();
}

static int byte_can_be_programmed(uint8_t current, uint8_t desired)
{
    return (uint8_t)(current & desired) == desired;
}

uint32_t iap_slot_base(IapSlot slot)
{
    if (slot == IAP_SLOT_A) {
        return IAP_SLOT_A_BASE;
    }
    if (slot == IAP_SLOT_B) {
        return IAP_SLOT_B_BASE;
    }
    return 0u;
}

int iap_flash_range_valid(uint32_t address, uint32_t length)
{
    uint32_t end;

    if (length == 0u || address < IAP_PARAM_BASE ||
        address >= IAP_FLASH_END) {
        return 0;
    }
    end = address + length;
    if (end < address || end > IAP_FLASH_END) {
        return 0;
    }
    return 1;
}

IapError iap_flash_erase_region(uint32_t address, uint32_t length)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t sector_error = 0xFFFFFFFFu;
    uint32_t end;
    int first;
    int last;
    HAL_StatusTypeDef status;

    if (!iap_flash_range_valid(address, length)) {
        return IAP_ERR_FLASH;
    }
    end = address + length;
    first = flash_sector_index(address);
    last = flash_sector_index(end - 1u);
    if (first < 0 || last < first || flash_sectors[first].start != address ||
        flash_sectors[last].end != end) {
        return IAP_ERR_FLASH;
    }

    memset(&erase, 0, sizeof(erase));
    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase.Sector = flash_sectors[first].sector;
    erase.NbSectors = (uint32_t)(last - first + 1);

    if (HAL_FLASH_Unlock() != HAL_OK) {
        return IAP_ERR_FLASH;
    }
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR |
                           FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR |
                           FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
    status = HAL_FLASHEx_Erase(&erase, &sector_error);
    (void)HAL_FLASH_Lock();
    flash_reset_caches();
    return status == HAL_OK ? IAP_ERR_OK : IAP_ERR_FLASH;
}

IapError iap_flash_program(uint32_t address,
                           const uint8_t *data,
                           uint32_t length)
{
    uint32_t offset = 0u;
    HAL_StatusTypeDef status = HAL_OK;

    if (data == NULL || !iap_flash_range_valid(address, length)) {
        return IAP_ERR_FLASH;
    }
    if (HAL_FLASH_Unlock() != HAL_OK) {
        return IAP_ERR_FLASH;
    }
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR |
                           FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR |
                           FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    while (offset < length && ((address + offset) & 3u) != 0u) {
        volatile const uint8_t *current =
            (volatile const uint8_t *)(uintptr_t)(address + offset);
        if (!byte_can_be_programmed(*current, data[offset])) {
            status = HAL_ERROR;
            break;
        }
        if (*current != data[offset]) {
            status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE,
                                       address + offset,
                                       data[offset]);
            if (status != HAL_OK) {
                break;
            }
        }
        ++offset;
    }

    while (status == HAL_OK && length - offset >= 4u) {
        volatile const uint32_t *current =
            (volatile const uint32_t *)(uintptr_t)(address + offset);
        uint32_t desired;

        memcpy(&desired, &data[offset], sizeof(desired));
        if ((*current & desired) != desired) {
            status = HAL_ERROR;
            break;
        }
        if (*current != desired) {
            status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                                       address + offset,
                                       desired);
            if (status != HAL_OK) {
                break;
            }
        }
        offset += 4u;
    }

    while (status == HAL_OK && offset < length) {
        volatile const uint8_t *current =
            (volatile const uint8_t *)(uintptr_t)(address + offset);
        if (!byte_can_be_programmed(*current, data[offset])) {
            status = HAL_ERROR;
            break;
        }
        if (*current != data[offset]) {
            status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE,
                                       address + offset,
                                       data[offset]);
            if (status != HAL_OK) {
                break;
            }
        }
        ++offset;
    }

    (void)HAL_FLASH_Lock();
    flash_reset_caches();
    if (status != HAL_OK || !iap_flash_equal(address, data, length)) {
        return IAP_ERR_FLASH;
    }
    return IAP_ERR_OK;
}

int iap_flash_equal(uint32_t address,
                    const uint8_t *data,
                    uint32_t length)
{
    volatile const uint8_t *flash;
    uint32_t index;

    if (data == NULL || !iap_flash_range_valid(address, length)) {
        return 0;
    }
    flash = (volatile const uint8_t *)(uintptr_t)address;
    for (index = 0u; index < length; ++index) {
        if (flash[index] != data[index]) {
            return 0;
        }
    }
    return 1;
}

uint32_t iap_flash_crc32(uint32_t address, uint32_t length)
{
    if (!iap_flash_range_valid(address, length)) {
        return 0u;
    }
    return iap_crc32_ieee((const uint8_t *)(uintptr_t)address, length);
}
