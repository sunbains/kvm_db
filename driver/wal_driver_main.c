/*
 * wal_driver.c - Write-Ahead Log Device Driver Implementation
 *
 * This kernel module provides drivers for:
 * - /dev/rwal (character device, major=240, minor=0)
 * - /dev/wal  (block device, major=240, minor=1)
 *
 * Character device operations:
 * - Read: Returns "Hello from WAL"
 * - Write: Captures and prints written data
 *
 * Block device operations:
 * - Read: Returns "Hello from WAL" pattern
 * - Write: Captures and prints written data
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include "wal_driver.h"

/* Module metadata */
MODULE_LICENSE("GPL");
MODULE_AUTHOR(WAL_DRIVER_AUTHOR);
MODULE_DESCRIPTION(WAL_DRIVER_DESC);
MODULE_VERSION(WAL_DRIVER_VERSION);

/* Global device structures */
static struct {
    /* Character device */
    struct cdev char_cdev;
    dev_t char_dev;
    struct class *char_class;
    struct device *char_device;

    /* Block device */
    struct gendisk *block_disk;
    struct request_queue *block_queue;
    spinlock_t block_lock;
    u8 *block_data;  /* Virtual block device storage */

    /* Statistics and state */
    struct wal_status status;
    struct mutex status_mutex;

    /* Proc filesystem */
    struct proc_dir_entry *proc_entry;
} wal_global;

/* Forward declarations */
static int wal_proc_show(struct seq_file *m, void *v);
static int wal_proc_open(struct inode *inode, struct file *file);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)
static void wal_block_submit_bio(struct bio *bio);
#endif

/* Character device file operations */
static const struct file_operations wal_char_fops = {
    .owner = THIS_MODULE,
    .open = wal_char_open,
    .release = wal_char_release,
    .read = wal_char_read,
    .write = wal_char_write,
    .unlocked_ioctl = wal_char_ioctl,
    .llseek = default_llseek,
};

/* Block device operations - Updated for newer kernels */
static const struct block_device_operations wal_block_ops = {
    .owner = THIS_MODULE,
    .open = wal_block_open,
    .release = wal_block_release,
    .getgeo = wal_block_getgeo,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)
    .submit_bio = wal_block_submit_bio,
#endif
};

/* Proc file operations */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
static const struct proc_ops wal_proc_ops = {
    .proc_open = wal_proc_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};
#else
static const struct file_operations wal_proc_ops = {
    .owner = THIS_MODULE,
    .open = wal_proc_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};
#endif

/*
 * Character Device Implementation
 */

int wal_char_open(struct inode *inode, struct file *file)
{
    pr_info("wal_driver: Character device /dev/rwal opened (pid: %d)\n", current->pid);

    mutex_lock(&wal_global.status_mutex);
    /* No specific open actions needed, just log */
    mutex_unlock(&wal_global.status_mutex);

    return 0;
}

int wal_char_release(struct inode *inode, struct file *file)
{
    pr_info("wal_driver: Character device /dev/rwal closed (pid: %d)\n", current->pid);
    return 0;
}

ssize_t wal_char_read(struct file *file, char __user *buffer, size_t count, loff_t *pos)
{
    size_t response_len = WAL_RESPONSE_LEN;
    size_t to_copy;

    mutex_lock(&wal_global.status_mutex);

    if (wal_global.status.current_mode != WAL_MODE_QUIET) {
        pr_info("wal_driver: Character read request - count=%zu, pos=%lld\n", count, *pos);
    }

    /* Handle EOF */
    if (*pos >= response_len) {
        mutex_unlock(&wal_global.status_mutex);
        return 0;
    }

    /* Calculate how much to copy */
    to_copy = min(count, response_len - (size_t)*pos);

    /* Copy data to user space */
    if (copy_to_user(buffer, WAL_RESPONSE_MSG + *pos, to_copy)) {
        pr_err("wal_driver: Failed to copy data to user space\n");
        mutex_unlock(&wal_global.status_mutex);
        return -EFAULT;
    }

    *pos += to_copy;
    wal_global.status.char_read_count++;
    wal_global.status.total_bytes_read += to_copy;

    if (wal_global.status.current_mode == WAL_MODE_DEBUG) {
        pr_info("wal_driver: Returned %zu bytes: \"%.*s\"\n",
                to_copy, (int)to_copy, WAL_RESPONSE_MSG + (*pos - to_copy));
    }

    mutex_unlock(&wal_global.status_mutex);
    return to_copy;
}

