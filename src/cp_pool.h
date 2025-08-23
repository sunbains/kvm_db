#ifndef _KDB_CP_POOL_H
#define _KDB_CP_POOL_H

#include <linux/types.h>

struct page;

/* Canonical Page Pool - 4KB page allocator */

/**
 * cp_pool_init - Initialize the canonical page pool
 * Returns 0 on success, negative error code on failure
 */
int cp_pool_init(void);

/**
 * cp_pool_exit - Clean up the canonical page pool
 */
void cp_pool_exit(void);

/**
 * cp_pool_alloc - Allocate a canonical page
 * Returns a zeroed struct page* or NULL on failure
 */
struct page *cp_pool_alloc(void);

/**
 * cp_pool_free - Free a canonical page
 * @page: The page to free
 */
void cp_pool_free(struct page *page);

/**
 * cp_pool_stats - Get pool statistics
 * @allocated: Number of currently allocated pages (output)
 * @total_allocs: Total allocations since init (output)
 * @total_frees: Total frees since init (output)
 */
void cp_pool_stats(u64 *allocated, u64 *total_allocs, u64 *total_frees);

#endif /* _KDB_CP_POOL_H */