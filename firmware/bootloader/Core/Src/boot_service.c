#include "boot_service.h"

#include "flash_if.h"
#include "metadata.h"

#include <string.h>

#define START_DATA_LENGTH 13u
#define DATA_HEADER_LENGTH 8u
#define REPLY_DATA_LENGTH 10u

static IapSlot other_slot(IapSlot slot)
{
    return slot == IAP_SLOT_A ? IAP_SLOT_B : IAP_SLOT_A;
}

static uint32_t next_image_generation(const IapMetaRecord *meta)
{
    uint32_t generation = meta->slot[0].generation;

    if (meta->slot[1].generation > generation) {
        generation = meta->slot[1].generation;
    }
    ++generation;
    return generation == 0u ? 1u : generation;
}

static IapSlot select_automatic_slot(const IapMetaRecord *meta)
{
    if (meta->run.valid == IAP_RUN_VALID_MAGIC &&
        meta->run.source_slot <= IAP_SLOT_B) {
        return other_slot((IapSlot)meta->run.source_slot);
    }
    if (meta->slot[IAP_SLOT_A].state == IAP_IMAGE_CONFIRMED &&
        meta->slot[IAP_SLOT_B].state != IAP_IMAGE_CONFIRMED) {
        return IAP_SLOT_B;
    }
    if (meta->slot[IAP_SLOT_B].state == IAP_IMAGE_CONFIRMED &&
        meta->slot[IAP_SLOT_A].state != IAP_IMAGE_CONFIRMED) {
        return IAP_SLOT_A;
    }
    return meta->slot[IAP_SLOT_A].generation <=
                   meta->slot[IAP_SLOT_B].generation
               ? IAP_SLOT_A
               : IAP_SLOT_B;
}

static int target_would_destroy_only_fallback(const IapMetaRecord *meta,
                                               IapSlot target)
{
    IapSlot other = other_slot(target);

    if (meta->run.valid == IAP_RUN_VALID_MAGIC &&
        meta->run.source_slot == (uint32_t)target) {
        return 1;
    }
    return meta->slot[target].state == IAP_IMAGE_CONFIRMED &&
           meta->slot[other].state != IAP_IMAGE_CONFIRMED;
}

static IapError handle_start(IapBootService *service,
                             const IapFrame *request)
{
    IapMetaRecord previous;
    IapSlot target;
    uint8_t requested_target;
    uint32_t version;
    uint32_t size;
    uint32_t crc32;
    IapError error;

    if (request->data_length != START_DATA_LENGTH) {
        return IAP_ERR_FRAME_LENGTH;
    }
    requested_target = request->data[0];
    version = iap_get_u32_be(&request->data[1]);
    size = iap_get_u32_be(&request->data[5]);
    crc32 = iap_get_u32_be(&request->data[9]);

    if (service->download.active != 0u) {
        if ((requested_target == IAP_SLOT_AUTO ||
             requested_target == service->download.target) &&
            version == service->download.version &&
            size == service->download.expected_size &&
            crc32 == service->download.expected_crc32) {
            return IAP_ERR_OK;
        }
        return IAP_ERR_STATE;
    }
    if (version == 0u || size < 8u || size > IAP_RUN_SIZE) {
        return IAP_ERR_IMAGE_SIZE;
    }
    if (requested_target == IAP_SLOT_AUTO) {
        target = select_automatic_slot(&service->meta);
    } else if (requested_target == IAP_SLOT_A ||
               requested_target == IAP_SLOT_B) {
        target = (IapSlot)requested_target;
    } else {
        return IAP_ERR_TARGET;
    }
    if (target_would_destroy_only_fallback(&service->meta, target)) {
        return IAP_ERR_TARGET;
    }

    previous = service->meta;
    service->meta.slot[target].state = IAP_IMAGE_DOWNLOADING;
    service->meta.slot[target].version = version;
    service->meta.slot[target].size = size;
    service->meta.slot[target].crc32 = crc32;
    service->meta.slot[target].generation =
        next_image_generation(&service->meta);
    service->meta.slot[target].boot_attempts = 0u;
    error = iap_metadata_store(&service->meta);
    if (error != IAP_ERR_OK) {
        service->meta = previous;
        return error;
    }

    error = iap_flash_erase_region(iap_slot_base(target), IAP_SLOT_A_SIZE);
    if (error != IAP_ERR_OK) {
        return error;
    }

    memset(&service->download, 0, sizeof(service->download));
    service->download.active = 1u;
    service->download.target = target;
    service->download.version = version;
    service->download.expected_size = size;
    service->download.expected_crc32 = crc32;
    return IAP_ERR_OK;
}

