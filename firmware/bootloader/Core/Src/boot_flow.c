#include "boot_flow.h"

#include "app_jump.h"
#include "flash_if.h"
#include "metadata.h"

#include <stdint.h>

#define SRAM_BASE_ADDRESS 0x20000000u
#define SRAM_END_ADDRESS  0x20020000u

static int image_meta_is_sane(const IapImageMeta *image)
{
    return image->size >= 8u && image->size <= IAP_RUN_SIZE &&
           image->version != 0u;
}

static int stored_vectors_are_valid(IapSlot slot, uint32_t image_size)
{
    const volatile uint32_t *vectors;
    uint32_t initial_msp;
    uint32_t reset_handler;
    uint32_t reset_code;

    if (slot > IAP_SLOT_B || image_size < 8u || image_size > IAP_RUN_SIZE) {
        return 0;
    }
    vectors = (const volatile uint32_t *)(uintptr_t)iap_slot_base(slot);
    initial_msp = vectors[0];
    reset_handler = vectors[1];
    reset_code = reset_handler & ~1u;

    return initial_msp >= SRAM_BASE_ADDRESS &&
           initial_msp <= SRAM_END_ADDRESS &&
           (initial_msp & 7u) == 0u &&
           (reset_handler & 1u) != 0u &&
           reset_code >= IAP_RUN_BASE &&
           reset_code < IAP_RUN_BASE + image_size;
}

static IapError validate_stored_slot(const IapMetaRecord *meta, IapSlot slot)
{
    const IapImageMeta *image;

    if (slot > IAP_SLOT_B) {
        return IAP_ERR_TARGET;
    }
    image = &meta->slot[slot];
    if (!image_meta_is_sane(image)) {
        return IAP_ERR_IMAGE_SIZE;
    }
    if (iap_flash_crc32(iap_slot_base(slot), image->size) != image->crc32) {
        return IAP_ERR_IMAGE_CRC;
    }
    if (!stored_vectors_are_valid(slot, image->size)) {
        return IAP_ERR_VECTOR;
    }
    return IAP_ERR_OK;
}

static IapError validate_running_image(const IapMetaRecord *meta,
                                       uint32_t *initial_msp,
                                       uint32_t *reset_handler)
{
    const IapImageMeta *source;

    if (meta->run.valid != IAP_RUN_VALID_MAGIC ||
        meta->run.source_slot > IAP_SLOT_B) {
        return IAP_ERR_NO_IMAGE;
    }
    source = &meta->slot[meta->run.source_slot];
    if (!image_meta_is_sane(source) ||
        meta->run.version != source->version ||
        meta->run.size != source->size ||
        meta->run.crc32 != source->crc32) {
        return IAP_ERR_STATE;
    }
    if (iap_flash_crc32(IAP_RUN_BASE, meta->run.size) != meta->run.crc32) {
        return IAP_ERR_IMAGE_CRC;
    }
    return iap_app_vectors_read_and_validate(IAP_RUN_BASE,
                                              meta->run.size,
                                              initial_msp,
                                              reset_handler);
}

static IapSlot newest_slot_with_state(const IapMetaRecord *meta,
                                      IapImageState state,
                                      IapSlot excluded)
{
    IapSlot selected = IAP_SLOT_NONE;
    uint32_t index;

    for (index = 0u; index < 2u; ++index) {
        if (index == (uint32_t)excluded || meta->slot[index].state != state) {
            continue;
        }
        if (selected == IAP_SLOT_NONE ||
            meta->slot[index].generation > meta->slot[selected].generation) {
            selected = (IapSlot)index;
        }
    }
    return selected;
}

static IapError copy_slot_to_run(const IapMetaRecord *meta,
                                 IapSlot slot,
                                 uint32_t *initial_msp,
                                 uint32_t *reset_handler)
{
    const IapImageMeta *image = &meta->slot[slot];
    uint32_t offset;
    IapError error;

    error = validate_stored_slot(meta, slot);
    if (error != IAP_ERR_OK) {
        return error;
    }
    error = iap_flash_erase_region(IAP_RUN_BASE, IAP_RUN_SIZE);
    if (error != IAP_ERR_OK) {
        return error;
    }

    for (offset = 0u; offset < image->size; offset += IAP_DATA_BYTES) {
        uint32_t amount = image->size - offset;
        if (amount > IAP_DATA_BYTES) {
            amount = IAP_DATA_BYTES;
        }
        error = iap_flash_program(
            IAP_RUN_BASE + offset,
            (const uint8_t *)(uintptr_t)(iap_slot_base(slot) + offset),
            amount);
        if (error != IAP_ERR_OK) {
            return error;
        }
    }

    if (iap_flash_crc32(IAP_RUN_BASE, image->size) != image->crc32) {
        return IAP_ERR_IMAGE_CRC;
    }
    return iap_app_vectors_read_and_validate(IAP_RUN_BASE,
                                              image->size,
                                              initial_msp,
                                              reset_handler);
}

