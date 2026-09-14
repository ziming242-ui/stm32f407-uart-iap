#include "boot_service.h"
#include "iap_crc.h"
#include "iap_protocol.h"
#include "mock_platform.h"

#include <stdio.h>
#include <string.h>

#define IMAGE_CAPACITY 2048u

typedef struct {
    uint16_t command;
    uint32_t next_sequence;
    uint32_t expected_offset;
    IapError error;
} Reply;

static int failures;

#define CHECK(condition, message)                                    \
    do {                                                             \
        if (!(condition)) {                                          \
            printf("  FAIL: %s (line %d)\n", message, __LINE__);     \
            ++failures;                                              \
            return 0;                                                \
        }                                                            \
    } while (0)

static void put_u32_le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static void make_image(uint8_t *image, uint32_t size, uint32_t version)
{
    uint32_t index;
    for (index = 0u; index < size; ++index) {
        image[index] = (uint8_t)(index * 31u + version * 17u);
    }
    put_u32_le(&image[0], 0x20010000u);
    put_u32_le(&image[4], (IAP_RUN_BASE + 0x101u));
    put_u32_le(&image[8], version);
}

static int exchange_wire(IapBootService *service,
                         const IapFrame *request,
                         int corrupt,
                         Reply *reply)
{
    uint8_t request_wire[IAP_FRAME_WIRE_MAX];
    uint8_t response_wire[IAP_FRAME_WIRE_MAX];
    IapFrame response;
    size_t request_length;
    size_t response_length;

    request_length = iap_frame_encode(request, request_wire,
                                      sizeof(request_wire));
    if (request_length == 0u) {
        return 0;
    }
    if (corrupt) {
        request_wire[request_length / 2u] ^= 0x40u;
    }
    response_length = iap_boot_process_wire(service,
                                            request_wire,
                                            request_length,
                                            response_wire,
                                            sizeof(response_wire));
    if (iap_frame_decode(response_wire, response_length, &response) !=
            IAP_ERR_OK ||
        response.data_length != 10u) {
        return 0;
    }
    reply->command = response.command;
    reply->next_sequence = iap_get_u32_be(&response.data[0]);
    reply->expected_offset = iap_get_u32_be(&response.data[4]);
    reply->error = (IapError)iap_get_u16_be(&response.data[8]);
    return 1;
}

static int send_empty(IapBootService *service, uint16_t command, Reply *reply)
{
    IapFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.command = command;
    return exchange_wire(service, &frame, 0, reply);
}

static int send_start(IapBootService *service,
                      uint8_t target,
                      uint32_t version,
                      uint32_t size,
                      uint32_t crc,
                      Reply *reply)
{
    IapFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.command = IAP_CMD_START;
    frame.data_length = 13u;
    frame.data[0] = target;
    iap_put_u32_be(&frame.data[1], version);
    iap_put_u32_be(&frame.data[5], size);
    iap_put_u32_be(&frame.data[9], crc);
    return exchange_wire(service, &frame, 0, reply);
}

static int send_data(IapBootService *service,
                     uint32_t sequence,
                     uint32_t offset,
                     const uint8_t *data,
                     uint32_t amount,
                     int corrupt,
                     Reply *reply)
{
    IapFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.command = IAP_CMD_DATA;
    frame.data_length = (uint16_t)(8u + amount);
    iap_put_u32_be(&frame.data[0], sequence);
    iap_put_u32_be(&frame.data[4], offset);
    memcpy(&frame.data[8], data, amount);
    return exchange_wire(service, &frame, corrupt, reply);
}

static int upload_remaining(IapBootService *service,
                            const uint8_t *image,
                            uint32_t size,
                            uint32_t offset,
                            uint32_t sequence)
{
    Reply reply;
    while (offset < size) {
        uint32_t amount = size - offset;
        if (amount > IAP_DATA_BYTES) {
            amount = IAP_DATA_BYTES;
        }
        if (!send_data(service, sequence, offset, &image[offset], amount,
                       0, &reply) || reply.command != IAP_CMD_ACK) {
            return 0;
        }
        offset += amount;
        ++sequence;
    }
    return 1;
}

static int test_crc_and_frame(void)
{
    static const uint8_t vector[] = "123456789";
    IapFrame input;
    IapFrame output;
    uint8_t wire[IAP_FRAME_WIRE_MAX];
    size_t length;
    CHECK(iap_crc16_ccitt_false(vector, 9u) == 0x29B1u,
          "CRC16 known vector");
    CHECK(iap_crc32_ieee(vector, 9u) == 0xCBF43926u,
          "CRC32 known vector");
    CHECK(sizeof(IapMetaRecord) == 128u, "metadata record is 128 bytes");

    memset(&input, 0, sizeof(input));
    input.command = IAP_CMD_HELLO;
    input.data_length = 3u;
    input.data[0] = 0x12u;
    input.data[1] = 0x34u;
    input.data[2] = 0x56u;
    length = iap_frame_encode(&input, wire, sizeof(wire));
    CHECK(iap_frame_decode(wire, length, &output) == IAP_ERR_OK,
          "frame round trip");
    CHECK(output.command == input.command && output.data_length == 3u &&
          memcmp(output.data, input.data, 3u) == 0,
          "frame fields survive round trip");
    puts("PASS crc_and_frame");
    return 1;
}

