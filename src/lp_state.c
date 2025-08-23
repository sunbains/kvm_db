#include "lp_state.h"
#include "cp_pool.h"
#include <linux/slab.h>
#include <linux/hash.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>

/* Access to cp_pool statistics */
extern atomic64_t cp_allocated;
extern atomic64_t cp_total_frees;

static struct kmem_cache *lp_state_cache;
static struct kmem_cache *vma_ctx_cache;

#define LP_HASH_BITS 10
#define LP_HASH_SIZE (1 << LP_HASH_BITS)

int lp_state_init(void)
{
	/* Create slab caches */
	lp_state_cache = kmem_cache_create("kdb_lp_state",
					   sizeof(struct lp_state),
					   0, SLAB_HWCACHE_ALIGN, NULL);
	if (!lp_state_cache)
		return -ENOMEM;
	
	vma_ctx_cache = kmem_cache_create("kdb_vma_ctx",
					  sizeof(struct vma_ctx),
					  0, SLAB_HWCACHE_ALIGN, NULL);
	if (!vma_ctx_cache) {
		kmem_cache_destroy(lp_state_cache);
		return -ENOMEM;
	}
	
	pr_info("kdb: LP state management initialized\n");
	return 0;
}

void lp_state_exit(void)
{
	if (vma_ctx_cache)
		kmem_cache_destroy(vma_ctx_cache);
	if (lp_state_cache)
		kmem_cache_destroy(lp_state_cache);
	
	pr_info("kdb: LP state management exited\n");
}

struct vma_ctx *vma_ctx_create(u64 cp_size, u64 lp_size, u64 n_lpn)
{
	struct vma_ctx *ctx;
	u32 cp_per_lp;
	int i;
	
	/* Validate parameters */
	if (!cp_size || !lp_size || !n_lpn)
		return NULL;
	
	if (lp_size % cp_size != 0)
		return NULL;
	
	cp_per_lp = lp_size / cp_size;
	if (cp_per_lp > LP_CP_MAX)
		return NULL;
	
	/* Allocate context */
	ctx = kmem_cache_zalloc(vma_ctx_cache, GFP_KERNEL);
	if (!ctx)
		return NULL;
	
	/* Initialize parameters */
	ctx->cp_size = cp_size;
	ctx->lp_size = lp_size;
	ctx->n_lpn = n_lpn;
	ctx->cp_per_lp = cp_per_lp;
	
	/* Allocate hash table */
	ctx->lp_hash_bits = LP_HASH_BITS;
	ctx->lp_hash = kcalloc(LP_HASH_SIZE, sizeof(struct hlist_head), GFP_KERNEL);
	if (!ctx->lp_hash) {
		kmem_cache_free(vma_ctx_cache, ctx);
		return NULL;
	}
	
	/* Initialize hash table */
	for (i = 0; i < LP_HASH_SIZE; i++)
		INIT_HLIST_HEAD(&ctx->lp_hash[i]);
	
	spin_lock_init(&ctx->hash_lock);
	
	/* Initialize statistics */
	atomic64_set(&ctx->total_faults, 0);
	atomic64_set(&ctx->total_mkwrite, 0);
	atomic64_set(&ctx->total_lp_created, 0);
	
	return ctx;
}

void vma_ctx_destroy(struct vma_ctx *ctx)
{
	struct lp_state *lp;
	struct hlist_node *tmp;
	int i;
	
	if (!ctx)
		return;
	
	/* Free all logical page states */
	spin_lock(&ctx->hash_lock);
	for (i = 0; i < LP_HASH_SIZE; i++) {
		hlist_for_each_entry_safe(lp, tmp, &ctx->lp_hash[i], hash_node) {
			/* Remove from hash */
			hlist_del(&lp->hash_node);
			
			/* Note: We don't free the canonical pages here because they're 
			 * managed by the kernel VMA system. The pages will be freed 
			 * when their reference count reaches zero. We just clear our 
			 * pointers to avoid use-after-free. */
			if (lp->cp) {
				int j;
				for (j = 0; j < ctx->cp_per_lp; j++) {
					if (lp->cp[j]) {
						/* Update statistics to reflect that we're no longer tracking this page */
						atomic64_dec(&cp_allocated);
						atomic64_inc(&cp_total_frees);
					}
				}
				kfree(lp->cp);
			}
			
			/* Free dirty bitmap */
			kfree(lp->dirty_bitmap);
			
			/* Free lp_state */
			kmem_cache_free(lp_state_cache, lp);
		}
	}
	spin_unlock(&ctx->hash_lock);
	
	/* Free hash table */
	kfree(ctx->lp_hash);
	
	/* Free context */
	kmem_cache_free(vma_ctx_cache, ctx);
}

