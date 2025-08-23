#ifndef _KDB_LP_STATE_H
#define _KDB_LP_STATE_H

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/bitmap.h>

struct page;
struct vma_ctx;

/* Default configuration - can be overridden by ioctl */
#define LP_CP_MAX 1024  /* Max canonical pages per logical page */
#define LP_SIZE_DEFAULT (4096 * LP_CP_MAX)  /* 4MB logical page by default */
#define CP_SIZE_DEFAULT 4096                /* 4KB canonical page */

/* Logical Page State */
struct lp_state {
	spinlock_t lock;                    /* Protects this structure */
	u64 lpn;                           /* Logical page number */
	u32 cp_per_lp;                     /* Canonical pages per logical page */
	struct page **cp;                  /* Array of canonical page pointers */
	unsigned long *dirty_bitmap;       /* Dirty bitmap for canonical pages */
	atomic_t refcount;                 /* Reference count */
	struct hlist_node hash_node;       /* Hash table linkage */
};

/* VMA context */
struct vma_ctx {
	u64 cp_size;                       /* Canonical page size */
	u64 lp_size;                       /* Logical page size */
	u64 n_lpn;                         /* Number of logical page numbers */
	u32 cp_per_lp;                     /* Canonical pages per logical page */
	
	/* Hash table for LPN -> lp_state mapping */
	struct hlist_head *lp_hash;
	u32 lp_hash_bits;
	spinlock_t hash_lock;              /* Protects hash table */
	
	/* Statistics */
	atomic64_t total_faults;
	atomic64_t total_mkwrite;
	atomic64_t total_lp_created;
};

/**
 * lp_state_init - Initialize LP state subsystem
 * Returns 0 on success, negative error code on failure
 */
int lp_state_init(void);

/**
 * lp_state_exit - Clean up LP state subsystem
 */
void lp_state_exit(void);

/**
 * vma_ctx_create - Create a new VMA context
 * @cp_size: Canonical page size
 * @lp_size: Logical page size
 * @n_lpn: Number of logical page numbers
 * Returns allocated vma_ctx or NULL on failure
 */
struct vma_ctx *vma_ctx_create(u64 cp_size, u64 lp_size, u64 n_lpn);

/**
 * vma_ctx_destroy - Destroy a VMA context
 * @ctx: Context to destroy
 */
void vma_ctx_destroy(struct vma_ctx *ctx);

/**
 * lp_get_or_create - Get or create logical page state
 * @ctx: VMA context
 * @lpn: Logical page number
 * Returns lp_state pointer or NULL on failure
 */
struct lp_state *lp_get_or_create(struct vma_ctx *ctx, u64 lpn);

/**
 * lp_lookup - Lookup logical page state
 * @ctx: VMA context
 * @lpn: Logical page number
 * Returns lp_state pointer or NULL if not found
 */
struct lp_state *lp_lookup(struct vma_ctx *ctx, u64 lpn);

/**
 * lp_put - Release reference to logical page state
 * @lp: Logical page state
 */
void lp_put(struct lp_state *lp);

#endif /* _KDB_LP_STATE_H */