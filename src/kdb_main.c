#include "lp_state.h"
#include "cp_pool.h"
#include "kdb_module.h"
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

/**
 * kdb_init - Initialize the KDB cache module
 */
static int __init kdb_init(void)
{
	int ret;
	
	pr_info("kdb: KDB Cache Module v1.0 loading\n");
	
	/* Initialize canonical page pool */
	ret = cp_pool_init();
	if (ret) {
		pr_err("kdb: failed to initialize CP pool: %d\n", ret);
		goto err_cp_pool;
	}
	
	/* Initialize logical page state management */
	ret = lp_state_init();
	if (ret) {
		pr_err("kdb: failed to initialize LP state: %d\n", ret);
		goto err_lp_state;
	}
	
	/* Initialize character device */
	ret = kdb_chrdev_init();
	if (ret) {
		pr_err("kdb: failed to initialize character device: %d\n", ret);
		goto err_chrdev;
	}
	
	pr_info("kdb: KDB Cache Module loaded successfully\n");
	return 0;
	
err_chrdev:
	lp_state_exit();
err_lp_state:
	cp_pool_exit();
err_cp_pool:
	return ret;
}

/**
 * kdb_exit - Cleanup the KDB cache module
 */
static void __exit kdb_exit(void)
{
	pr_info("kdb: KDB Cache Module unloading\n");
	
	/* Cleanup in reverse order */
	kdb_chrdev_exit();
	lp_state_exit();
	cp_pool_exit();
	
	pr_info("kdb: KDB Cache Module unloaded\n");
}

module_init(kdb_init);
module_exit(kdb_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("KDB Cache Team");
MODULE_DESCRIPTION("KDB Page-Fault Driven Cache");
MODULE_VERSION("1.0");
MODULE_ALIAS("char-major-" __stringify(MISC_MAJOR) "-" __stringify(MISC_DYNAMIC_MINOR));