static IapError activate_slot(IapMetaRecord *meta,
                              IapSlot slot,
                              IapImageState activation_state)
{
    uint32_t initial_msp;
    uint32_t reset_handler;
    IapError error;

    error = copy_slot_to_run(meta, slot, &initial_msp, &reset_handler);
    if (error != IAP_ERR_OK) {
        return error;
    }

    meta->slot[slot].state = activation_state;
    meta->slot[slot].boot_attempts =
        activation_state == IAP_IMAGE_PENDING ? 1u : 0u;
    meta->run.valid = IAP_RUN_VALID_MAGIC;
    meta->run.source_slot = slot;
    meta->run.version = meta->slot[slot].version;
    meta->run.size = meta->slot[slot].size;
    meta->run.crc32 = meta->slot[slot].crc32;
    error = iap_metadata_store(meta);
    if (error != IAP_ERR_OK) {
        return error;
    }

    iap_jump_to_application(IAP_RUN_BASE, initial_msp, reset_handler);
    return IAP_ERR_STATE;
}

static IapError reject_slot(IapMetaRecord *meta, IapSlot slot)
{
    meta->slot[slot].state = IAP_IMAGE_BAD;
    meta->slot[slot].boot_attempts = 0u;
    if (meta->run.valid == IAP_RUN_VALID_MAGIC &&
        meta->run.source_slot == (uint32_t)slot) {
        meta->run.valid = 0u;
        meta->run.source_slot = IAP_SLOT_NONE;
    }
    return iap_metadata_store(meta);
}

IapError iap_boot_try_start_application(void)
{
    IapMetaRecord meta;
    IapSlot slot;
    uint32_t initial_msp;
    uint32_t reset_handler;
    IapError error;

    error = iap_metadata_load(&meta);
    if (error != IAP_ERR_OK) {
        return error;
    }

    /* A reset before APP confirmation means rollback when a known-good slot exists. */
    if (meta.run.valid == IAP_RUN_VALID_MAGIC &&
        meta.run.source_slot <= IAP_SLOT_B &&
        meta.slot[meta.run.source_slot].state == IAP_IMAGE_PENDING) {
        IapSlot pending = (IapSlot)meta.run.source_slot;
        IapSlot fallback = newest_slot_with_state(&meta,
                                                  IAP_IMAGE_CONFIRMED,
                                                  pending);

        if (fallback != IAP_SLOT_NONE) {
            error = validate_stored_slot(&meta, fallback);
            if (error == IAP_ERR_OK) {
                meta.slot[pending].state = IAP_IMAGE_BAD;
                meta.slot[pending].boot_attempts = 0u;
                return activate_slot(&meta, fallback, IAP_IMAGE_CONFIRMED);
            }
            meta.slot[fallback].state = IAP_IMAGE_BAD;
            error = iap_metadata_store(&meta);
            if (error != IAP_ERR_OK) {
                return error;
            }
        }

        error = validate_running_image(&meta, &initial_msp, &reset_handler);
        if (error == IAP_ERR_OK &&
            meta.slot[pending].boot_attempts < IAP_PENDING_BOOT_LIMIT) {
            meta.slot[pending].boot_attempts += 1u;
            error = iap_metadata_store(&meta);
            if (error != IAP_ERR_OK) {
                return error;
            }
            iap_jump_to_application(IAP_RUN_BASE, initial_msp, reset_handler);
        }

        error = reject_slot(&meta, pending);
        if (error != IAP_ERR_OK) {
            return error;
        }
    }

    /* A completed download takes precedence over the currently confirmed image. */
    for (;;) {
        slot = newest_slot_with_state(&meta, IAP_IMAGE_READY, IAP_SLOT_NONE);
        if (slot == IAP_SLOT_NONE) {
            break;
        }
        error = validate_stored_slot(&meta, slot);
        if (error == IAP_ERR_OK) {
            return activate_slot(&meta, slot, IAP_IMAGE_PENDING);
        }
        error = reject_slot(&meta, slot);
        if (error != IAP_ERR_OK) {
            return error;
        }
    }

    if (meta.run.valid == IAP_RUN_VALID_MAGIC &&
        meta.run.source_slot <= IAP_SLOT_B &&
        meta.slot[meta.run.source_slot].state == IAP_IMAGE_CONFIRMED) {
        error = validate_running_image(&meta, &initial_msp, &reset_handler);
        if (error == IAP_ERR_OK) {
            iap_jump_to_application(IAP_RUN_BASE, initial_msp, reset_handler);
        }
    }

    /* Restore Run from the newest valid confirmed copy, including self-repair. */
    for (;;) {
        slot = newest_slot_with_state(&meta, IAP_IMAGE_CONFIRMED, IAP_SLOT_NONE);
        if (slot == IAP_SLOT_NONE) {
            break;
        }
        error = validate_stored_slot(&meta, slot);
        if (error == IAP_ERR_OK) {
            return activate_slot(&meta, slot, IAP_IMAGE_CONFIRMED);
        }
        error = reject_slot(&meta, slot);
        if (error != IAP_ERR_OK) {
            return error;
        }
    }

    /* Development escape hatch: allow a manually flashed Run image only before
       the first metadata record exists. This path has no image CRC check. */
    if (meta.record_generation == 0u &&
        iap_app_vectors_read_and_validate(IAP_RUN_BASE,
                                           IAP_RUN_SIZE,
                                           &initial_msp,
                                           &reset_handler) == IAP_ERR_OK) {
        iap_jump_to_application(IAP_RUN_BASE, initial_msp, reset_handler);
    }
    return IAP_ERR_NO_IMAGE;
}
