/*
 * uringblk_uapi.h - io_uring-first Linux Block Driver Userspace API
 *
 * This header defines the userspace interface for the io_uring-first block driver.
 * It contains only the structures and constants needed by userspace applications.
 */

#ifndef URINGBLK_UAPI_H
#define URINGBLK_UAPI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Device constants */
#define URINGBLK_DEVICE_NAME    "uringblk"
#define URINGBLK_MINORS         16
#define URINGBLK_DEFAULT_QUEUE_DEPTH    1024
#define URINGBLK_DEFAULT_NR_HW_QUEUES   4
#define URINGBLK_MAX_SEGMENTS   128
#define URINGBLK_MAX_SEGMENT_SIZE   (1 << 20)  /* 1MB */

/* Driver version and info */
#define URINGBLK_DRIVER_VERSION "1.0.0"
#define URINGBLK_DRIVER_AUTHOR  "KVM Database Project"
#define URINGBLK_DRIVER_DESC    "io_uring-first Linux Block Driver"

/* URING_CMD ABI */
#define URINGBLK_URING_CMD_IO   'U'

/* URING_CMD opcodes */
enum uringblk_ucmd {
    URINGBLK_UCMD_IDENTIFY      = 0x01,
    URINGBLK_UCMD_GET_LIMITS    = 0x02,
    URINGBLK_UCMD_GET_FEATURES  = 0x03,
    URINGBLK_UCMD_SET_FEATURES  = 0x04,
    URINGBLK_UCMD_GET_GEOMETRY  = 0x05,
    URINGBLK_UCMD_GET_STATS     = 0x06,
    URINGBLK_UCMD_ZONE_MGMT     = 0x10,
    URINGBLK_UCMD_FIRMWARE_OP   = 0x20,
};

/* URING_CMD header structure */
struct uringblk_ucmd_hdr {
    uint16_t abi_major;
    uint16_t abi_minor;
    uint16_t opcode;           /* enum uringblk_ucmd */
    uint16_t flags;            /* reserved */
    uint32_t payload_len;
    /* payload follows */
} __attribute__((packed));

/* IDENTIFY command response */
struct uringblk_identify {
    uint8_t  model[40];
    uint8_t  firmware[16];
    uint32_t logical_block_size;
    uint32_t physical_block_size;
    uint64_t capacity_sectors;
    uint64_t features_bitmap;
    uint32_t queue_count;
    uint32_t queue_depth;
    uint32_t max_segments;
    uint32_t max_segment_size;
    uint32_t dma_alignment;
    uint32_t io_min;
    uint32_t io_opt;
    uint32_t discard_granularity;
    uint64_t discard_max_bytes;
} __attribute__((packed));

/* GET_LIMITS response */
struct uringblk_limits {
    uint32_t max_hw_sectors_kb;
    uint32_t max_sectors_kb;
    uint32_t nr_hw_queues;
    uint32_t queue_depth;
    uint32_t max_segments;
    uint32_t max_segment_size;
    uint32_t dma_alignment;
    uint32_t io_min;
    uint32_t io_opt;
    uint32_t discard_granularity;
    uint64_t discard_max_bytes;
} __attribute__((packed));

/* Feature flags */
#define URINGBLK_FEAT_WRITE_CACHE   (1ULL << 0)
#define URINGBLK_FEAT_FUA           (1ULL << 1)
#define URINGBLK_FEAT_FLUSH         (1ULL << 2)
#define URINGBLK_FEAT_DISCARD       (1ULL << 3)
#define URINGBLK_FEAT_WRITE_ZEROES  (1ULL << 4)
#define URINGBLK_FEAT_ZONED         (1ULL << 5)
#define URINGBLK_FEAT_POLLING       (1ULL << 6)

/* GET_GEOMETRY response */
struct uringblk_geometry {
    uint64_t capacity_sectors;
    uint32_t logical_block_size;
    uint32_t physical_block_size;
    uint16_t cylinders;
    uint8_t  heads;
    uint8_t  sectors_per_track;
} __attribute__((packed));

/* GET_STATS response */
struct uringblk_stats {
    uint64_t read_ops;
    uint64_t write_ops;
    uint64_t flush_ops;
    uint64_t discard_ops;
    uint64_t read_sectors;
    uint64_t write_sectors;
    uint64_t read_bytes;
    uint64_t write_bytes;
    uint64_t queue_full_events;
    uint64_t media_errors;
    uint64_t retries;
    uint32_t p50_read_latency_us;
    uint32_t p99_read_latency_us;
    uint32_t p50_write_latency_us;
    uint32_t p99_write_latency_us;
} __attribute__((packed));

/* ABI version */
#define URINGBLK_ABI_MAJOR  1
#define URINGBLK_ABI_MINOR  0

#ifdef __cplusplus
}
#endif

#endif /* URINGBLK_UAPI_H */