ssize_t wal_char_write(struct file *file, const char __user *buffer, size_t count, loff_t *pos)
{
    char *kernel_buffer;
    size_t i;
    size_t actual_count = min(count, (size_t)PAGE_SIZE); /* Limit to prevent excessive memory usage */

    mutex_lock(&wal_global.status_mutex);

    if (wal_global.status.current_mode != WAL_MODE_QUIET) {
        pr_info("wal_driver: Character write request - count=%zu, pos=%lld\n", count, *pos);
    }

    /* Allocate kernel buffer */
    kernel_buffer = kzalloc(actual_count + 1, GFP_KERNEL);
    if (!kernel_buffer) {
        pr_err("wal_driver: Failed to allocate kernel buffer\n");
        mutex_unlock(&wal_global.status_mutex);
        return -ENOMEM;
    }

    /* Copy data from user space */
    if (copy_from_user(kernel_buffer, buffer, actual_count)) {
        pr_err("wal_driver: Failed to copy data from user space\n");
        kfree(kernel_buffer);
        mutex_unlock(&wal_global.status_mutex);
        return -EFAULT;
    }

    /* Update statistics */
    wal_global.status.char_write_count++;
    wal_global.status.total_bytes_written += actual_count;

    /* Print captured data based on mode */
    if (wal_global.status.current_mode != WAL_MODE_QUIET) {
        pr_info("wal_driver: Captured character write (%zu bytes):\n", actual_count);

        /* Print as string if printable */
        bool is_printable = true;
        for (i = 0; i < actual_count; i++) {
            if (!isprint(kernel_buffer[i]) && !isspace(kernel_buffer[i])) {
                is_printable = false;
                break;
            }
        }

        if (is_printable) {
            pr_info("wal_driver: Text data: \"%s\"\n", kernel_buffer);
        }

        /* Print hex dump for debug mode or non-printable data */
        if (wal_global.status.current_mode == WAL_MODE_DEBUG || !is_printable) {
            pr_info("wal_driver: Hex dump: ");
            for (i = 0; i < actual_count && i < 64; i++) {
                printk(KERN_CONT "%02x ", (unsigned char)kernel_buffer[i]);
            }
            if (actual_count > 64) {
                printk(KERN_CONT "... (truncated)");
            }
            printk(KERN_CONT "\n");
        }
    }

    kfree(kernel_buffer);
    *pos += actual_count;
    mutex_unlock(&wal_global.status_mutex);

    return actual_count;
}

long wal_char_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int retval = 0;
    struct wal_status status_copy;
    enum wal_mode new_mode;

    /* Verify ioctl command */
    if (_IOC_TYPE(cmd) != WAL_IOC_MAGIC) return -ENOTTY;
    if (_IOC_NR(cmd) > WAL_IOC_MAXNR) return -ENOTTY;

    mutex_lock(&wal_global.status_mutex);

    switch (cmd) {
    case WAL_IOC_RESET:
        pr_info("wal_driver: Resetting statistics\n");
        memset(&wal_global.status, 0, sizeof(wal_global.status));
        wal_global.status.current_mode = WAL_MODE_NORMAL;
        break;

    case WAL_IOC_GET_STATUS:
        status_copy = wal_global.status;
        mutex_unlock(&wal_global.status_mutex);
        if (copy_to_user((void __user *)arg, &status_copy, sizeof(status_copy))) {
            return -EFAULT;
        }
        return 0;

    case WAL_IOC_SET_MODE:
        if (copy_from_user(&new_mode, (void __user *)arg, sizeof(new_mode))) {
            retval = -EFAULT;
            break;
        }
        if (new_mode < WAL_MODE_NORMAL || new_mode > WAL_MODE_QUIET) {
            retval = -EINVAL;
            break;
        }
        wal_global.status.current_mode = new_mode;
        pr_info("wal_driver: Mode changed to %d\n", new_mode);
        break;

    default:
        retval = -ENOTTY;
        break;
    }

    mutex_unlock(&wal_global.status_mutex);
    return retval;
}

/*
 * Block Device Implementation - Updated for newer kernels
 */

/* Updated function signatures for newer kernels */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 5, 0)
int wal_block_open(struct gendisk *disk, blk_mode_t mode)
#else
int wal_block_open(struct block_device *bdev, fmode_t mode)
#endif
{
    pr_info("wal_driver: Block device /dev/wal opened\n");
    return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 5, 0)
