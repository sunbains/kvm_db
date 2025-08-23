#ifndef _UAPI_KDB_H
#define _UAPI_KDB_H

#include <linux/types.h>
#include <linux/ioctl.h>

/* Device name */
#define KDB_DEV_NAME "kdbcache"

/* Layout configuration structure */
struct kdb_layout {
	__u64 cp_size;    /* Canonical page size (bytes) */
	__u64 lp_size;    /* Logical page size (bytes) */
	__u64 n_lpn;      /* Number of logical page numbers */
};

/* Statistics structure */
struct kdb_stats {
	__u64 total_faults;     /* Total page faults handled */
	__u64 total_mkwrite;    /* Total page_mkwrite calls */
	__u64 total_cp_alloc;   /* Total canonical pages allocated */
	__u64 total_lp_created; /* Total logical pages created */
	__u64 dirty_pages;      /* Currently dirty pages */
	__u64 allocated_cp;     /* Currently allocated canonical pages */
	__u64 allocated_lp;     /* Currently allocated logical pages */
};

/* IOCTL definitions */
#define KDB_MAGIC 'k'
#define KDB_SET_LAYOUT   _IOW(KDB_MAGIC, 1, struct kdb_layout)
#define KDB_GET_LAYOUT   _IOR(KDB_MAGIC, 2, struct kdb_layout)
#define KDB_GET_STATS    _IOR(KDB_MAGIC, 3, struct kdb_stats)
#define KDB_RESET_STATS  _IO(KDB_MAGIC, 4)

#endif /* _UAPI_KDB_H */