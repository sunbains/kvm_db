/*
 * uringblk_driver.h - io_uring-first Linux Block Driver Header
 *
 * This header defines the interface for the io_uring-first block driver
 * as specified in the Linux Block Driver with io_uring-first User Interface spec.
 */

#ifndef URINGBLK_DRIVER_H
#define URINGBLK_DRIVER_H

#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/version.h>
#include <linux/blk-mq.h>
#include <linux/blkdev.h>
#include <linux/io_uring.h>

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
    __u16 abi_major;
    __u16 abi_minor;
    __u16 opcode;           /* enum uringblk_ucmd */
    __u16 flags;            /* reserved */
    __u32 payload_len;
    /* payload follows */
} __packed;

/* IDENTIFY command response */
struct uringblk_identify {
    __u8  model[40];
    __u8  firmware[16];
    __u32 logical_block_size;
    __u32 physical_block_size;
    __u64 capacity_sectors;
    __u64 features_bitmap;
    __u32 queue_count;
    __u32 queue_depth;
    __u32 max_segments;
    __u32 max_segment_size;
    __u32 dma_alignment;
    __u32 io_min;
    __u32 io_opt;
    __u32 discard_granularity;
    __u64 discard_max_bytes;
} __packed;

/* GET_LIMITS response */
struct uringblk_limits {
    __u32 max_hw_sectors_kb;
    __u32 max_sectors_kb;
    __u32 nr_hw_queues;
    __u32 queue_depth;
    __u32 max_segments;
    __u32 max_segment_size;
    __u32 dma_alignment;
    __u32 io_min;
    __u32 io_opt;
    __u32 discard_granularity;
    __u64 discard_max_bytes;
} __packed;

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
    __u64 capacity_sectors;
    __u32 logical_block_size;
    __u32 physical_block_size;
    __u16 cylinders;
    __u8  heads;
    __u8  sectors_per_track;
} __packed;

/* GET_STATS response */
struct uringblk_stats {
    __u64 read_ops;
    __u64 write_ops;
    __u64 flush_ops;
    __u64 discard_ops;
    __u64 read_sectors;
    __u64 write_sectors;
    __u64 read_bytes;
    __u64 write_bytes;
    __u64 queue_full_events;
    __u64 media_errors;
    __u64 retries;
    __u32 p50_read_latency_us;
    __u32 p99_read_latency_us;
    __u32 p50_write_latency_us;
    __u32 p99_write_latency_us;
} __packed;

#ifdef __KERNEL__

/* Storage backend types */
enum uringblk_backend_type {
    URINGBLK_BACKEND_VIRTUAL = 0,  /* In-memory virtual storage */
    URINGBLK_BACKEND_DEVICE = 1,   /* Real block device */
};

/* Forward declaration */
struct uringblk_backend;

/* Storage backend interface */
struct uringblk_backend_ops {
    int (*init)(struct uringblk_backend *backend, const char *device_path, size_t capacity);
    void (*cleanup)(struct uringblk_backend *backend);
    int (*read)(struct uringblk_backend *backend, loff_t pos, void *buf, size_t len);
    int (*write)(struct uringblk_backend *backend, loff_t pos, const void *buf, size_t len);
    int (*flush)(struct uringblk_backend *backend);
    int (*discard)(struct uringblk_backend *backend, loff_t pos, size_t len);
};

struct uringblk_backend {
    enum uringblk_backend_type type;
    const struct uringblk_backend_ops *ops;
    void *private_data;
    size_t capacity;
    struct mutex io_mutex;
};

/* Driver configuration */
struct uringblk_config {
    unsigned int nr_hw_queues;
    unsigned int queue_depth;
    bool enable_poll;
    bool enable_discard;
    bool write_cache;
    bool zoned_mode;
    enum uringblk_backend_type backend_type;
    char backend_device[256];
};

/* Per-device structure */
struct uringblk_device {
    struct gendisk *disk;
    struct blk_mq_tag_set tag_set;
    struct uringblk_config config;
    struct uringblk_stats stats;
    spinlock_t stats_lock;
    
    /* Storage backend */
    struct uringblk_backend backend;
    
    /* Features */
    u64 features;
    
    /* Device identification */
    char model[40];
    char firmware[16];
    
    struct mutex admin_mutex;
    int major;
    int minor;
};

/* Per-queue context */
struct uringblk_queue {
    struct uringblk_device *dev;
    struct blk_mq_hw_ctx *hctx;
    unsigned int queue_num;
    spinlock_t lock;
};

/* Function declarations */
int uringblk_init_device(struct uringblk_device *dev, int minor);
void uringblk_cleanup_device(struct uringblk_device *dev);
int uringblk_handle_uring_cmd(struct io_uring_cmd *cmd, unsigned int issue_flags);
int uringblk_poll(struct blk_mq_hw_ctx *hctx, struct io_uring_cmd *ioucmd);

/* Block device operations */
blk_status_t uringblk_queue_rq(struct blk_mq_hw_ctx *hctx,
                               const struct blk_mq_queue_data *bd);
int uringblk_init_hctx(struct blk_mq_hw_ctx *hctx, void *data, 
                       unsigned int hctx_idx);
void uringblk_exit_hctx(struct blk_mq_hw_ctx *hctx, unsigned int hctx_idx);
int uringblk_poll_fn(struct blk_mq_hw_ctx *hctx, struct io_comp_batch *iob);

/* Block device file operations */
int uringblk_open(struct gendisk *disk, blk_mode_t mode);
void uringblk_release(struct gendisk *disk);
int uringblk_getgeo(struct block_device *bdev, struct hd_geometry *geo);

/* URING_CMD handlers */
int uringblk_cmd_identify(struct uringblk_device *dev, void __user *argp, u32 len);
int uringblk_cmd_get_limits(struct uringblk_device *dev, void __user *argp, u32 len);
int uringblk_cmd_get_features(struct uringblk_device *dev, void __user *argp, u32 len);
int uringblk_cmd_set_features(struct uringblk_device *dev, void __user *argp, u32 len);
int uringblk_cmd_get_geometry(struct uringblk_device *dev, void __user *argp, u32 len);
int uringblk_cmd_get_stats(struct uringblk_device *dev, void __user *argp, u32 len);

/* Module parameters */
extern unsigned int uringblk_nr_hw_queues;
extern unsigned int uringblk_queue_depth;
extern bool uringblk_enable_poll;
extern bool uringblk_enable_discard;
extern bool uringblk_write_cache;
extern unsigned int uringblk_logical_block_size;
extern unsigned int uringblk_capacity_mb;
extern int uringblk_backend_type;
extern char *uringblk_backend_device;
extern bool uringblk_auto_detect_size;
extern int uringblk_max_devices;
extern char *uringblk_devices;

/* Sysfs functions */
int uringblk_sysfs_create(struct gendisk *disk);
void uringblk_sysfs_remove(struct gendisk *disk);

/* ABI version */
#define URINGBLK_ABI_MAJOR  1
#define URINGBLK_ABI_MINOR  0

#endif /* __KERNEL__ */

#endif /* URINGBLK_DRIVER_H */
