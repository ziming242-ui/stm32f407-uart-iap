#include "mock_platform.h"

#include "flash_if.h"
#include "iap_crc.h"
#include "metadata.h"

#include <stddef.h>
#include <string.h>

static uint8_t run_flash[IAP_RUN_SIZE];
static uint8_t slot_a_flash[IAP_SLOT_A_SIZE];
static uint8_t slot_b_flash[IAP_SLOT_B_SIZE];
static IapMetaRecord persisted_meta;
static int persisted_meta_valid;

static uint8_t *address_to_bytes(uint32_t address, uint32_t length)
{
    uint32_t offset;
    if (address >= IAP_RUN_BASE &&
        address + length >= address &&
        address + length <= IAP_RUN_BASE + IAP_RUN_SIZE) {
        offset = address - IAP_RUN_BASE;
        return &run_flash[offset];
    }
    if (address >= IAP_SLOT_A_BASE &&
        address + length >= address &&
        address + length <= IAP_SLOT_A_BASE + IAP_SLOT_A_SIZE) {
        offset = address - IAP_SLOT_A_BASE;
        return &slot_a_flash[offset];
    }
    if (address >= IAP_SLOT_B_BASE &&
        address + length >= address &&
        address + length <= IAP_SLOT_B_BASE + IAP_SLOT_B_SIZE) {
        offset = address - IAP_SLOT_B_BASE;
        return &slot_b_flash[offset];
    }
    return NULL;
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
    return address_to_bytes(address, length) != NULL;
}

IapError iap_flash_erase_region(uint32_t address, uint32_t length)
{
    uint8_t *bytes = address_to_bytes(address, length);
    if (bytes == NULL ||
        !((address == IAP_RUN_BASE && length == IAP_RUN_SIZE) ||
          (address == IAP_SLOT_A_BASE && length == IAP_SLOT_A_SIZE) ||
          (address == IAP_SLOT_B_BASE && length == IAP_SLOT_B_SIZE))) {
        return IAP_ERR_FLASH;
    }
    memset(bytes, 0xFF, length);
    return IAP_ERR_OK;
}

IapError iap_flash_program(uint32_t address,
                           const uint8_t *data,
                           uint32_t length)
{
    uint8_t *bytes = address_to_bytes(address, length);
    uint32_t index;
    if (bytes == NULL || data == NULL) {
        return IAP_ERR_FLASH;
    }
    for (index = 0u; index < length; ++index) {
        if ((bytes[index] & data[index]) != data[index]) {
            return IAP_ERR_FLASH;
        }
    }
    for (index = 0u; index < length; ++index) {
        bytes[index] &= data[index];
    }
    return IAP_ERR_OK;
}

int iap_flash_equal(uint32_t address,
                    const uint8_t *data,
                    uint32_t length)
{
    uint8_t *bytes = address_to_bytes(address, length);
    return bytes != NULL && data != NULL && memcmp(bytes, data, length) == 0;
}

uint32_t iap_flash_crc32(uint32_t address, uint32_t length)
{
    uint8_t *bytes = address_to_bytes(address, length);
    return bytes == NULL ? 0u : iap_crc32_ieee(bytes, length);
}

void iap_metadata_default(IapMetaRecord *record)
{
    memset(record, 0, sizeof(*record));
    record->magic = IAP_META_MAGIC;
    record->schema = IAP_META_SCHEMA;
    record->record_size = IAP_META_RECORD_SIZE;
    record->run.source_slot = IAP_SLOT_NONE;
}

IapError iap_metadata_load(IapMetaRecord *record)
{
    if (record == NULL) {
        return IAP_ERR_FLASH;
    }
    if (persisted_meta_valid) {
        *record = persisted_meta;
    } else {
        iap_metadata_default(record);
    }
    return IAP_ERR_OK;
}

IapError iap_metadata_store(IapMetaRecord *record)
{
    if (record == NULL) {
        return IAP_ERR_FLASH;
    }
    record->magic = IAP_META_MAGIC;
    record->schema = IAP_META_SCHEMA;
    record->record_size = IAP_META_RECORD_SIZE;
    record->record_generation += 1u;
    record->record_crc32 = iap_crc32_ieee(
        (const uint8_t *)record,
        offsetof(IapMetaRecord, record_crc32));
    record->commit_magic = IAP_META_COMMIT_MAGIC;
    persisted_meta = *record;
    persisted_meta_valid = 1;
    return IAP_ERR_OK;
}