void wal_block_release(struct gendisk *disk)
#else
void wal_block_release(struct gendisk *disk, fmode_t mode)
#endif
{
    pr_info("wal_driver: Block device /dev/wal closed\n");
}

int wal_block_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
    /* Fake geometry for our virtual block device */
    geo->heads = 4;
    geo->sectors = 16;
    geo->cylinders = WAL_BLOCK_SECTORS / (geo->heads * geo->sectors);
    geo->start = 0;

    pr_info("wal_driver: Block device geometry requested\n");
    return 0;
}

/* Block device request handling for newer kernels */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)
static void wal_block_submit_bio(struct bio *bio)
{
    struct bio_vec bvec;
    struct bvec_iter iter;
    sector_t sector = bio->bi_iter.bi_sector;
    unsigned long offset = sector * WAL_BLOCK_SIZE;
    char *buffer;

    mutex_lock(&wal_global.status_mutex);

    if (wal_global.status.current_mode != WAL_MODE_QUIET) {
        pr_info("wal_driver: Block %s request - sector=%llu, size=%u\n",
                bio_data_dir(bio) ? "WRITE" : "READ",
                (unsigned long long)sector, bio->bi_iter.bi_size);
    }

    bio_for_each_segment(bvec, bio, iter) {
        buffer = kmap_atomic(bvec.bv_page) + bvec.bv_offset;

        if (bio_data_dir(bio)) {
            /* Write request - capture data */
            if (wal_global.status.current_mode != WAL_MODE_QUIET) {
                pr_info("wal_driver: Block write captured (%u bytes at offset %lu)\n",
                        bvec.bv_len, offset);

                if (wal_global.status.current_mode == WAL_MODE_DEBUG) {
                    print_hex_dump(KERN_INFO, "wal_driver: ", DUMP_PREFIX_OFFSET,
                                   16, 1, buffer, min_t(size_t, bvec.bv_len, 256), true);
                }
            }

            /* Copy to our virtual storage */
            if (offset + bvec.bv_len <= WAL_BLOCK_SECTORS * WAL_BLOCK_SIZE) {
                memcpy(wal_global.block_data + offset, buffer, bvec.bv_len);
            }

            wal_global.status.block_write_count++;
            wal_global.status.total_bytes_written += bvec.bv_len;
        } else {
            /* Read request - return "Hello from WAL" pattern */
            size_t response_len = WAL_RESPONSE_LEN;
            size_t pos = 0;

            if (wal_global.status.current_mode == WAL_MODE_DEBUG) {
                pr_info("wal_driver: Block read returning Hello pattern\n");
            }

            /* Fill buffer with repeating "Hello from WAL" pattern */
            while (pos < bvec.bv_len) {
                size_t chunk_size = min(bvec.bv_len - pos, response_len);
                memcpy(buffer + pos, WAL_RESPONSE_MSG, chunk_size);
                pos += chunk_size;
            }

            /* Also update our virtual storage */
            if (offset + bvec.bv_len <= WAL_BLOCK_SECTORS * WAL_BLOCK_SIZE) {
                memcpy(wal_global.block_data + offset, buffer, bvec.bv_len);
            }

            wal_global.status.block_read_count++;
            wal_global.status.total_bytes_read += bvec.bv_len;
        }

        kunmap_atomic(buffer);
        offset += bvec.bv_len;
    }

    mutex_unlock(&wal_global.status_mutex);
    bio_endio(bio);
}
#else
void wal_block_request(struct request_queue *q)
{
    struct request *req;
    unsigned long offset, nbytes;
    char *buffer;
    size_t i;

    while ((req = blk_fetch_request(q)) != NULL) {
        if (req->cmd_type != REQ_TYPE_FS) {
            pr_err("wal_driver: Non-filesystem request\n");
            __blk_end_request_all(req, -EIO);
            continue;
        }

        offset = blk_rq_pos(req) * WAL_BLOCK_SIZE;
        nbytes = blk_rq_bytes(req);
        buffer = bio_data(req->bio);

        mutex_lock(&wal_global.status_mutex);

        if (wal_global.status.current_mode != WAL_MODE_QUIET) {
            pr_info("wal_driver: Block %s request - offset=%lu, bytes=%lu\n",
                    rq_data_dir(req) ? "WRITE" : "READ", offset, nbytes);
        }

        if (rq_data_dir(req)) {
            /* Write request - capture data */
            if (wal_global.status.current_mode != WAL_MODE_QUIET) {
                pr_info("wal_driver: Block write captured (%lu bytes):\n", nbytes);

                if (wal_global.status.current_mode == WAL_MODE_DEBUG) {
                    /* Print captured data (limit to avoid log spam) */
                    for (i = 0; i < nbytes && i < 256; i++) {
                        if (i % 16 == 0) {
                            pr_info("wal_driver: %04lx: ", offset + i);
                        }
                        printk(KERN_CONT "%02x ", (unsigned char)buffer[i]);
                        if (i % 16 == 15) {
                            printk(KERN_CONT "\n");
                        }
                    }
                    if (i % 16 != 0) {
                        printk(KERN_CONT "\n");
                    }
                    if (nbytes > 256) {
                        pr_info("wal_driver: ... (truncated)\n");
                    }
                }
            }

            /* Copy to our virtual storage */
            if (offset + nbytes <= WAL_BLOCK_SECTORS * WAL_BLOCK_SIZE) {
                memcpy(wal_global.block_data + offset, buffer, nbytes);
            }

            wal_global.status.block_write_count++;
            wal_global.status.total_bytes_written += nbytes;
        } else {
            /* Read request - return "Hello from WAL" pattern */
            size_t response_len = WAL_RESPONSE_LEN;
            size_t pos = 0;

            if (wal_global.status.current_mode == WAL_MODE_DEBUG) {
                pr_info("wal_driver: Block read returning Hello pattern\n");
            }

            /* Fill buffer with repeating "Hello from WAL" pattern */
            while (pos < nbytes) {
                size_t chunk_size = min(nbytes - pos, response_len);
                memcpy(buffer + pos, WAL_RESPONSE_MSG, chunk_size);
                pos += chunk_size;
            }

            /* Also update our virtual storage */
            if (offset + nbytes <= WAL_BLOCK_SECTORS * WAL_BLOCK_SIZE) {
                memcpy(wal_global.block_data + offset, buffer, nbytes);
            }

            wal_global.status.block_read_count++;
            wal_global.status.total_bytes_read += nbytes;
        }

        mutex_unlock(&wal_global.status_mutex);
        __blk_end_request_all(req, 0);
    }
}
#endif

