#include "metadata.h"

#include "flash_if.h"
#include "iap_crc.h"

#include <stddef.h>
#include <string.h>

#define META_RECORD_COUNT (IAP_PARAM_SIZE / IAP_META_RECORD_SIZE)

static uint32_t record_address(uint32_t index)
{
    return IAP_PARAM_BASE + index * IAP_META_RECORD_SIZE;
}

static int record_is_erased(uint32_t address)
{
    volatile const uint32_t *words =
        (volatile const uint32_t *)(uintptr_t)address;
    uint32_t index;

    for (index = 0u; index < IAP_META_RECORD_SIZE / 4u; ++index) {
        if (words[index] != 0xFFFFFFFFu) {
            return 0;
        }
    }
    return 1;
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

void iap_metadata_default(IapMetaRecord *record)
{
    if (record == NULL) {
        return;
    }
    memset(record, 0, sizeof(*record));
    record->magic = IAP_META_MAGIC;
    record->schema = IAP_META_SCHEMA;
    record->record_size = IAP_META_RECORD_SIZE;
    record->slot[0].state = IAP_IMAGE_EMPTY;
    record->slot[1].state = IAP_IMAGE_EMPTY;
    record->run.source_slot = IAP_SLOT_NONE;
}

IapError iap_metadata_load(IapMetaRecord *record)
{
    uint32_t index;
    int found = 0;

    if (record == NULL) {
        return IAP_ERR_FLASH;
    }
    iap_metadata_default(record);

    for (index = 0u; index < META_RECORD_COUNT; ++index) {
        uint32_t address = record_address(index);
        const IapMetaRecord *candidate =
            (const IapMetaRecord *)(uintptr_t)address;

        if (record_is_erased(address)) {
            break;
        }
        if (record_is_valid(candidate) &&
            (!found || candidate->record_generation >=
                           record->record_generation)) {
            memcpy(record, candidate, sizeof(*record));
            found = 1;
        }
    }
    return IAP_ERR_OK;
}

IapError iap_metadata_store(IapMetaRecord *record)
{
    IapMetaRecord candidate;
    uint32_t index;
    uint32_t address = 0u;
    IapError error;

    if (record == NULL) {
        return IAP_ERR_FLASH;
    }
    for (index = 0u; index < META_RECORD_COUNT; ++index) {
        if (record_is_erased(record_address(index))) {
            address = record_address(index);
            break;
        }
    }
    if (address == 0u) {
        return IAP_ERR_META_FULL;
    }

    candidate = *record;
    candidate.magic = IAP_META_MAGIC;
    candidate.schema = IAP_META_SCHEMA;
    candidate.record_size = IAP_META_RECORD_SIZE;
    candidate.record_generation = record->record_generation + 1u;
    candidate.record_crc32 = iap_crc32_ieee(
        (const uint8_t *)&candidate,
        offsetof(IapMetaRecord, record_crc32));
    candidate.commit_magic = IAP_META_COMMIT_MAGIC;

    error = iap_flash_program(address,
                              (const uint8_t *)&candidate,
                              (uint32_t)offsetof(IapMetaRecord,
                                                 commit_magic));
    if (error != IAP_ERR_OK) {
        return error;
    }
    error = iap_flash_program(address +
                                  (uint32_t)offsetof(IapMetaRecord,
                                                     commit_magic),
                              (const uint8_t *)&candidate.commit_magic,
                              sizeof(candidate.commit_magic));
    if (error != IAP_ERR_OK) {
        return error;
    }
    memcpy(record, &candidate, sizeof(*record));
    return IAP_ERR_OK;
}

IapError iap_metadata_confirm_running(void)
{
    IapMetaRecord record;
    uint32_t slot;
    IapError error = iap_metadata_load(&record);

    if (error != IAP_ERR_OK) {
        return error;
    }
    if (record.run.valid != IAP_RUN_VALID_MAGIC ||
        record.run.source_slot > IAP_SLOT_B) {
        return IAP_ERR_NO_IMAGE;
    }
    slot = record.run.source_slot;
    if (record.slot[slot].state != IAP_IMAGE_PENDING &&
        record.slot[slot].state != IAP_IMAGE_CONFIRMED) {
        return IAP_ERR_STATE;
    }
    record.slot[slot].state = IAP_IMAGE_CONFIRMED;
    record.slot[slot].boot_attempts = 0u;
    return iap_metadata_store(&record);
}
