/*
 * uringblk_sysfs.c - Sysfs attributes for uringblk driver
 *
 * This file implements the sysfs interface for the uringblk driver,
 * providing runtime configuration and statistics visibility.
 */

#include <linux/kernel.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/blkdev.h>
#include <linux/kobject.h>

#include "uringblk_driver.h"

/* Sysfs show/store functions for device attributes */

static ssize_t features_show(struct device *dev, struct device_attribute *attr,
                            char *buf)
{
    struct gendisk *disk = dev_to_disk(dev);
    struct uringblk_device *udev = disk->private_data;
    
    return sprintf(buf, "0x%llx\n", udev->features);
}

static ssize_t firmware_rev_show(struct device *dev, struct device_attribute *attr,
                                 char *buf)
{
    struct gendisk *disk = dev_to_disk(dev);
    struct uringblk_device *udev = disk->private_data;
    
    return sprintf(buf, "%s\n", udev->firmware);
}

static ssize_t model_show(struct device *dev, struct device_attribute *attr,
                         char *buf)
{
    struct gendisk *disk = dev_to_disk(dev);
    struct uringblk_device *udev = disk->private_data;
    
    return sprintf(buf, "%s\n", udev->model);
}

static ssize_t nr_hw_queues_show(struct device *dev, struct device_attribute *attr,
                                 char *buf)
{
    struct gendisk *disk = dev_to_disk(dev);
    struct uringblk_device *udev = disk->private_data;
    
    return sprintf(buf, "%u\n", udev->config.nr_hw_queues);
}

static ssize_t queue_depth_show(struct device *dev, struct device_attribute *attr,
                               char *buf)
{
    struct gendisk *disk = dev_to_disk(dev);
    struct uringblk_device *udev = disk->private_data;
    
    return sprintf(buf, "%u\n", udev->config.queue_depth);
}

static ssize_t poll_enabled_show(struct device *dev, struct device_attribute *attr,
                                 char *buf)
{
    struct gendisk *disk = dev_to_disk(dev);
    struct uringblk_device *udev = disk->private_data;
    
    return sprintf(buf, "%s\n", udev->config.enable_poll ? "enabled" : "disabled");
}

static ssize_t discard_enabled_show(struct device *dev, struct device_attribute *attr,
                                   char *buf)
{
    struct gendisk *disk = dev_to_disk(dev);
    struct uringblk_device *udev = disk->private_data;
    
    return sprintf(buf, "%s\n", udev->config.enable_discard ? "enabled" : "disabled");
}

static ssize_t write_cache_show(struct device *dev, struct device_attribute *attr,
                               char *buf)
{
    struct gendisk *disk = dev_to_disk(dev);
    struct uringblk_device *udev = disk->private_data;
    
    return sprintf(buf, "%s\n", udev->config.write_cache ? "write-back" : "write-through");
}

static ssize_t capacity_show(struct device *dev, struct device_attribute *attr,
                            char *buf)
{
    struct gendisk *disk = dev_to_disk(dev);
    struct uringblk_device *udev = disk->private_data;
    
    return sprintf(buf, "%zu\n", udev->backend.capacity);
}

static ssize_t read_ops_show(struct device *dev, struct device_attribute *attr,
                            char *buf)
{
    struct gendisk *disk = dev_to_disk(dev);
    struct uringblk_device *udev = disk->private_data;
    unsigned long flags;
    u64 read_ops;
    
    spin_lock_irqsave(&udev->stats_lock, flags);
    read_ops = udev->stats.read_ops;
    spin_unlock_irqrestore(&udev->stats_lock, flags);
    
    return sprintf(buf, "%llu\n", read_ops);
}

static ssize_t write_ops_show(struct device *dev, struct device_attribute *attr,
                             char *buf)
{
    struct gendisk *disk = dev_to_disk(dev);
    struct uringblk_device *udev = disk->private_data;
    unsigned long flags;
    u64 write_ops;
    
    spin_lock_irqsave(&udev->stats_lock, flags);
    write_ops = udev->stats.write_ops;
    spin_unlock_irqrestore(&udev->stats_lock, flags);
    
    return sprintf(buf, "%llu\n", write_ops);
}

static ssize_t read_bytes_show(struct device *dev, struct device_attribute *attr,
                              char *buf)
{
    struct gendisk *disk = dev_to_disk(dev);
    struct uringblk_device *udev = disk->private_data;
    unsigned long flags;
    u64 read_bytes;
    
    spin_lock_irqsave(&udev->stats_lock, flags);
    read_bytes = udev->stats.read_bytes;
    spin_unlock_irqrestore(&udev->stats_lock, flags);
    
    return sprintf(buf, "%llu\n", read_bytes);
}

static ssize_t write_bytes_show(struct device *dev, struct device_attribute *attr,
                               char *buf)
{
    struct gendisk *disk = dev_to_disk(dev);
    struct uringblk_device *udev = disk->private_data;
    unsigned long flags;
    u64 write_bytes;
    
    spin_lock_irqsave(&udev->stats_lock, flags);
    write_bytes = udev->stats.write_bytes;
    spin_unlock_irqrestore(&udev->stats_lock, flags);
    
    return sprintf(buf, "%llu\n", write_bytes);
}

