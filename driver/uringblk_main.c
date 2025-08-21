/*
 * uringblk_main.c - io_uring-first Linux Block Driver Implementation
 *
 * This kernel module implements a high-performance block device driver
 * with io_uring as the primary interface, following the specification
 * for Linux Block Driver with io_uring-first User Interface.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/bio.h>
#include <linux/blk-mq.h>
#include <linux/blkdev.h>
#include <linux/genhd.h>
#include <linux/hdreg.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/io_uring.h>
#include <linux/version.h>

#include "uringblk_driver.h"

/* Module metadata */
MODULE_LICENSE("GPL");
MODULE_AUTHOR(URINGBLK_DRIVER_AUTHOR);
MODULE_DESCRIPTION(URINGBLK_DRIVER_DESC);
MODULE_VERSION(URINGBLK_DRIVER_VERSION);

/* Module parameters */
static unsigned int nr_hw_queues = URINGBLK_DEFAULT_NR_HW_QUEUES;
module_param(nr_hw_queues, uint, 0644);
MODULE_PARM_DESC(nr_hw_queues, "Number of hardware queues (default: 4)");

static unsigned int queue_depth = URINGBLK_DEFAULT_QUEUE_DEPTH;
module_param(queue_depth, uint, 0644);
MODULE_PARM_DESC(queue_depth, "Queue depth per hardware queue (default: 1024)");

static bool enable_poll = true;
module_param(enable_poll, bool, 0644);
MODULE_PARM_DESC(enable_poll, "Enable polling support (default: true)");

static bool enable_discard = true;
module_param(enable_discard, bool, 0644);
MODULE_PARM_DESC(enable_discard, "Enable discard/TRIM support (default: true)");

static bool write_cache = true;
module_param(write_cache, bool, 0644);
MODULE_PARM_DESC(write_cache, "Enable write cache (default: true)");

static unsigned int logical_block_size = 512;
module_param(logical_block_size, uint, 0444);
MODULE_PARM_DESC(logical_block_size, "Logical block size in bytes (default: 512)");

static unsigned int capacity_mb = 1024;
module_param(capacity_mb, uint, 0644);
MODULE_PARM_DESC(capacity_mb, "Device capacity in MB (default: 1024)");

/* Global state */
static int uringblk_major = 0;
static struct uringblk_device *uringblk_dev = NULL;

/* Forward declarations */
static int uringblk_init_device(struct uringblk_device *dev, int minor);
static void uringblk_cleanup_device(struct uringblk_device *dev);

/*
 * Block device request queue operations
 */
static blk_status_t uringblk_queue_rq(struct blk_mq_hw_ctx *hctx,
                                      const struct blk_mq_queue_data *bd)
{
    struct request *rq = bd->rq;
    struct uringblk_queue *uq = hctx->driver_data;
    struct uringblk_device *dev = uq->dev;
    struct bio_vec bvec;
    struct req_iterator iter;
    loff_t pos = blk_rq_pos(rq) * logical_block_size;
    loff_t dev_size = dev->capacity;
    unsigned long flags;
    blk_status_t status = BLK_STS_OK;

    blk_mq_start_request(rq);

    /* Bounds checking */
    if (pos >= dev_size || pos + blk_rq_bytes(rq) > dev_size) {
        blk_mq_end_request(rq, BLK_STS_IOERR);
        return BLK_STS_OK;
    }

    /* Update statistics */
    spin_lock_irqsave(&dev->stats_lock, flags);
    switch (req_op(rq)) {
    case REQ_OP_READ:
        dev->stats.read_ops++;
        dev->stats.read_sectors += blk_rq_sectors(rq);
        dev->stats.read_bytes += blk_rq_bytes(rq);
        break;
    case REQ_OP_WRITE:
        dev->stats.write_ops++;
        dev->stats.write_sectors += blk_rq_sectors(rq);
        dev->stats.write_bytes += blk_rq_bytes(rq);
        break;
    case REQ_OP_FLUSH:
        dev->stats.flush_ops++;
        break;
    case REQ_OP_DISCARD:
        dev->stats.discard_ops++;
        break;
    default:
        status = BLK_STS_NOTSUPP;
        break;
    }
    spin_unlock_irqrestore(&dev->stats_lock, flags);

    if (status != BLK_STS_OK) {
        blk_mq_end_request(rq, status);
        return BLK_STS_OK;
    }

    /* Process each bio segment */
    rq_for_each_segment(bvec, rq, iter) {
        void *buffer = page_address(bvec.bv_page) + bvec.bv_offset;
        size_t len = bvec.bv_len;

        if (pos + len > dev_size) {
            len = dev_size - pos;
        }

        switch (req_op(rq)) {
        case REQ_OP_READ:
            if (dev->data && pos + len <= dev->capacity) {
                memcpy(buffer, dev->data + pos, len);
            } else {
                memset(buffer, 0, len);
            }
            break;
        case REQ_OP_WRITE:
            if (dev->data && pos + len <= dev->capacity) {
                memcpy(dev->data + pos, buffer, len);
            }
            break;
        case REQ_OP_FLUSH:
            /* Simulate flush - no-op for in-memory device */
            break;
        case REQ_OP_DISCARD:
            if (dev->data && pos + len <= dev->capacity) {
                memset(dev->data + pos, 0, len);
            }
            break;
        }

        pos += len;
    }

    blk_mq_end_request(rq, BLK_STS_OK);
    return BLK_STS_OK;
}

