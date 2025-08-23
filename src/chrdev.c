#include "lp_state.h"
#include "cp_pool.h"
#include "kdb_module.h"
#include "../include/uapi/kdb.h"
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mman.h>

/* Global device state */
static struct {
	struct kdb_layout layout;
	bool layout_set;
	struct mutex lock;
} kdb_dev = {
	.layout_set = false,
	.lock = __MUTEX_INITIALIZER(kdb_dev.lock),
};

/**
 * kdb_open - Open the KDB cache device
 * @inode: Inode for the device
 * @filp: File pointer
 */
static int kdb_open(struct inode *inode, struct file *filp)
{
	pr_debug("kdb: device opened\n");
	return 0;
}

/**
 * kdb_release - Close the KDB cache device
 * @inode: Inode for the device
 * @filp: File pointer
 */
static int kdb_release(struct inode *inode, struct file *filp)
{
	pr_debug("kdb: device closed\n");
	return 0;
}

/**
 * kdb_mmap - Memory map the KDB cache
 * @filp: File pointer
 * @vma: VMA to map
 */
static int kdb_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct vma_ctx *ctx;
	unsigned long size;
	u64 total_size;
	
	mutex_lock(&kdb_dev.lock);
	
	/* Check if layout has been configured */
	if (!kdb_dev.layout_set) {
		mutex_unlock(&kdb_dev.lock);
		pr_err("kdb: mmap attempted before layout configuration\n");
		return -EINVAL;
	}
	
	/* Calculate total mapping size */
	total_size = kdb_dev.layout.n_lpn * kdb_dev.layout.lp_size;
	size = vma->vm_end - vma->vm_start;
	
	/* Validate mapping size */
	if (size > total_size) {
		mutex_unlock(&kdb_dev.lock);
		pr_err("kdb: mmap size %lu exceeds configured size %llu\n", 
		       size, total_size);
		return -EINVAL;
	}
	
	/* Create VMA context */
	ctx = vma_ctx_create(kdb_dev.layout.cp_size, 
			     kdb_dev.layout.lp_size,
			     kdb_dev.layout.n_lpn);
	if (!ctx) {
		mutex_unlock(&kdb_dev.lock);
		pr_err("kdb: failed to create VMA context\n");
		return -ENOMEM;
	}
	
	mutex_unlock(&kdb_dev.lock);
	
	/* Configure VMA */
	vma->vm_ops = &kdb_vm_ops;
	vma->vm_private_data = ctx;
	vm_flags_set(vma, VM_MIXEDMAP);  /* Mixed page types */
	
	pr_info("kdb: mmap configured: size=%lu, lpns=%llu, lp_size=%llu, cp_size=%llu\n",
		size, ctx->n_lpn, ctx->lp_size, ctx->cp_size);
	
	return 0;
}

/**
 * kdb_ioctl - Handle ioctl commands
 * @filp: File pointer
 * @cmd: IOCTL command
 * @arg: IOCTL argument
 */
static long kdb_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct kdb_layout layout;
	struct kdb_stats stats;
	int ret = 0;
	
	switch (cmd) {
	case KDB_SET_LAYOUT:
		if (copy_from_user(&layout, (void __user *)arg, sizeof(layout)))
			return -EFAULT;
		
		/* Validate layout parameters */
		if (!layout.cp_size || !layout.lp_size || !layout.n_lpn) {
			pr_err("kdb: invalid layout parameters\n");
			return -EINVAL;
		}
		
		if (layout.lp_size % layout.cp_size != 0) {
			pr_err("kdb: lp_size must be multiple of cp_size\n");
			return -EINVAL;
		}
		
		if (layout.lp_size / layout.cp_size > LP_CP_MAX) {
			pr_err("kdb: too many CPs per LP: %llu (max %d)\n", 
			       layout.lp_size / layout.cp_size, LP_CP_MAX);
			return -EINVAL;
		}
		
		mutex_lock(&kdb_dev.lock);
		kdb_dev.layout = layout;
		kdb_dev.layout_set = true;
		mutex_unlock(&kdb_dev.lock);
		
		pr_info("kdb: layout configured: cp_size=%llu, lp_size=%llu, n_lpn=%llu\n",
			layout.cp_size, layout.lp_size, layout.n_lpn);
		break;
		
	case KDB_GET_LAYOUT:
		mutex_lock(&kdb_dev.lock);
		if (!kdb_dev.layout_set) {
			mutex_unlock(&kdb_dev.lock);
			return -ENODATA;
		}
		layout = kdb_dev.layout;
		mutex_unlock(&kdb_dev.lock);
		
		if (copy_to_user((void __user *)arg, &layout, sizeof(layout)))
			return -EFAULT;
		break;
		
	case KDB_GET_STATS:
		/* For now, just return zeroed stats - full implementation would
		 * aggregate from all active VMAs */
		memset(&stats, 0, sizeof(stats));
		
		/* Add basic CP pool stats */
		cp_pool_stats(&stats.allocated_cp, NULL, NULL);
		
		if (copy_to_user((void __user *)arg, &stats, sizeof(stats)))
			return -EFAULT;
		break;
		
	case KDB_RESET_STATS:
		/* Reset global statistics - for now just a no-op */
		pr_info("kdb: statistics reset\n");
		break;
		
	default:
		ret = -ENOTTY;
	}
	
	return ret;
}

/* File operations */
static const struct file_operations kdb_fops = {
	.owner = THIS_MODULE,
	.open = kdb_open,
	.release = kdb_release,
	.mmap = kdb_mmap,
	.unlocked_ioctl = kdb_ioctl,
	.compat_ioctl = kdb_ioctl,
};

/* Miscdevice structure */
static struct miscdevice kdb_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = KDB_DEV_NAME,
	.fops = &kdb_fops,
};

/**
 * kdb_chrdev_init - Initialize the character device
 */
int kdb_chrdev_init(void)
{
	int ret;
	
	/* Register miscdevice */
	ret = misc_register(&kdb_miscdev);
	if (ret) {
		pr_err("kdb: failed to register misc device: %d\n", ret);
		return ret;
	}
	
	pr_info("kdb: character device registered as /dev/%s\n", KDB_DEV_NAME);
	return 0;
}

/**
 * kdb_chrdev_exit - Cleanup the character device
 */
void kdb_chrdev_exit(void)
{
	misc_deregister(&kdb_miscdev);
	pr_info("kdb: character device unregistered\n");
}