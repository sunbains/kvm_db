# KVM DB

Modern Linux-native storage fabric combining Oracle ASM + Oracle Buffer Cache + Exadata Smart FlashCache into a page-fault-driven, compressed, COW, shared-memory buffer cache.

## Overview

"ASM-on-steroids" — a modern storage fabric that combines page-cache semantics with COW block management and compression.

## Conceptual Positioning

### Oracle ASM (baseline)
- Presents logical volumes out of raw disks
- Stripes/mirrors data, manages disk groups  
- DB buffer cache sits above it in SGA; ASM itself is not a cache
- I/O is synchronous block I/O (8K pages typical)

### Modernized ASM-on-Steroids (proposed)
- Merges ASM's logical volume mgmt with the buffer cache
- Unified logical page model: DB frontend maps LPNs directly into VA space
- Page-fault driven cache = the residency map disappears (MMU is the directory)
- COW everywhere: every update allocates a new compressed physical block
- Compression and checksums are built in
- Journaled LPN→PPN mapping ensures crash consistency
- Transparent persistence & recovery: WAL integration drives durability
- NUMA-aware, 4K canonical page granularity for modern CPUs and storage stacks
- Integration with io_uring: efficient async I/O to backing devices

## Key Improvements Over Classic ASM

| Area | ASM (2003) | ASM-on-Steroids |
|------|------------|-----------------|
| Page access | Block I/O via syscalls | Memory-mapped VA space, faults trigger fills |
| Cache | In DB SGA only | Unified buffer cache in enclave, shared memory, page-fault driven |
| Write policy | In-place or mirrored | Always COW; atomic LPN→PPN flip |
| Compression | N/A | Built-in, transparent per LPN |
| Metadata | Extent maps | Journaled LPN→PPN map |
| Residency | Tracked in buffer cache | MMU PTE = residency; no separate map |
| Fault tolerance | Mirrors, rebalance | Mirrors + COW snapshots + GC |
| Integration | Kernel driver + DB processes | Kernel enclave + DB frontend shared VA |

## Architecture Layers

### Database Frontend
- Memory-maps sparse VA region: `addr = base + LPN*LP_SIZE`
- Accessing VA faults into enclave
- Issues WAL flushes to coordinate durability

### Enclave Buffer Cache
- Owns CP pools (`struct page*`, 4K)
- Per-LPN state: locks, dirty bitmap, WAL LSN, CP pointers
- Fault handler: on demand fetch/decompress, installs CP into PTE
- Write path: COW new CPs → compress → write → journal map flip

### Persistent Storage
- Backing device(s) managed like ASM disks
- Data is segmented (like ASM extents)
- Journaled map: LPN→PPN records
- GC/compaction similar to LSM or log-structured FS

## Extended Features
- **Mirroring/RAID-like**: multiple PPNs per LPN, write all then flip pointer
- **Striping**: spread CBs across devices/zones; IO path parallelizes
- **Snapshots/clones**: trivial with COW; multiple LPN maps can point to same CBs
- **Zoned devices**: zones = log segments; resets reclaim space
- **Cloud-scale**: LPN→PPN map can be distributed/replicated across nodes (future extension)
- **Telemetry/IO hints**: admin can query latency histograms, space usage, live/dead block ratios

## Key Innovations vs Oracle ASM
- **MMU-integrated cache**: no need to check a buffer cache hash — a PTE miss is the miss
- **Unified durability semantics**: WAL flush coordinates both log and COW map flips
- **Zero-copy**: DB threads read/write directly in mapped VA; no extra buffer copies
- **Compression & checksums**: native, per-page
- **COW journaled metadata**: no partial writes; always safe pointer flips
- **NUMA-aware, io_uring-driven**: modern Linux concurrency, no global locks

## Analogy

Think of it as:
- ZFS (COW, compression, checksums) +
- Linux page cache (fault-driven) +
- Oracle ASM (logical→physical disk mapping) +  
- Exadata flash cache (transparent, eviction, prefetch)

→ unified into one subsystem where the page fault itself is the cache lookup.

## Benefits
- **Simplicity**: no explicit buffer cache mgmt; VA access is the API
- **Performance**: 4K granularity aligns with modern SSD/NVMe, reduces write amp
- **Safety**: journaled map + COW = crash consistency without torn writes
- **Scalability**: NUMA sharding, io_uring queues scale with cores
- **Extensibility**: replication, snapshots, compression policies are orthogonal

## Summary

This "ASM-on-steroids" system is a modern buffer cache + storage manager hybrid. It replaces ASM's extent map + DB buffer cache with:
- Memory-mapped LPN space
- Page-fault driven cache fills
- COW compressed persistence  
- Journaled LPN→PPN metadata

The MMU is leveraged as the cache directory, eliminating one of the most complex data structures in traditional RDBMS buffer caches.