static int uringblk_init_hctx(struct blk_mq_hw_ctx *hctx, void *data,
                              unsigned int hctx_idx)
{
    struct uringblk_device *dev = data;
    struct uringblk_queue *uq;

    uq = kzalloc(sizeof(*uq), GFP_KERNEL);
    if (!uq)
        return -ENOMEM;

    uq->dev = dev;
    uq->hctx = hctx;
    uq->queue_num = hctx_idx;
    spin_lock_init(&uq->lock);

    hctx->driver_data = uq;
    return 0;
}

static void uringblk_exit_hctx(struct blk_mq_hw_ctx *hctx, unsigned int hctx_idx)
{
    kfree(hctx->driver_data);
    hctx->driver_data = NULL;
}

static int uringblk_poll_fn(struct blk_mq_hw_ctx *hctx, struct io_uring_cmd *ioucmd)
{
    /* For our in-memory device, all operations complete immediately */
    return 0;
}

static const struct blk_mq_ops uringblk_mq_ops = {
    .queue_rq = uringblk_queue_rq,
    .init_hctx = uringblk_init_hctx,
    .exit_hctx = uringblk_exit_hctx,
    .poll = uringblk_poll_fn,
};

/*
 * Block device file operations
 */
static int uringblk_open(struct block_device *bdev, fmode_t mode)
{
    struct uringblk_device *dev = bdev->bd_disk->private_data;
    
    if (!dev)
        return -ENODEV;

    return 0;
}

static void uringblk_release(struct gendisk *disk, fmode_t mode)
{
    /* Nothing to do for release */
}

static int uringblk_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
    struct uringblk_device *dev = bdev->bd_disk->private_data;
    u64 sectors = dev->capacity / logical_block_size;

    /* Create fake geometry */
    geo->cylinders = sectors / (16 * 63);
    geo->heads = 16;
    geo->sectors = 63;
    geo->start = 0;

    return 0;
}

/*
 * URING_CMD admin interface
 */
