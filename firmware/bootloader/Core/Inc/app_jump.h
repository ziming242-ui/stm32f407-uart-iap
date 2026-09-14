#ifndef IAP_APP_JUMP_H
#define IAP_APP_JUMP_H

#include "iap_config.h"

#include <stdint.h>

IapError iap_app_vectors_read_and_validate(uint32_t app_base,
                                           uint32_t image_size,
                                           uint32_t *initial_msp,
                                           uint32_t *reset_handler);
void iap_jump_to_application(uint32_t app_base,
                             uint32_t initial_msp,
                             uint32_t reset_handler);

#endif
