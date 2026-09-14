#include "app_confirm.h"

#include "iap_config.h"
#include "iap_crc.h"
#include "iap_storage.h"
#include "stm32f4xx_hal.h"

#include <stddef.h>
#include <stdint.h>

#define META_RECORD_COUNT (IAP_PARAM_SIZE / IAP_META_RECORD_SIZE)

static int bytes_are_erased(const volatile uint8_t *bytes, uint32_t length)
{
    uint32_t i;
    for (i = 0u; i < length; ++i) {
        if (bytes[i] != 0xFFu) {
            return 0;
        }
    }
    return 1;
}

static void copy_from_flash(IapMetaRecord *destination, uint32_t address)
{
    uint32_t i;
    uint8_t *out = (uint8_t *)destination;
    const volatile uint8_t *in = (const volatile uint8_t *)(uintptr_t)address;
    for (i = 0u; i < sizeof(*destination); ++i) {
        out[i] = in[i];
    }
}

static int record_is_valid(const IapMetaRecord *record)
{
    uint32_t crc;
    if (record->magic != IAP_META_MAGIC ||
        record->schema != IAP_META_SCHEMA ||
        record->record_size != IAP_META_RECORD_SIZE ||
        record->commit_magic != IAP_META_COMMIT_MAGIC) {
        return 0;
    }
    crc = iap_crc32_ieee((const uint8_t *)record,
                         offsetof(IapMetaRecord, record_crc32));
    return crc == record->record_crc32;
}

static int load_latest(IapMetaRecord *latest)
{
    uint32_t index;
    int found = 0;
    for (index = 0u; index < META_RECORD_COUNT; ++index) {
        IapMetaRecord candidate;
        const uint32_t address = IAP_PARAM_BASE + index * IAP_META_RECORD_SIZE;
        copy_from_flash(&candidate, address);
        if (record_is_valid(&candidate) &&
            (!found || candidate.record_generation > latest->record_generation)) {
            *latest = candidate;
            found = 1;
        }
    }
    return found;
}

static uint32_t find_empty_record(void)
{
    uint32_t index;
    for (index = 0u; index < META_RECORD_COUNT; ++index) {
        const uint32_t address = IAP_PARAM_BASE + index * IAP_META_RECORD_SIZE;
        if (bytes_are_erased((const volatile uint8_t *)(uintptr_t)address,
                             IAP_META_RECORD_SIZE)) {
            return address;
        }
    }
    return 0u;
}

static int program_record(uint32_t address, IapMetaRecord *record)
{
    uint32_t offset;
    const uint32_t crc_offset = offsetof(IapMetaRecord, record_crc32);
    const uint32_t commit_offset = offsetof(IapMetaRecord, commit_magic);
    const uint32_t *words = (const uint32_t *)(const void *)record;

    record->record_crc32 = iap_crc32_ieee((const uint8_t *)record,
                                          crc_offset);
    record->commit_magic = IAP_META_COMMIT_MAGIC;
    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR |
                           FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR |
                           FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    /* Commit marker last: a reset before this word leaves an invalid record. */
    for (offset = 0u; offset < commit_offset; offset += 4u) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                              address + offset,
                              words[offset / 4u]) != HAL_OK) {
            HAL_FLASH_Lock();
            return 0;
        }
    }
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                          address + commit_offset,
                          record->commit_magic) != HAL_OK) {
        HAL_FLASH_Lock();
        return 0;
    }
    HAL_FLASH_Lock();

    for (offset = 0u; offset < sizeof(*record); ++offset) {
        if (*(const volatile uint8_t *)(uintptr_t)(address + offset) !=
            ((const uint8_t *)record)[offset]) {
            return 0;
        }
    }
    return 1;
}

AppConfirmResult app_confirm_running_image(void)
{
    IapMetaRecord latest;
    uint32_t destination;
    uint32_t slot;

    if (!load_latest(&latest) || latest.run.valid != IAP_RUN_VALID_MAGIC ||
        latest.run.source_slot > IAP_SLOT_B) {
        return APP_CONFIRM_NOT_PENDING;
    }

    slot = latest.run.source_slot;
    if (latest.slot[slot].state != IAP_IMAGE_PENDING ||
        latest.slot[slot].version != latest.run.version ||
        latest.slot[slot].size != latest.run.size ||
        latest.slot[slot].crc32 != latest.run.crc32) {
        return APP_CONFIRM_NOT_PENDING;
    }

    destination = find_empty_record();
    if (destination == 0u) {
        return APP_CONFIRM_META_FULL;
    }

    latest.record_generation += 1u;
    latest.slot[slot].state = IAP_IMAGE_CONFIRMED;
    latest.slot[slot].boot_attempts = 0u;
    latest.run.valid = IAP_RUN_VALID_MAGIC;
    if (!program_record(destination, &latest)) {
        return APP_CONFIRM_FLASH_ERROR;
    }
    return APP_CONFIRM_OK;
}