static int uringblk_handle_uring_cmd(struct io_uring_cmd *cmd)
{
    struct block_device *bdev = cmd->file->f_mapping->host->i_bdev;
    struct uringblk_device *dev = bdev->bd_disk->private_data;
    struct uringblk_ucmd_hdr hdr;
    void __user *argp = u64_to_user_ptr(cmd->cmd);
    int ret;

    if (!dev)
        return -ENODEV;

    /* Copy command header */
    if (copy_from_user(&hdr, argp, sizeof(hdr)))
        return -EFAULT;

    /* Check ABI version */
    if (hdr.abi_major != URINGBLK_ABI_MAJOR)
        return -EINVAL;

    /* Validate payload length */
    if (hdr.payload_len > PAGE_SIZE)
        return -EINVAL;

    mutex_lock(&dev->admin_mutex);

    switch (hdr.opcode) {
    case URINGBLK_UCMD_IDENTIFY:
        ret = uringblk_cmd_identify(dev, 
                                   (char __user *)argp + sizeof(hdr), 
                                   hdr.payload_len);
        break;
    case URINGBLK_UCMD_GET_LIMITS:
        ret = uringblk_cmd_get_limits(dev,
                                     (char __user *)argp + sizeof(hdr),
                                     hdr.payload_len);
        break;
    case URINGBLK_UCMD_GET_FEATURES:
        ret = uringblk_cmd_get_features(dev,
                                       (char __user *)argp + sizeof(hdr),
                                       hdr.payload_len);
        break;
    case URINGBLK_UCMD_GET_GEOMETRY:
        ret = uringblk_cmd_get_geometry(dev,
                                       (char __user *)argp + sizeof(hdr),
                                       hdr.payload_len);
        break;
    case URINGBLK_UCMD_GET_STATS:
        ret = uringblk_cmd_get_stats(dev,
                                    (char __user *)argp + sizeof(hdr),
                                    hdr.payload_len);
        break;
    default:
        ret = -EOPNOTSUPP;
        break;
    }

    mutex_unlock(&dev->admin_mutex);

    if (ret >= 0) {
        io_uring_cmd_done(cmd, ret);
        return 0;
    } else {
        io_uring_cmd_done(cmd, ret);
        return ret;
    }
}

static const struct block_device_operations uringblk_fops = {
    .owner = THIS_MODULE,
    .open = uringblk_open,
    .release = uringblk_release,
    .getgeo = uringblk_getgeo,
    .uring_cmd = uringblk_handle_uring_cmd,
};

/*
 * URING_CMD command handlers
 */
int uringblk_cmd_identify(struct uringblk_device *dev, void __user *argp, u32 len)
{
    struct uringblk_identify id;

    if (len < sizeof(id))
        return -EINVAL;

    memset(&id, 0, sizeof(id));
    strncpy(id.model, dev->model, sizeof(id.model) - 1);
    strncpy(id.firmware, dev->firmware, sizeof(id.firmware) - 1);
    id.logical_block_size = logical_block_size;
    id.physical_block_size = logical_block_size;
    id.capacity_sectors = dev->capacity / logical_block_size;
    id.features_bitmap = dev->features;
    id.queue_count = dev->config.nr_hw_queues;
    id.queue_depth = dev->config.queue_depth;
    id.max_segments = URINGBLK_MAX_SEGMENTS;
    id.max_segment_size = URINGBLK_MAX_SEGMENT_SIZE;
    id.dma_alignment = 4096;
    id.io_min = logical_block_size;
    id.io_opt = 64 * 1024;

    if (copy_to_user(argp, &id, sizeof(id)))
        return -EFAULT;

    return sizeof(id);
}

int uringblk_cmd_get_limits(struct uringblk_device *dev, void __user *argp, u32 len)
{
    struct uringblk_limits limits;

    if (len < sizeof(limits))
        return -EINVAL;

    memset(&limits, 0, sizeof(limits));
    limits.max_hw_sectors_kb = 4096;
    limits.max_sectors_kb = 4096;
    limits.nr_hw_queues = dev->config.nr_hw_queues;
    limits.queue_depth = dev->config.queue_depth;
    limits.max_segments = URINGBLK_MAX_SEGMENTS;
    limits.max_segment_size = URINGBLK_MAX_SEGMENT_SIZE;
    limits.dma_alignment = 4096;
    limits.io_min = logical_block_size;
    limits.io_opt = 64 * 1024;

    if (copy_to_user(argp, &limits, sizeof(limits)))
        return -EFAULT;

    return sizeof(limits);
}

int uringblk_cmd_get_features(struct uringblk_device *dev, void __user *argp, u32 len)
{
    if (len < sizeof(dev->features))
        return -EINVAL;

    if (copy_to_user(argp, &dev->features, sizeof(dev->features)))
        return -EFAULT;

    return sizeof(dev->features);
}