IapError iap_metadata_confirm_running(void)
{
    IapMetaRecord record;
    uint32_t slot;
    iap_metadata_load(&record);
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

void sim_storage_reset(void)
{
    memset(run_flash, 0xFF, sizeof(run_flash));
    memset(slot_a_flash, 0xFF, sizeof(slot_a_flash));
    memset(slot_b_flash, 0xFF, sizeof(slot_b_flash));
    memset(&persisted_meta, 0, sizeof(persisted_meta));
    persisted_meta_valid = 0;
}

void sim_seed_confirmed(IapSlot slot,
                        const uint8_t *image,
                        uint32_t size,
                        uint32_t version)
{
    IapMetaRecord record;
    uint8_t *destination = slot == IAP_SLOT_A ? slot_a_flash : slot_b_flash;
    sim_storage_reset();
    memcpy(destination, image, size);
    memcpy(run_flash, image, size);
    iap_metadata_default(&record);
    record.slot[slot].state = IAP_IMAGE_CONFIRMED;
    record.slot[slot].version = version;
    record.slot[slot].size = size;
    record.slot[slot].crc32 = iap_crc32_ieee(image, size);
    record.slot[slot].generation = 1u;
    record.run.valid = IAP_RUN_VALID_MAGIC;
    record.run.source_slot = slot;
    record.run.version = version;
    record.run.size = size;
    record.run.crc32 = record.slot[slot].crc32;
    (void)iap_metadata_store(&record);
}

static IapSlot newest_with_state(const IapMetaRecord *record,
                                 IapImageState state,
                                 IapSlot excluded)
{
    IapSlot best = IAP_SLOT_NONE;
    uint32_t index;
    for (index = 0u; index < 2u; ++index) {
        if (index == (uint32_t)excluded || record->slot[index].state != state) {
            continue;
        }
        if (best == IAP_SLOT_NONE ||
            record->slot[index].generation > record->slot[best].generation) {
            best = (IapSlot)index;
        }
    }
    return best;
}

static SimBootResult copy_and_start(IapMetaRecord *record,
                                    IapSlot slot,
                                    IapImageState state,
                                    uint32_t power_loss_after,
                                    IapSlot *started_slot)
{
    uint8_t *source = slot == IAP_SLOT_A ? slot_a_flash : slot_b_flash;
    uint32_t copied = 0u;
    const uint32_t size = record->slot[slot].size;

    if (size < 8u || size > IAP_RUN_SIZE ||
        iap_crc32_ieee(source, size) != record->slot[slot].crc32) {
        return SIM_BOOT_CORRUPT;
    }
    memset(run_flash, 0xFF, sizeof(run_flash));
    while (copied < size) {
        uint32_t amount = size - copied;
        if (amount > IAP_DATA_BYTES) {
            amount = IAP_DATA_BYTES;
        }
        memcpy(&run_flash[copied], &source[copied], amount);
        copied += amount;
        if (power_loss_after != SIM_NO_POWER_LOSS &&
            copied >= power_loss_after && copied < size) {
            return SIM_BOOT_POWER_LOSS;
        }
    }
    record->slot[slot].state = state;
    record->slot[slot].boot_attempts = state == IAP_IMAGE_PENDING ? 1u : 0u;
    record->run.valid = IAP_RUN_VALID_MAGIC;
    record->run.source_slot = slot;
    record->run.version = record->slot[slot].version;
    record->run.size = size;
    record->run.crc32 = record->slot[slot].crc32;
    (void)iap_metadata_store(record);
    *started_slot = slot;
    return SIM_BOOT_STARTED;
}

SimBootResult sim_boot_prepare(uint32_t power_loss_after, IapSlot *started_slot)
{
    IapMetaRecord record;
    IapSlot slot;
    iap_metadata_load(&record);

    if (record.run.valid == IAP_RUN_VALID_MAGIC &&
        record.run.source_slot <= IAP_SLOT_B &&
        record.slot[record.run.source_slot].state == IAP_IMAGE_PENDING) {
        IapSlot pending = (IapSlot)record.run.source_slot;
        IapSlot fallback = newest_with_state(&record,
                                             IAP_IMAGE_CONFIRMED,
                                             pending);
        if (fallback != IAP_SLOT_NONE) {
            record.slot[pending].state = IAP_IMAGE_BAD;
            return copy_and_start(&record, fallback, IAP_IMAGE_CONFIRMED,
                                  power_loss_after, started_slot);
        }
    }

    slot = newest_with_state(&record, IAP_IMAGE_READY, IAP_SLOT_NONE);
    if (slot != IAP_SLOT_NONE) {
        return copy_and_start(&record, slot, IAP_IMAGE_PENDING,
                              power_loss_after, started_slot);
    }
    if (record.run.valid == IAP_RUN_VALID_MAGIC &&
        record.run.source_slot <= IAP_SLOT_B &&
        record.slot[record.run.source_slot].state == IAP_IMAGE_CONFIRMED &&
        iap_crc32_ieee(run_flash, record.run.size) == record.run.crc32) {
        *started_slot = (IapSlot)record.run.source_slot;
        return SIM_BOOT_STARTED;
    }
    slot = newest_with_state(&record, IAP_IMAGE_CONFIRMED, IAP_SLOT_NONE);
    if (slot != IAP_SLOT_NONE) {
        return copy_and_start(&record, slot, IAP_IMAGE_CONFIRMED,
                              power_loss_after, started_slot);
    }
    return SIM_BOOT_NO_IMAGE;
}

IapError sim_app_confirm(void)
{
    return iap_metadata_confirm_running();
}

IapMetaRecord sim_meta_snapshot(void)
{
    IapMetaRecord record;
    (void)iap_metadata_load(&record);
    return record;
}

const uint8_t *sim_slot_bytes(IapSlot slot)
{
    return slot == IAP_SLOT_A ? slot_a_flash : slot_b_flash;
}

const uint8_t *sim_run_bytes(void)
{
    return run_flash;
}