/*
 * Proc filesystem interface
 */

static int wal_proc_show(struct seq_file *m, void *v)
{
    struct wal_status status_copy;

    mutex_lock(&wal_global.status_mutex);
    status_copy = wal_global.status;
    mutex_unlock(&wal_global.status_mutex);

    seq_printf(m, "WAL Driver Statistics\n");
    seq_printf(m, "=====================\n");
    seq_printf(m, "Character device reads:  %u\n", status_copy.char_read_count);
    seq_printf(m, "Character device writes: %u\n", status_copy.char_write_count);
    seq_printf(m, "Block device reads:      %u\n", status_copy.block_read_count);
    seq_printf(m, "Block device writes:     %u\n", status_copy.block_write_count);
    seq_printf(m, "Total bytes read:        %u\n", status_copy.total_bytes_read);
    seq_printf(m, "Total bytes written:     %u\n", status_copy.total_bytes_written);
    seq_printf(m, "Current mode:            %d\n", status_copy.current_mode);

    return 0;
}

static int wal_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, wal_proc_show, NULL);
}

/*
 * Device initialization and cleanup functions
 */

int wal_driver_init_char_device(void)
{
    int ret;

    /* Allocate device number dynamically - this is the modern approach */
    ret = alloc_chrdev_region(&wal_global.char_dev, WAL_CHAR_MINOR, 1, WAL_CHAR_NAME);
    if (ret < 0) {
        pr_err("wal_driver: Failed to allocate character device region\n");
        return ret;
    }

    pr_info("wal_driver: Allocated character device major=%d, minor=%d\n",
            MAJOR(wal_global.char_dev), MINOR(wal_global.char_dev));

    /* Initialize and add character device */
    cdev_init(&wal_global.char_cdev, &wal_char_fops);
    wal_global.char_cdev.owner = THIS_MODULE;

    ret = cdev_add(&wal_global.char_cdev, wal_global.char_dev, 1);
    if (ret < 0) {
        pr_err("wal_driver: Failed to add character device\n");
        goto fail_cdev_add;
    }

    /* Create device class */
    wal_global.char_class = class_create(THIS_MODULE, WAL_CHAR_NAME);
    if (IS_ERR(wal_global.char_class)) {
        pr_err("wal_driver: Failed to create character device class\n");
        ret = PTR_ERR(wal_global.char_class);
        goto fail_class_create;
    }

    /* Create device */
    wal_global.char_device = device_create(wal_global.char_class, NULL,
                                          wal_global.char_dev, NULL, WAL_CHAR_NAME);
    if (IS_ERR(wal_global.char_device)) {
        pr_err("wal_driver: Failed to create character device\n");
        ret = PTR_ERR(wal_global.char_device);
        goto fail_device_create;
    }

    pr_info("wal_driver: Character device /dev/%s created successfully (major=%d, minor=%d)\n", 
            WAL_CHAR_NAME, MAJOR(wal_global.char_dev), MINOR(wal_global.char_dev));
    return 0;

fail_device_create:
    class_destroy(wal_global.char_class);
fail_class_create:
    cdev_del(&wal_global.char_cdev);
fail_cdev_add:
    unregister_chrdev_region(wal_global.char_dev, 1);
    return ret;
}