static IapError handle_data(IapBootService *service,
                            const IapFrame *request)
{
    uint32_t sequence;
    uint32_t offset;
    uint32_t amount;
    uint32_t end;
    uint32_t address;
    IapError error;

    if (request->data_length <= DATA_HEADER_LENGTH ||
        request->data_length > DATA_HEADER_LENGTH + IAP_DATA_BYTES) {
        return IAP_ERR_FRAME_LENGTH;
    }
    if (service->download.active == 0u ||
        service->download.target > IAP_SLOT_B) {
        return IAP_ERR_STATE;
    }

    sequence = iap_get_u32_be(&request->data[0]);
    offset = iap_get_u32_be(&request->data[4]);
    amount = request->data_length - DATA_HEADER_LENGTH;
    end = offset + amount;
    if (end < offset || end > service->download.expected_size) {
        return IAP_ERR_IMAGE_SIZE;
    }
    address = iap_slot_base((IapSlot)service->download.target) + offset;

    if (sequence < service->download.next_sequence &&
        end <= service->download.received) {
        return iap_flash_equal(address,
                               &request->data[DATA_HEADER_LENGTH],
                               amount)
                   ? IAP_ERR_OK
                   : IAP_ERR_SEQUENCE;
    }
    if (sequence != service->download.next_sequence) {
        return IAP_ERR_SEQUENCE;
    }
    if (offset != service->download.received) {
        return IAP_ERR_OFFSET;
    }

    error = iap_flash_program(address,
                              &request->data[DATA_HEADER_LENGTH],
                              amount);
    if (error != IAP_ERR_OK) {
        return error;
    }
    service->download.received = end;
    ++service->download.next_sequence;
    return IAP_ERR_OK;
}

static IapError handle_end(IapBootService *service,
                           const IapFrame *request)
{
    IapSlot target;
    IapError error;

    if (request->data_length != 0u) {
        return IAP_ERR_FRAME_LENGTH;
    }
    if (service->download.active == 0u ||
        service->download.target > IAP_SLOT_B) {
        return IAP_ERR_STATE;
    }
    if (service->download.received != service->download.expected_size) {
        return IAP_ERR_OFFSET;
    }
    target = (IapSlot)service->download.target;
    if (iap_flash_crc32(iap_slot_base(target),
                        service->download.expected_size) !=
        service->download.expected_crc32) {
        service->meta.slot[target].state = IAP_IMAGE_BAD;
        error = iap_metadata_store(&service->meta);
        service->download.active = 0u;
        return error == IAP_ERR_OK ? IAP_ERR_IMAGE_CRC : error;
    }

    service->meta.slot[target].state = IAP_IMAGE_READY;
    error = iap_metadata_store(&service->meta);
    if (error != IAP_ERR_OK) {
        service->meta.slot[target].state = IAP_IMAGE_DOWNLOADING;
        return error;
    }
    service->download.active = 0u;
    return IAP_ERR_OK;
}

static IapError handle_abort(IapBootService *service,
                             const IapFrame *request)
{
    IapError error = IAP_ERR_OK;

    if (request->data_length != 0u) {
        return IAP_ERR_FRAME_LENGTH;
    }
    if (service->download.active != 0u &&
        service->download.target <= IAP_SLOT_B) {
        service->meta.slot[service->download.target].state = IAP_IMAGE_BAD;
        error = iap_metadata_store(&service->meta);
    }
    memset(&service->download, 0, sizeof(service->download));
    return error;
}