int uringblk_cmd_get_geometry(struct uringblk_device *dev, void __user *argp, u32 len)
{
    struct uringblk_geometry geo;

    if (len < sizeof(geo))
        return -EINVAL;

    memset(&geo, 0, sizeof(geo));
    geo.capacity_sectors = dev->capacity / logical_block_size;
    geo.logical_block_size = logical_block_size;
    geo.physical_block_size = logical_block_size;
    geo.cylinders = geo.capacity_sectors / (16 * 63);
    geo.heads = 16;
    geo.sectors_per_track = 63;

    if (copy_to_user(argp, &geo, sizeof(geo)))
        return -EFAULT;

    return sizeof(geo);
}

int uringblk_cmd_get_stats(struct uringblk_device *dev, void __user *argp, u32 len)
{
    struct uringblk_stats stats;
    unsigned long flags;

    if (len < sizeof(stats))
        return -EINVAL;

    spin_lock_irqsave(&dev->stats_lock, flags);
    memcpy(&stats, &dev->stats, sizeof(stats));
    spin_unlock_irqrestore(&dev->stats_lock, flags);

    if (copy_to_user(argp, &stats, sizeof(stats)))
        return -EFAULT;

    return sizeof(stats);
}

/*
 * Device initialization and cleanup
 */
static int uringblk_init_device(struct uringblk_device *dev, int minor)
{
    int ret;

    /* Initialize device structure */
    memset(dev, 0, sizeof(*dev));
    dev->minor = minor;
    spin_lock_init(&dev->stats_lock);
    mutex_init(&dev->admin_mutex);

    /* Set up configuration */
    dev->config.nr_hw_queues = nr_hw_queues;
    dev->config.queue_depth = queue_depth;
    dev->config.enable_poll = enable_poll;
    dev->config.enable_discard = enable_discard;
    dev->config.write_cache = write_cache;

    /* Set up features */
    dev->features = URINGBLK_FEAT_FLUSH;
    if (dev->config.write_cache)
        dev->features |= URINGBLK_FEAT_WRITE_CACHE;
    if (dev->config.enable_discard)
        dev->features |= URINGBLK_FEAT_DISCARD | URINGBLK_FEAT_WRITE_ZEROES;
    if (dev->config.enable_poll)
        dev->features |= URINGBLK_FEAT_POLLING;
    
    dev->features |= URINGBLK_FEAT_FUA;

    /* Set device info */
    snprintf(dev->model, sizeof(dev->model), "uringblk Virtual Device");
    snprintf(dev->firmware, sizeof(dev->firmware), "v%s", URINGBLK_DRIVER_VERSION);

    /* Allocate virtual storage */
    dev->capacity = (size_t)capacity_mb * 1024 * 1024;
    dev->data = vzalloc(dev->capacity);
    if (!dev->data) {
        pr_err("uringblk: failed to allocate device storage\n");
        return -ENOMEM;
    }

    /* Initialize tag set */
    memset(&dev->tag_set, 0, sizeof(dev->tag_set));
    dev->tag_set.ops = &uringblk_mq_ops;
    dev->tag_set.nr_hw_queues = dev->config.nr_hw_queues;
    dev->tag_set.queue_depth = dev->config.queue_depth;
    dev->tag_set.numa_node = NUMA_NO_NODE;
    dev->tag_set.cmd_size = 0;
    dev->tag_set.flags = BLK_MQ_F_SHOULD_MERGE;
    dev->tag_set.driver_data = dev;

    ret = blk_mq_alloc_tag_set(&dev->tag_set);
    if (ret) {
        pr_err("uringblk: failed to allocate tag set: %d\n", ret);
        goto err_free_data;
    }

    /* Allocate disk */
    dev->disk = blk_mq_alloc_disk(&dev->tag_set, dev);
    if (IS_ERR(dev->disk)) {
        ret = PTR_ERR(dev->disk);
        pr_err("uringblk: failed to allocate disk: %d\n", ret);
        goto err_free_tag_set;
    }

    dev->disk->major = uringblk_major;
    dev->disk->first_minor = minor;
    dev->disk->minors = 1;
    dev->disk->fops = &uringblk_fops;
    dev->disk->private_data = dev;
    snprintf(dev->disk->disk_name, sizeof(dev->disk->disk_name),
             "%s%d", URINGBLK_DEVICE_NAME, minor);

    /* Set queue limits */
    blk_queue_logical_block_size(dev->disk->queue, logical_block_size);
    blk_queue_physical_block_size(dev->disk->queue, logical_block_size);
    blk_queue_max_hw_sectors(dev->disk->queue, 4096 * 2); /* 4MB */
    blk_queue_max_segments(dev->disk->queue, URINGBLK_MAX_SEGMENTS);
    blk_queue_max_segment_size(dev->disk->queue, URINGBLK_MAX_SEGMENT_SIZE);

    if (dev->config.enable_discard) {
        blk_queue_flag_set(QUEUE_FLAG_DISCARD, dev->disk->queue);
        blk_queue_max_discard_sectors(dev->disk->queue, UINT_MAX);
        blk_queue_max_write_zeroes_sectors(dev->disk->queue, UINT_MAX);
    }

    if (dev->config.write_cache) {
        blk_queue_flag_set(QUEUE_FLAG_WC, dev->disk->queue);
    }

    /* Set capacity */
    set_capacity(dev->disk, dev->capacity / logical_block_size);

    /* Add disk */
    ret = add_disk(dev->disk);
    if (ret) {
        pr_err("uringblk: failed to add disk: %d\n", ret);
        goto err_put_disk;
    }

    pr_info("uringblk: created device %s (%zu MB)\n", 
            dev->disk->disk_name, dev->capacity / (1024 * 1024));

    return 0;

err_put_disk:
    put_disk(dev->disk);
err_free_tag_set:
    blk_mq_free_tag_set(&dev->tag_set);
err_free_data:
    vfree(dev->data);
    return ret;
}