int wal_driver_init_block_device(void)
{
    int ret;
    int major_num;

    /* Allocate virtual storage */
    wal_global.block_data = vzalloc(WAL_BLOCK_SECTORS * WAL_BLOCK_SIZE);
    if (!wal_global.block_data) {
        pr_err("wal_driver: Failed to allocate block device storage\n");
        return -ENOMEM;
    }

    /* Initialize spinlock */
    spin_lock_init(&wal_global.block_lock);

    /* Create request queue */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)
    wal_global.block_queue = blk_alloc_queue(NUMA_NO_NODE);
    if (!wal_global.block_queue) {
        pr_err("wal_driver: Failed to allocate block device queue\n");
        ret = -ENOMEM;
        goto fail_queue;
    }
    blk_queue_make_request(wal_global.block_queue, wal_block_make_request);
#else
    wal_global.block_queue = blk_init_queue(wal_block_request, &wal_global.block_lock);
    if (!wal_global.block_queue) {
        pr_err("wal_driver: Failed to allocate block device queue\n");
        ret = -ENOMEM;
        goto fail_queue;
    }
#endif

    /* Set queue properties */
    blk_queue_logical_block_size(wal_global.block_queue, WAL_BLOCK_SIZE);

    /* Allocate gendisk */
    wal_global.block_disk = alloc_disk(1);
    if (!wal_global.block_disk) {
        pr_err("wal_driver: Failed to allocate block device disk\n");
        ret = -ENOMEM;
        goto fail_disk;
    }

    /* Register block device - use dynamic major number */
    major_num = register_blkdev(0, WAL_DEVICE_NAME);
    if (major_num < 0) {
        pr_err("wal_driver: Failed to register block device\n");
        ret = major_num;
        goto fail_register_blkdev;
    }

    pr_info("wal_driver: Allocated block device major number: %d\n", major_num);

    /* Configure gendisk */
    wal_global.block_disk->major = major_num;
    wal_global.block_disk->first_minor = WAL_BLOCK_MINOR;
    wal_global.block_disk->fops = &wal_block_ops;
    wal_global.block_disk->queue = wal_global.block_queue;
    snprintf(wal_global.block_disk->disk_name, 32, WAL_DEVICE_NAME);
    set_capacity(wal_global.block_disk, WAL_BLOCK_SECTORS);

    /* Add disk */
    add_disk(wal_global.block_disk);

    pr_info("wal_driver: Block device /dev/%s created successfully (major=%d, minor=%d)\n",
            WAL_DEVICE_NAME, major_num, WAL_BLOCK_MINOR);
    return 0;

fail_register_blkdev:
    put_disk(wal_global.block_disk);
fail_disk:
    blk_cleanup_queue(wal_global.block_queue);
fail_queue:
    vfree(wal_global.block_data);
    return ret;
}


void wal_driver_cleanup_char_device(void)
{
    if (wal_global.char_device) {
        device_destroy(wal_global.char_class, wal_global.char_dev);
    }
    if (wal_global.char_class) {
        class_destroy(wal_global.char_class);
    }
    cdev_del(&wal_global.char_cdev);
    unregister_chrdev_region(wal_global.char_dev, 1);

    pr_info("wal_driver: Character device cleaned up\n");
}

void wal_driver_cleanup_block_device(void)
{
    int major_num = 0;

    if (wal_global.block_disk) {
        major_num = wal_global.block_disk->major;
        del_gendisk(wal_global.block_disk);
        put_disk(wal_global.block_disk);
    }

    if (major_num > 0) {
        unregister_blkdev(major_num, WAL_DEVICE_NAME);
        pr_info("wal_driver: Unregistered block device major number: %d\n", major_num);
    }

    if (wal_global.block_queue) {
        blk_cleanup_queue(wal_global.block_queue);
    }
    if (wal_global.block_data) {
        vfree(wal_global.block_data);
    }

    pr_info("wal_driver: Block device cleaned up\n");
}


