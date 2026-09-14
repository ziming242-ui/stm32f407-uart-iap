#ifndef IAP_STORAGE_H
#define IAP_STORAGE_H

#include "iap_config.h"

#include <stdint.h>

typedef struct {
    uint32_t state;
    uint32_t version;
    uint32_t size;
    uint32_t crc32;
    uint32_t generation;
    uint32_t boot_attempts;
} IapImageMeta;

typedef struct {
    uint32_t valid;
    uint32_t source_slot;
    uint32_t version;
    uint32_t size;
    uint32_t crc32;
} IapRunMeta;

/* Exactly 128 bytes. commit_magic is programmed last as the transaction commit. */
typedef struct {
    uint32_t magic;
    uint32_t schema;
    uint32_t record_size;
    uint32_t record_generation;
    IapImageMeta slot[2];
    IapRunMeta run;
    uint32_t reserved[9];
    uint32_t record_crc32;
    uint32_t commit_magic;
} IapMetaRecord;

typedef char IapMetaRecord_must_be_128_bytes[
    (sizeof(IapMetaRecord) == IAP_META_RECORD_SIZE) ? 1 : -1];

typedef struct {
    uint32_t active;
    uint32_t target;
    uint32_t version;
    uint32_t expected_size;
    uint32_t expected_crc32;
    uint32_t received;
    uint32_t next_sequence;
} IapDownloadSession;

#endif