static void uringblk_cleanup_device(struct uringblk_device *dev)
{
    if (dev->disk) {
        del_gendisk(dev->disk);
        put_disk(dev->disk);
    }
    
    blk_mq_free_tag_set(&dev->tag_set);
    
    if (dev->data) {
        vfree(dev->data);
    }
}

/*
 * Module initialization and cleanup
 */
static int __init uringblk_init(void)
{
    int ret;

    pr_info("uringblk: Loading io_uring-first block driver v%s\n", 
            URINGBLK_DRIVER_VERSION);

    /* Register block device major number */
    uringblk_major = register_blkdev(0, URINGBLK_DEVICE_NAME);
    if (uringblk_major < 0) {
        pr_err("uringblk: failed to register block device: %d\n", uringblk_major);
        return uringblk_major;
    }

    /* Allocate device structure */
    uringblk_dev = kzalloc(sizeof(*uringblk_dev), GFP_KERNEL);
    if (!uringblk_dev) {
        ret = -ENOMEM;
        goto err_unregister;
    }

    /* Initialize device */
    ret = uringblk_init_device(uringblk_dev, 0);
    if (ret) {
        pr_err("uringblk: failed to initialize device: %d\n", ret);
        goto err_free_dev;
    }

    pr_info("uringblk: driver loaded successfully (major=%d)\n", uringblk_major);
    return 0;

err_free_dev:
    kfree(uringblk_dev);
    uringblk_dev = NULL;
err_unregister:
    unregister_blkdev(uringblk_major, URINGBLK_DEVICE_NAME);
    return ret;
}

static void __exit uringblk_exit(void)
{
    pr_info("uringblk: Unloading driver\n");

    if (uringblk_dev) {
        uringblk_cleanup_device(uringblk_dev);
        kfree(uringblk_dev);
        uringblk_dev = NULL;
    }

    unregister_blkdev(uringblk_major, URINGBLK_DEVICE_NAME);

    pr_info("uringblk: driver unloaded\n");
}

module_init(uringblk_init);
module_exit(uringblk_exit);