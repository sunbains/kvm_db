#include "lp_state.h"
#include "cp_pool.h"
#include <linux/mm.h>
#include <linux/highmem.h>

/**
 * kdb_fault - Handle page faults for KDB mappings
 * @vmf: VM fault information
 * 
 * This function handles page faults by:
 * 1. Decoding the LPN and CP index from the page offset
 * 2. Getting or creating the logical page state
 * 3. Allocating a canonical page if needed (zero-filled)
 * 4. Installing the page in the VMA
 */
static vm_fault_t kdb_fault(struct vm_fault *vmf)
{
	struct vma_ctx *ctx = vmf->vma->vm_private_data;
	u64 pgoff = vmf->pgoff;               /* 4K page index */
	u64 lpn = pgoff / ctx->cp_per_lp;     /* Logical page number */
	u32 cpi = pgoff % ctx->cp_per_lp;     /* Canonical page index */
	struct lp_state *lp;
	struct page *pg;
	vm_fault_t ret = VM_FAULT_LOCKED;
	
	if (!ctx) {
		pr_err("kdb: fault with NULL vma context\n");
		return VM_FAULT_SIGBUS;
	}
	
	/* Validate bounds */
	if (lpn >= ctx->n_lpn) {
		pr_err("kdb: fault beyond allocated range: lpn=%llu, max=%llu\n", 
		       lpn, ctx->n_lpn);
		return VM_FAULT_SIGBUS;
	}
	
	/* Get or create logical page state */
	lp = lp_get_or_create(ctx, lpn);
	if (!lp) {
		pr_err("kdb: failed to get/create lp_state for lpn=%llu\n", lpn);
		return VM_FAULT_OOM;
	}
	
	/* Lock the logical page */
	spin_lock(&lp->lock);
	
	/* Check if we already have this canonical page */
	pg = lp->cp[cpi];
	if (!pg) {
		/* Allocate and zero-fill a new canonical page */
		pg = cp_pool_alloc();
		if (!pg) {
			spin_unlock(&lp->lock);
			lp_put(lp);
			pr_err("kdb: failed to allocate canonical page\n");
			return VM_FAULT_OOM;
		}
		
		/* Store in the logical page state */
		lp->cp[cpi] = pg;
		
		pr_debug("kdb: allocated CP for lpn=%llu, cpi=%u\n", lpn, cpi);
	}
	
	/* Take a reference for the VMA - the kernel will drop this when the VMA is unmapped */
	get_page(pg);
	vmf->page = pg;
	
	spin_unlock(&lp->lock);
	lp_put(lp);
	
	/* Update statistics */
	atomic64_inc(&ctx->total_faults);
	
	return ret;
}

/**
 * kdb_page_mkwrite - Handle write faults (copy-on-write notification)
 * @vmf: VM fault information
 * 
 * This function is called when a page is about to be written to.
 * We use it to track dirty pages in our bitmap.
 */
static vm_fault_t kdb_page_mkwrite(struct vm_fault *vmf)
{
	struct vma_ctx *ctx = vmf->vma->vm_private_data;
	u64 pgoff = vmf->pgoff;
	u64 lpn = pgoff / ctx->cp_per_lp;
	u32 cpi = pgoff % ctx->cp_per_lp;
	struct lp_state *lp;
	
	if (!ctx) {
		pr_err("kdb: mkwrite with NULL vma context\n");
		return VM_FAULT_SIGBUS;
	}
	
	/* Validate bounds */
	if (lpn >= ctx->n_lpn) {
		pr_err("kdb: mkwrite beyond allocated range: lpn=%llu, max=%llu\n", 
		       lpn, ctx->n_lpn);
		return VM_FAULT_SIGBUS;
	}
	
	/* Lookup logical page state */
	lp = lp_lookup(ctx, lpn);
	if (!lp) {
		pr_err("kdb: mkwrite on non-existent lp_state for lpn=%llu\n", lpn);
		return VM_FAULT_SIGBUS;
	}
	
	/* Mark the canonical page as dirty */
	spin_lock(&lp->lock);
	set_bit(cpi, lp->dirty_bitmap);
	spin_unlock(&lp->lock);
	
	lp_put(lp);
	
	/* Update statistics */
	atomic64_inc(&ctx->total_mkwrite);
	
	pr_debug("kdb: marked dirty: lpn=%llu, cpi=%u\n", lpn, cpi);
	
	return VM_FAULT_LOCKED;
}

/**
 * kdb_vma_open - Called when VMA is duplicated (e.g., fork)
 * @vma: The VMA being opened
 */
static void kdb_vma_open(struct vm_area_struct *vma)
{
	struct vma_ctx *ctx = vma->vm_private_data;
	
	pr_debug("kdb: VMA opened: %px\n", ctx);
}

/**
 * kdb_vma_close - Called when VMA is closed
 * @vma: The VMA being closed
 */
static void kdb_vma_close(struct vm_area_struct *vma)
{
	struct vma_ctx *ctx = vma->vm_private_data;
	
	if (ctx) {
		pr_debug("kdb: VMA closed, destroying context: %px\n", ctx);
		vma_ctx_destroy(ctx);
		vma->vm_private_data = NULL;
	}
}

/* VM operations structure */
const struct vm_operations_struct kdb_vm_ops = {
	.open = kdb_vma_open,
	.close = kdb_vma_close,
	.fault = kdb_fault,
	.page_mkwrite = kdb_page_mkwrite,
};