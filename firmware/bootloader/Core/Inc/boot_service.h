#ifndef IAP_BOOT_SERVICE_H
#define IAP_BOOT_SERVICE_H

#include "iap_protocol.h"
#include "iap_storage.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    IapMetaRecord meta;
    IapDownloadSession download;
    uint32_t permanent_hold;
    uint32_t boot_requested;
} IapBootService;

IapError iap_boot_service_init(IapBootService *service);
size_t iap_boot_process_wire(IapBootService *service,
                             const uint8_t *request,
                             size_t request_length,
                             uint8_t *response,
                             size_t response_capacity);

#endif
