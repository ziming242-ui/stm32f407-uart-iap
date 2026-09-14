#ifndef IAP_CONFIG_H
#define IAP_CONFIG_H

#include <stdint.h>

/* STM32F407ZGT6 1 MiB internal Flash partition (sector aligned). */
#define IAP_BOOT_BASE          0x08000000u
#define IAP_BOOT_SIZE          0x00020000u
#define IAP_PARAM_BASE         0x08020000u
#define IAP_PARAM_SIZE         0x00020000u
#define IAP_RUN_BASE           0x08040000u
#define IAP_RUN_SIZE           0x00040000u
#define IAP_SLOT_A_BASE        0x08080000u
#define IAP_SLOT_A_SIZE        0x00040000u
#define IAP_SLOT_B_BASE        0x080C0000u
#define IAP_SLOT_B_SIZE        0x00040000u
#define IAP_FLASH_END          0x08100000u

#define IAP_PROTOCOL_VERSION   1u
#define IAP_UART_BAUDRATE      115200u
#define IAP_BOOT_WAIT_MS       3000u
#define IAP_FRAME_TIMEOUT_MS   1500u
#define IAP_DATA_BYTES         256u
#define IAP_PENDING_BOOT_LIMIT 2u

#define IAP_FRAME_OVERHEAD     10u
#define IAP_FRAME_DATA_MAX     512u
#define IAP_FRAME_WIRE_MAX     (IAP_FRAME_OVERHEAD + IAP_FRAME_DATA_MAX)

#define IAP_META_MAGIC         0x4941504Du /* "IAPM" */
#define IAP_META_SCHEMA        1u
#define IAP_META_RECORD_SIZE   128u
#define IAP_META_COMMIT_MAGIC  0x434F4D54u /* "COMT" */
#define IAP_RUN_VALID_MAGIC    0x52554E31u /* "RUN1" */

typedef enum {
    IAP_SLOT_A = 0,
    IAP_SLOT_B = 1,
    IAP_SLOT_NONE = 0xFE,
    IAP_SLOT_AUTO = 0xFF
} IapSlot;

typedef enum {
    IAP_IMAGE_EMPTY = 0,
    IAP_IMAGE_DOWNLOADING,
    IAP_IMAGE_READY,
    IAP_IMAGE_PENDING,
    IAP_IMAGE_CONFIRMED,
    IAP_IMAGE_BAD
} IapImageState;

typedef enum {
    IAP_CMD_HELLO  = 0x0001,
    IAP_CMD_HOLD   = 0x0002,
    IAP_CMD_START  = 0x0010,
    IAP_CMD_DATA   = 0x0011,
    IAP_CMD_END    = 0x0012,
    IAP_CMD_ABORT  = 0x0013,
    IAP_CMD_STATUS = 0x0020,
    IAP_CMD_BOOT   = 0x0030,
    IAP_CMD_ACK    = 0x8000,
    IAP_CMD_NACK   = 0x8001
} IapCommand;

typedef enum {
    IAP_ERR_OK = 0,
    IAP_ERR_FRAME_LENGTH,
    IAP_ERR_FRAME_CRC,
    IAP_ERR_RESERVED,
    IAP_ERR_COMMAND,
    IAP_ERR_STATE,
    IAP_ERR_TARGET,
    IAP_ERR_IMAGE_SIZE,
    IAP_ERR_SEQUENCE,
    IAP_ERR_OFFSET,
    IAP_ERR_IMAGE_CRC,
    IAP_ERR_NO_IMAGE,
    IAP_ERR_VECTOR,
    IAP_ERR_FLASH,
    IAP_ERR_META_FULL,
    IAP_ERR_TIMEOUT
} IapError;

#endif