static int test_normal_and_duplicate(void)
{
    uint8_t image[IMAGE_CAPACITY];
    const uint32_t size = 700u;
    IapBootService service;
    IapMetaRecord meta;
    IapSlot started;
    Reply reply;

    make_image(image, size, 1u);
    sim_storage_reset();
    CHECK(iap_boot_service_init(&service) == IAP_ERR_OK, "service init");
    CHECK(send_empty(&service, IAP_CMD_HOLD, &reply) &&
          reply.command == IAP_CMD_ACK, "HOLD ACK");
    CHECK(send_start(&service, IAP_SLOT_AUTO, 1u, size,
                     iap_crc32_ieee(image, size), &reply) &&
          reply.command == IAP_CMD_ACK &&
          service.download.target == IAP_SLOT_A, "START auto selects A");

    CHECK(send_data(&service, 0u, 0u, image, 256u, 0, &reply) &&
          reply.next_sequence == 1u && reply.expected_offset == 256u,
          "first DATA ACK");
    CHECK(send_data(&service, 0u, 0u, image, 256u, 0, &reply) &&
          reply.command == IAP_CMD_ACK && reply.next_sequence == 1u &&
          reply.expected_offset == 256u,
          "lost ACK retransmission is idempotent");
    CHECK(upload_remaining(&service, image, size, 256u, 1u),
          "remaining DATA");
    CHECK(send_empty(&service, IAP_CMD_END, &reply) &&
          reply.command == IAP_CMD_ACK, "END ACK");
    meta = sim_meta_snapshot();
    CHECK(meta.slot[IAP_SLOT_A].state == IAP_IMAGE_READY,
          "slot A becomes READY");
    CHECK(send_empty(&service, IAP_CMD_BOOT, &reply) &&
          reply.command == IAP_CMD_ACK && service.boot_requested == 1u,
          "BOOT accepted");
    CHECK(sim_boot_prepare(SIM_NO_POWER_LOSS, &started) == SIM_BOOT_STARTED &&
          started == IAP_SLOT_A, "READY copied to Run");
    CHECK(memcmp(sim_run_bytes(), image, size) == 0, "Run matches image");
    CHECK(sim_app_confirm() == IAP_ERR_OK, "APP health confirmation");
    meta = sim_meta_snapshot();
    CHECK(meta.slot[IAP_SLOT_A].state == IAP_IMAGE_CONFIRMED,
          "slot A becomes CONFIRMED");
    puts("PASS normal_and_duplicate_ack_retry");
    return 1;
}

static int test_bad_frame_and_order(void)
{
    uint8_t image[IMAGE_CAPACITY];
    const uint32_t size = 600u;
    IapBootService service;
    Reply reply;

    make_image(image, size, 2u);
    sim_storage_reset();
    CHECK(iap_boot_service_init(&service) == IAP_ERR_OK, "service init");
    CHECK(send_start(&service, IAP_SLOT_AUTO, 2u, size,
                     iap_crc32_ieee(image, size), &reply), "START exchange");
    CHECK(send_data(&service, 0u, 0u, image, 256u, 1, &reply) &&
          reply.command == IAP_CMD_NACK && reply.error == IAP_ERR_FRAME_CRC &&
          reply.expected_offset == 0u, "bad CRC16 does not advance offset");
    CHECK(send_data(&service, 1u, 0u, image, 256u, 0, &reply) &&
          reply.command == IAP_CMD_NACK && reply.error == IAP_ERR_SEQUENCE,
          "out-of-order sequence rejected");
    CHECK(send_data(&service, 0u, 4u, image, 256u, 0, &reply) &&
          reply.command == IAP_CMD_NACK && reply.error == IAP_ERR_OFFSET,
          "wrong offset rejected");
    CHECK(service.download.received == 0u &&
          service.download.next_sequence == 0u,
          "rejected frames leave progress unchanged");
    puts("PASS bad_crc16_sequence_offset");
    return 1;
}

static int test_whole_image_crc_failure(void)
{
    uint8_t image[IMAGE_CAPACITY];
    const uint32_t size = 513u;
    IapBootService service;
    IapMetaRecord meta;
    Reply reply;

    make_image(image, size, 3u);
    sim_storage_reset();
    CHECK(iap_boot_service_init(&service) == IAP_ERR_OK, "service init");
    CHECK(send_start(&service, IAP_SLOT_AUTO, 3u, size,
                     iap_crc32_ieee(image, size) ^ 1u, &reply), "START exchange");
    CHECK(upload_remaining(&service, image, size, 0u, 0u), "upload image");
    CHECK(send_empty(&service, IAP_CMD_END, &reply) &&
          reply.command == IAP_CMD_NACK && reply.error == IAP_ERR_IMAGE_CRC,
          "whole-image CRC mismatch rejected");
    meta = sim_meta_snapshot();
    CHECK(meta.slot[IAP_SLOT_A].state == IAP_IMAGE_BAD,
          "bad image never becomes READY");
    puts("PASS whole_image_crc32_failure");
    return 1;
}

