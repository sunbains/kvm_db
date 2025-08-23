#include "cp_pool.h"
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/highmem.h>
#include <linux/atomic.h>
#include <linux/spinlock.h>

/* Pool statistics */
atomic64_t cp_allocated = ATOMIC64_INIT(0);
atomic64_t cp_total_allocs = ATOMIC64_INIT(0);
atomic64_t cp_total_frees = ATOMIC64_INIT(0);

int cp_pool_init(void)
{
	/* Reset statistics */
	atomic64_set(&cp_allocated, 0);
	atomic64_set(&cp_total_allocs, 0);
	atomic64_set(&cp_total_frees, 0);
	
	pr_info("kdb: Canonical page pool initialized\n");
	return 0;
}

void cp_pool_exit(void)
{
	u64 allocated = atomic64_read(&cp_allocated);
	u64 total_allocs = atomic64_read(&cp_total_allocs);
	u64 total_frees = atomic64_read(&cp_total_frees);
	
	if (allocated > 0) {
		pr_warn("kdb: CP pool exit with %llu pages still allocated\n", allocated);
	}
	
	pr_info("kdb: CP pool stats - allocs: %llu, frees: %llu, leaked: %llu\n",
		total_allocs, total_frees, allocated);
}

struct page *cp_pool_alloc(void)
{
	struct page *page;
	
	/* For now, just use the kernel page allocator */
	page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!page)
		return NULL;
	
	/* Update statistics */
	atomic64_inc(&cp_allocated);
	atomic64_inc(&cp_total_allocs);
	
	return page;
}

void cp_pool_free(struct page *page)
{
	if (!page)
		return;
	
	/* Free the page directly - caller is responsible for reference counting */
	__free_page(page);
	
	/* Update statistics */
	atomic64_dec(&cp_allocated);
	atomic64_inc(&cp_total_frees);
}

void cp_pool_stats(u64 *allocated, u64 *total_allocs, u64 *total_frees)
{
	if (allocated)
		*allocated = atomic64_read(&cp_allocated);
	if (total_allocs)
		*total_allocs = atomic64_read(&cp_total_allocs);
	if (total_frees)
		*total_frees = atomic64_read(&cp_total_frees);
}