static ssize_t flush_ops_show(struct device *dev, struct device_attribute *attr,
                             char *buf)
{
    struct gendisk *disk = dev_to_disk(dev);
    struct uringblk_device *udev = disk->private_data;
    unsigned long flags;
    u64 flush_ops;
    
    spin_lock_irqsave(&udev->stats_lock, flags);
    flush_ops = udev->stats.flush_ops;
    spin_unlock_irqrestore(&udev->stats_lock, flags);
    
    return sprintf(buf, "%llu\n", flush_ops);
}

static ssize_t discard_ops_show(struct device *dev, struct device_attribute *attr,
                               char *buf)
{
    struct gendisk *disk = dev_to_disk(dev);
    struct uringblk_device *udev = disk->private_data;
    unsigned long flags;
    u64 discard_ops;
    
    spin_lock_irqsave(&udev->stats_lock, flags);
    discard_ops = udev->stats.discard_ops;
    spin_unlock_irqrestore(&udev->stats_lock, flags);
    
    return sprintf(buf, "%llu\n", discard_ops);
}

static ssize_t queue_full_events_show(struct device *dev, struct device_attribute *attr,
                                     char *buf)
{
    struct gendisk *disk = dev_to_disk(dev);
    struct uringblk_device *udev = disk->private_data;
    unsigned long flags;
    u64 queue_full_events;
    
    spin_lock_irqsave(&udev->stats_lock, flags);
    queue_full_events = udev->stats.queue_full_events;
    spin_unlock_irqrestore(&udev->stats_lock, flags);
    
    return sprintf(buf, "%llu\n", queue_full_events);
}

static ssize_t media_errors_show(struct device *dev, struct device_attribute *attr,
                                char *buf)
{
    struct gendisk *disk = dev_to_disk(dev);
    struct uringblk_device *udev = disk->private_data;
    unsigned long flags;
    u64 media_errors;
    
    spin_lock_irqsave(&udev->stats_lock, flags);
    media_errors = udev->stats.media_errors;
    spin_unlock_irqrestore(&udev->stats_lock, flags);
    
    return sprintf(buf, "%llu\n", media_errors);
}

static ssize_t stats_reset_store(struct device *dev, struct device_attribute *attr,
                                const char *buf, size_t count)
{
    struct gendisk *disk = dev_to_disk(dev);
    struct uringblk_device *udev = disk->private_data;
    unsigned long flags;
    int val;
    
    if (kstrtoint(buf, 10, &val) || val != 1)
        return -EINVAL;
    
    spin_lock_irqsave(&udev->stats_lock, flags);
    memset(&udev->stats, 0, sizeof(udev->stats));
    spin_unlock_irqrestore(&udev->stats_lock, flags);
    
    return count;
}

/* Device attribute definitions */
static DEVICE_ATTR_RO(features);
static DEVICE_ATTR_RO(firmware_rev);
static DEVICE_ATTR_RO(model);
static DEVICE_ATTR_RO(nr_hw_queues);
static DEVICE_ATTR_RO(queue_depth);
static DEVICE_ATTR_RO(poll_enabled);
static DEVICE_ATTR_RO(discard_enabled);
static DEVICE_ATTR_RO(write_cache);
static DEVICE_ATTR_RO(capacity);
static DEVICE_ATTR_RO(read_ops);
static DEVICE_ATTR_RO(write_ops);
static DEVICE_ATTR_RO(read_bytes);
static DEVICE_ATTR_RO(write_bytes);
static DEVICE_ATTR_RO(flush_ops);
static DEVICE_ATTR_RO(discard_ops);
static DEVICE_ATTR_RO(queue_full_events);
static DEVICE_ATTR_RO(media_errors);
static DEVICE_ATTR_WO(stats_reset);

/* Array of device attributes */
static struct attribute *uringblk_attrs[] = {
    &dev_attr_features.attr,
    &dev_attr_firmware_rev.attr,
    &dev_attr_model.attr,
    &dev_attr_nr_hw_queues.attr,
    &dev_attr_queue_depth.attr,
    &dev_attr_poll_enabled.attr,
    &dev_attr_discard_enabled.attr,
    &dev_attr_write_cache.attr,
    &dev_attr_capacity.attr,
    &dev_attr_read_ops.attr,
    &dev_attr_write_ops.attr,
    &dev_attr_read_bytes.attr,
    &dev_attr_write_bytes.attr,
    &dev_attr_flush_ops.attr,
    &dev_attr_discard_ops.attr,
    &dev_attr_queue_full_events.attr,
    &dev_attr_media_errors.attr,
    &dev_attr_stats_reset.attr,
    NULL,
};

/* Attribute group */
static const struct attribute_group uringblk_attr_group = {
    .name = "uringblk",
    .attrs = uringblk_attrs,
};

/*
 * Public functions for sysfs management
 */
int uringblk_sysfs_create(struct gendisk *disk)
{
    return sysfs_create_group(&disk_to_dev(disk)->kobj, &uringblk_attr_group);
}

void uringblk_sysfs_remove(struct gendisk *disk)
{
    sysfs_remove_group(&disk_to_dev(disk)->kobj, &uringblk_attr_group);
}