static int test_interrupted_download_preserves_old(void)
{
    uint8_t old_image[IMAGE_CAPACITY];
    uint8_t new_image[IMAGE_CAPACITY];
    const uint32_t old_size = 512u;
    const uint32_t new_size = 900u;
    IapBootService before_reset;
    IapBootService after_reset;
    IapMetaRecord meta;
    IapSlot started;
    Reply reply;

    make_image(old_image, old_size, 10u);
    make_image(new_image, new_size, 11u);
    sim_seed_confirmed(IAP_SLOT_A, old_image, old_size, 10u);
    CHECK(iap_boot_service_init(&before_reset) == IAP_ERR_OK, "service init");
    CHECK(send_start(&before_reset, IAP_SLOT_AUTO, 11u, new_size,
                     iap_crc32_ieee(new_image, new_size), &reply) &&
          before_reset.download.target == IAP_SLOT_B, "new image targets B");
    CHECK(send_data(&before_reset, 0u, 0u, new_image, 256u, 0, &reply),
          "partial DATA accepted");

    CHECK(iap_boot_service_init(&after_reset) == IAP_ERR_OK,
          "power cycle clears RAM session");
    meta = sim_meta_snapshot();
    CHECK(after_reset.download.active == 0u &&
          meta.slot[IAP_SLOT_A].state == IAP_IMAGE_CONFIRMED &&
          meta.slot[IAP_SLOT_B].state == IAP_IMAGE_DOWNLOADING,
          "interrupted B is not bootable and A stays confirmed");
    CHECK(sim_boot_prepare(SIM_NO_POWER_LOSS, &started) == SIM_BOOT_STARTED &&
          started == IAP_SLOT_A &&
          memcmp(sim_run_bytes(), old_image, old_size) == 0,
          "old confirmed Run still starts");
    puts("PASS interrupted_download_preserves_confirmed");
    return 1;
}

static int test_copy_power_loss_and_rollback(void)
{
    uint8_t old_image[IMAGE_CAPACITY];
    uint8_t new_image[IMAGE_CAPACITY];
    const uint32_t old_size = 512u;
    const uint32_t new_size = 1024u;
    IapBootService service;
    IapMetaRecord meta;
    IapSlot started;
    Reply reply;

    make_image(old_image, old_size, 20u);
    make_image(new_image, new_size, 21u);
    sim_seed_confirmed(IAP_SLOT_A, old_image, old_size, 20u);
    CHECK(iap_boot_service_init(&service) == IAP_ERR_OK, "service init");
    CHECK(send_start(&service, IAP_SLOT_AUTO, 21u, new_size,
                     iap_crc32_ieee(new_image, new_size), &reply), "START B");
    CHECK(upload_remaining(&service, new_image, new_size, 0u, 0u),
          "upload B");
    CHECK(send_empty(&service, IAP_CMD_END, &reply) &&
          reply.command == IAP_CMD_ACK, "B READY");

    CHECK(sim_boot_prepare(300u, &started) == SIM_BOOT_POWER_LOSS,
          "power loss during copy is modeled");
    meta = sim_meta_snapshot();
    CHECK(meta.slot[IAP_SLOT_B].state == IAP_IMAGE_READY &&
          meta.slot[IAP_SLOT_A].state == IAP_IMAGE_CONFIRMED,
          "copy interruption leaves source READY and fallback confirmed");
    CHECK(sim_boot_prepare(SIM_NO_POWER_LOSS, &started) == SIM_BOOT_STARTED &&
          started == IAP_SLOT_B &&
          memcmp(sim_run_bytes(), new_image, new_size) == 0,
          "next boot recopies intact B and starts pending image");

    CHECK(sim_boot_prepare(SIM_NO_POWER_LOSS, &started) == SIM_BOOT_STARTED &&
          started == IAP_SLOT_A &&
          memcmp(sim_run_bytes(), old_image, old_size) == 0,
          "reset before confirmation rolls back to A");
    meta = sim_meta_snapshot();
    CHECK(meta.slot[IAP_SLOT_B].state == IAP_IMAGE_BAD &&
          meta.slot[IAP_SLOT_A].state == IAP_IMAGE_CONFIRMED,
          "unconfirmed B marked BAD; A retained");
    puts("PASS copy_power_loss_and_unconfirmed_rollback");
    return 1;
}

int main(void)
{
    puts("STM32F407 UART IAP host simulation");
    puts("Evidence: [PC simulation], not STM32 hardware measurement\n");
    (void)test_crc_and_frame();
    (void)test_normal_and_duplicate();
    (void)test_bad_frame_and_order();
    (void)test_whole_image_crc_failure();
    (void)test_interrupted_download_preserves_old();
    (void)test_copy_power_loss_and_rollback();

    if (failures != 0) {
        printf("\nRESULT: FAIL (%d test groups failed)\n", failures);
        return 1;
    }
    puts("\nRESULT: PASS (6 groups)");
    puts("This does not prove UART timing, STM32 Flash, reset, VTOR/MSP, or board power-loss behavior.");
    return 0;
}