static int has_bootable_image(const IapMetaRecord *meta)
{
    uint32_t index;

    if (meta->run.valid == IAP_RUN_VALID_MAGIC &&
        meta->run.source_slot <= IAP_SLOT_B) {
        uint32_t state = meta->slot[meta->run.source_slot].state;
        if (state == IAP_IMAGE_PENDING || state == IAP_IMAGE_CONFIRMED) {
            return 1;
        }
    }
    for (index = 0u; index < 2u; ++index) {
        if (meta->slot[index].state == IAP_IMAGE_READY ||
            meta->slot[index].state == IAP_IMAGE_CONFIRMED) {
            return 1;
        }
    }
    return 0;
}

static IapError dispatch_request(IapBootService *service,
                                 const IapFrame *request)
{
    if (!iap_is_request_command(request->command)) {
        return IAP_ERR_COMMAND;
    }
    switch (request->command) {
    case IAP_CMD_HELLO:
        return request->data_length == 0u ? IAP_ERR_OK
                                         : IAP_ERR_FRAME_LENGTH;
    case IAP_CMD_HOLD:
        if (request->data_length != 0u) {
            return IAP_ERR_FRAME_LENGTH;
        }
        service->permanent_hold = 1u;
        return IAP_ERR_OK;
    case IAP_CMD_START:
        return handle_start(service, request);
    case IAP_CMD_DATA:
        return handle_data(service, request);
    case IAP_CMD_END:
        return handle_end(service, request);
    case IAP_CMD_ABORT:
        return handle_abort(service, request);
    case IAP_CMD_STATUS:
        return request->data_length == 0u ? IAP_ERR_OK
                                         : IAP_ERR_FRAME_LENGTH;
    case IAP_CMD_BOOT:
        if (request->data_length != 0u) {
            return IAP_ERR_FRAME_LENGTH;
        }
        if (!has_bootable_image(&service->meta)) {
            return IAP_ERR_NO_IMAGE;
        }
        service->boot_requested = 1u;
        return IAP_ERR_OK;
    default:
        return IAP_ERR_COMMAND;
    }
}

static size_t encode_reply(const IapBootService *service,
                           IapError error,
                           uint8_t *response,
                           size_t response_capacity)
{
    IapFrame reply;
    uint32_t next_sequence = 0u;
    uint32_t expected_offset = 0u;

    if (service != NULL) {
        next_sequence = service->download.next_sequence;
        expected_offset = service->download.received;
    }
    memset(&reply, 0, sizeof(reply));
    reply.command = error == IAP_ERR_OK ? IAP_CMD_ACK : IAP_CMD_NACK;
    reply.data_length = REPLY_DATA_LENGTH;
    iap_put_u32_be(&reply.data[0], next_sequence);
    iap_put_u32_be(&reply.data[4], expected_offset);
    iap_put_u16_be(&reply.data[8], (uint16_t)error);
    return iap_frame_encode(&reply, response, response_capacity);
}

IapError iap_boot_service_init(IapBootService *service)
{
    if (service == NULL) {
        return IAP_ERR_STATE;
    }
    memset(service, 0, sizeof(*service));
    return iap_metadata_load(&service->meta);
}

size_t iap_boot_process_wire(IapBootService *service,
                             const uint8_t *request,
                             size_t request_length,
                             uint8_t *response,
                             size_t response_capacity)
{
    IapFrame decoded;
    IapError error;

    if (response == NULL) {
        return 0u;
    }
    if (service == NULL || request == NULL) {
        return encode_reply(service,
                            IAP_ERR_STATE,
                            response,
                            response_capacity);
    }
    error = iap_frame_decode(request, request_length, &decoded);
    if (error == IAP_ERR_OK) {
        error = dispatch_request(service, &decoded);
    }
    return encode_reply(service, error, response, response_capacity);
}