/*
 * Module initialization and cleanup
 */

static int __init wal_driver_init(void)
{
    int ret;

    pr_info("wal_driver: Initializing WAL driver v%s\n", WAL_DRIVER_VERSION);

    /* Initialize global state */
    memset(&wal_global, 0, sizeof(wal_global));
    mutex_init(&wal_global.status_mutex);
    wal_global.status.current_mode = WAL_MODE_NORMAL;

    /* Initialize character device */
    ret = wal_driver_init_char_device();
    if (ret) {
        pr_err("wal_driver: Failed to initialize character device\n");
        return ret;
    }

    /* Initialize block device */
    ret = wal_driver_init_block_device();
    if (ret) {
        pr_err("wal_driver: Failed to initialize block device\n");
        goto fail_block;
    }

    /* Create proc entry */
    wal_global.proc_entry = proc_create("wal_driver", 0444, NULL, &wal_proc_ops);
    if (!wal_global.proc_entry) {
        pr_warn("wal_driver: Failed to create proc entry\n");
    }

    pr_info("wal_driver: WAL driver initialized successfully\n");
    pr_info("wal_driver: Character device: /dev/%s (major=%d, minor=%d)\n",
            WAL_CHAR_NAME, WAL_MAJOR, WAL_CHAR_MINOR);
    pr_info("wal_driver: Block device: /dev/%s (major=%d, minor=%d)\n",
            WAL_DEVICE_NAME, WAL_MAJOR, WAL_BLOCK_MINOR);

    return 0;

fail_block:
    wal_driver_cleanup_char_device();
    return ret;
}

static void __exit wal_driver_exit(void)
{
    pr_info("wal_driver: Shutting down WAL driver\n");

    /* Remove proc entry */
    if (wal_global.proc_entry) {
        proc_remove(wal_global.proc_entry);
    }

    /* Cleanup devices */
    wal_driver_cleanup_block_device();
    wal_driver_cleanup_char_device();

    /* Print final statistics */
    pr_info("wal_driver: Final statistics:\n");
    pr_info("wal_driver: Character reads: %u, writes: %u\n",
            wal_global.status.char_read_count, wal_global.status.char_write_count);
    pr_info("wal_driver: Block reads: %u, writes: %u\n",
            wal_global.status.block_read_count, wal_global.status.block_write_count);
    pr_info("wal_driver: Total bytes: read=%u, written=%u\n",
            wal_global.status.total_bytes_read, wal_global.status.total_bytes_written);

    pr_info("wal_driver: WAL driver shutdown complete\n");
}

/* Utility functions for external access */
struct wal_status *wal_driver_get_status(void)
{
    return &wal_global.status;
}

void wal_driver_reset_stats(void)
{
    mutex_lock(&wal_global.status_mutex);
    memset(&wal_global.status, 0, sizeof(wal_global.status));
    wal_global.status.current_mode = WAL_MODE_NORMAL;
    mutex_unlock(&wal_global.status_mutex);
}

int wal_driver_set_mode(enum wal_mode mode)
{
    if (mode < WAL_MODE_NORMAL || mode > WAL_MODE_QUIET) {
        return -EINVAL;
    }

    mutex_lock(&wal_global.status_mutex);
    wal_global.status.current_mode = mode;
    mutex_unlock(&wal_global.status_mutex);

    return 0;
}

void wal_driver_print_stats(void)
{
    struct wal_status status_copy;

    mutex_lock(&wal_global.status_mutex);
    status_copy = wal_global.status;
    mutex_unlock(&wal_global.status_mutex);

    pr_info("wal_driver: Current statistics:\n");
    pr_info("wal_driver: Character reads: %u, writes: %u\n",
            status_copy.char_read_count, status_copy.char_write_count);
    pr_info("wal_driver: Block reads: %u, writes: %u\n",
            status_copy.block_read_count, status_copy.block_write_count);
    pr_info("wal_driver: Total bytes: read=%u, written=%u\n",
            status_copy.total_bytes_read, status_copy.total_bytes_written);
    pr_info("wal_driver: Current mode: %d\n", status_copy.current_mode);
}

module_init(wal_driver_init);
module_exit(wal_driver_exit);

