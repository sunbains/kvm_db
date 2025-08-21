/*
 * wal_driver.h - Write-Ahead Log Device Driver Header
 *
 * This header defines the interface for WAL device drivers:
 * - /dev/rwal (character device, major=240, minor=0)
 * - /dev/wal  (block device, major=240, minor=1)
 */

#ifndef WAL_DRIVER_H
#define WAL_DRIVER_H

#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/version.h>

/* Device constants */
#define WAL_MAJOR           240
#define WAL_CHAR_MINOR      0
#define WAL_BLOCK_MINOR     1
#define WAL_DEVICE_NAME     "wal"
#define WAL_CHAR_NAME       "rwal"
#define WAL_BLOCK_SIZE      512
#define WAL_BLOCK_SECTORS   2048  /* 1MB virtual block device */

/* Module information */
#define WAL_DRIVER_VERSION  "1.0"
#define WAL_DRIVER_AUTHOR   "KVM Database Project"
#define WAL_DRIVER_DESC     "WAL Character and Block Device Driver"

/* Response message */
#define WAL_RESPONSE_MSG    "Hello from WAL\n"
#define WAL_RESPONSE_LEN    15

/* IOCTL commands for WAL devices */
#define WAL_IOC_MAGIC       'w'
#define WAL_IOC_RESET       _IO(WAL_IOC_MAGIC, 0)
#define WAL_IOC_GET_STATUS  _IOR(WAL_IOC_MAGIC, 1, int)
#define WAL_IOC_SET_MODE    _IOW(WAL_IOC_MAGIC, 2, int)
#define WAL_IOC_MAXNR       2

/* WAL device modes */
enum wal_mode {
    WAL_MODE_NORMAL = 0,
    WAL_MODE_DEBUG = 1,
    WAL_MODE_QUIET = 2
};

/* WAL device status structure */
struct wal_status {
    __u32 char_read_count;
    __u32 char_write_count;
    __u32 block_read_count;
    __u32 block_write_count;
    __u32 total_bytes_read;
    __u32 total_bytes_written;
    enum wal_mode current_mode;
};

/* Function declarations for external use */
#ifdef __KERNEL__

/* Character device operations */
int wal_char_open(struct inode *inode, struct file *file);
int wal_char_release(struct inode *inode, struct file *file);
ssize_t wal_char_read(struct file *file, char __user *buffer, size_t count, loff_t *pos);
ssize_t wal_char_write(struct file *file, const char __user *buffer, size_t count, loff_t *pos);
long wal_char_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

/* Block device operations - Updated for kernel compatibility */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 5, 0)
int wal_block_open(struct gendisk *disk, blk_mode_t mode);
void wal_block_release(struct gendisk *disk);
#else
int wal_block_open(struct block_device *bdev, fmode_t mode);
void wal_block_release(struct gendisk *disk, fmode_t mode);
#endif

int wal_block_getgeo(struct block_device *bdev, struct hd_geometry *geo);

/* Block device request handling - internal functions, not exported */

/* Utility functions */
int wal_driver_init_char_device(void);
int wal_driver_init_block_device(void);
void wal_driver_cleanup_char_device(void);
void wal_driver_cleanup_block_device(void);
void wal_driver_print_stats(void);

/* Global data accessors */
struct wal_status *wal_driver_get_status(void);
void wal_driver_reset_stats(void);
int wal_driver_set_mode(enum wal_mode mode);

#endif /* __KERNEL__ */

#endif /* WAL_DRIVER_H */

