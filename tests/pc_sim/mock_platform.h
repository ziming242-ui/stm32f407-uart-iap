#ifndef MOCK_PLATFORM_H
#define MOCK_PLATFORM_H

#include "iap_storage.h"

#include <stdint.h>

#define SIM_NO_POWER_LOSS 0xFFFFFFFFu

typedef enum {
    SIM_BOOT_STARTED = 0,
    SIM_BOOT_NO_IMAGE,
    SIM_BOOT_POWER_LOSS,
    SIM_BOOT_CORRUPT
} SimBootResult;

void sim_storage_reset(void);
void sim_seed_confirmed(IapSlot slot,
                        const uint8_t *image,
                        uint32_t size,
                        uint32_t version);
SimBootResult sim_boot_prepare(uint32_t power_loss_after, IapSlot *started_slot);
IapError sim_app_confirm(void);
IapMetaRecord sim_meta_snapshot(void);
const uint8_t *sim_slot_bytes(IapSlot slot);
const uint8_t *sim_run_bytes(void);

#endif