static u32 lp_hash_fn(u64 lpn, u32 bits)
{
	return hash_64(lpn, bits);
}

struct lp_state *lp_get_or_create(struct vma_ctx *ctx, u64 lpn)
{
	struct lp_state *lp;
	struct hlist_head *head;
	u32 hash;
	int cp_bitmap_size;
	
	if (!ctx || lpn >= ctx->n_lpn)
		return NULL;
	
	hash = lp_hash_fn(lpn, ctx->lp_hash_bits);
	head = &ctx->lp_hash[hash];
	
	/* First, try to find existing */
	spin_lock(&ctx->hash_lock);
	hlist_for_each_entry(lp, head, hash_node) {
		if (lp->lpn == lpn) {
			atomic_inc(&lp->refcount);
			spin_unlock(&ctx->hash_lock);
			return lp;
		}
	}
	spin_unlock(&ctx->hash_lock);
	
	/* Allocate new lp_state */
	lp = kmem_cache_zalloc(lp_state_cache, GFP_KERNEL);
	if (!lp)
		return NULL;
	
	/* Initialize */
	spin_lock_init(&lp->lock);
	lp->lpn = lpn;
	lp->cp_per_lp = ctx->cp_per_lp;
	atomic_set(&lp->refcount, 1);
	
	/* Allocate canonical page array */
	lp->cp = kcalloc(ctx->cp_per_lp, sizeof(struct page *), GFP_KERNEL);
	if (!lp->cp) {
		kmem_cache_free(lp_state_cache, lp);
		return NULL;
	}
	
	/* Allocate dirty bitmap */
	cp_bitmap_size = BITS_TO_LONGS(ctx->cp_per_lp) * sizeof(unsigned long);
	lp->dirty_bitmap = kzalloc(cp_bitmap_size, GFP_KERNEL);
	if (!lp->dirty_bitmap) {
		kfree(lp->cp);
		kmem_cache_free(lp_state_cache, lp);
		return NULL;
	}
	
	/* Add to hash table */
	spin_lock(&ctx->hash_lock);
	/* Check again to avoid race */
	struct lp_state *existing;
	hlist_for_each_entry(existing, head, hash_node) {
		if (existing->lpn == lpn) {
			atomic_inc(&existing->refcount);
			spin_unlock(&ctx->hash_lock);
			
			/* Free the one we just created */
			kfree(lp->dirty_bitmap);
			kfree(lp->cp);
			kmem_cache_free(lp_state_cache, lp);
			
			return existing;
		}
	}
	
	/* Add to hash */
	hlist_add_head(&lp->hash_node, head);
	spin_unlock(&ctx->hash_lock);
	
	atomic64_inc(&ctx->total_lp_created);
	
	return lp;
}

struct lp_state *lp_lookup(struct vma_ctx *ctx, u64 lpn)
{
	struct lp_state *lp;
	struct hlist_head *head;
	u32 hash;
	
	if (!ctx || lpn >= ctx->n_lpn)
		return NULL;
	
	hash = lp_hash_fn(lpn, ctx->lp_hash_bits);
	head = &ctx->lp_hash[hash];
	
	spin_lock(&ctx->hash_lock);
	hlist_for_each_entry(lp, head, hash_node) {
		if (lp->lpn == lpn) {
			atomic_inc(&lp->refcount);
			spin_unlock(&ctx->hash_lock);
			return lp;
		}
	}
	spin_unlock(&ctx->hash_lock);
	
	return NULL;
}

void lp_put(struct lp_state *lp)
{
	if (!lp)
		return;
	
	if (atomic_dec_and_test(&lp->refcount)) {
		/* Should not happen - cleanup is done in vma_ctx_destroy */
		pr_warn("kdb: lp_state refcount reached zero\n");
	}
}