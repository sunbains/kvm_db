# KVM Database Buffer Cache System - Requirements Specification

## 1. Overview

### 1.1 Purpose
This document specifies the requirements for a high-performance database buffer cache system that utilizes KVM (Kernel-based Virtual Machine) to create an isolated buffer cache enclave with zero-copy I/O capabilities.

### 1.2 Scope
The system consists of three main components:
- Host database frontend for query/transaction processing
- KVM-based buffer cache enclave for cache management
- Host I/O proxy for zero-copy disk operations

## 2. System Architecture Requirements

### 2.1 High-Level Components

#### 2.1.1 Host DB Frontend
- **REQ-HDB-001**: The host database SHALL execute query and transaction logic
- **REQ-HDB-002**: The host database SHALL request and return cache pages via shared-memory API
- **REQ-HDB-003**: The host database SHALL NOT perform direct disk I/O system calls
- **REQ-HDB-004**: The host database SHALL access cached pages directly through shared memory

#### 2.1.2 Buffer Cache VM Enclave  
- **REQ-BCE-001**: The buffer cache enclave SHALL run as a micro-VM with minimal code footprint
- **REQ-BCE-002**: The enclave SHALL manage cache hit/miss decisions
- **REQ-BCE-003**: The enclave SHALL implement eviction policies
- **REQ-BCE-004**: The enclave SHALL handle prefetching logic
- **REQ-BCE-005**: The enclave SHALL support compression and checksums (optional)
- **REQ-BCE-006**: The enclave SHALL enforce durability policies

#### 2.1.3 Host I/O Proxy
- **REQ-HIP-001**: The I/O proxy SHALL run as a thread in the host process
- **REQ-HIP-002**: The I/O proxy SHALL own an io_uring instance to /dev/ublkbN
- **REQ-HIP-003**: The I/O proxy SHALL submit READ/WRITE/FSYNC operations on behalf of the enclave
- **REQ-HIP-004**: The I/O proxy SHALL target shared memory directly using registered buffers
- **REQ-HIP-005**: The I/O proxy SHALL achieve true zero-copy I/O operations

## 3. Shared Memory Requirements

### 3.1 Memory Region Structure
- **REQ-SMR-001**: The system SHALL use a single memfd or hugetlbfs region
- **REQ-SMR-002**: The region SHALL be mapped into both host and guest address spaces
- **REQ-SMR-003**: The region SHALL be exported to guest using KVM_SET_USER_MEMORY_REGION

### 3.2 Memory Layout
- **REQ-SML-001**: The shared memory SHALL contain a control page with doorbells, free lists, and version/epochs
- **REQ-SML-002**: The shared memory SHALL contain request rings for DB→enclave descriptors
- **REQ-SML-003**: The shared memory SHALL contain completion rings for enclave→DB descriptors  
- **REQ-SML-004**: The shared memory SHALL contain a page table array for page metadata
- **REQ-SML-005**: The shared memory SHALL contain data pages of configurable size (4K-16K typical)

## 4. Communication Requirements

### 4.1 Notification Mechanisms
- **REQ-NOT-001**: DB→enclave notifications SHALL use ioeventfd/doorbell or futex in shared memory
- **REQ-NOT-002**: Enclave→DB notifications SHALL use irqfd or eventfd to wake waiters
- **REQ-NOT-003**: The system SHALL support batched notifications to avoid interrupt storms

### 4.2 Ring Buffer Protocol
- **REQ-RBP-001**: The system SHALL use single-producer/single-consumer rings
- **REQ-RBP-002**: Each ring entry SHALL have a sequence counter
- **REQ-RBP-003**: Producers and consumers SHALL use memory barriers for ordering
- **REQ-RBP-004**: Ring sizes SHALL be power-of-two with head/tail/seq and cache-line padding

## 5. Cache API Requirements

### 5.1 Request Structure
- **REQ-REQ-001**: Request entries SHALL contain file_id (64-bit) for tablespace/file identification
- **REQ-REQ-002**: Request entries SHALL contain page_no (64-bit) for logical page number
- **REQ-REQ-003**: Request entries SHALL contain mode flags (SHARED|EXCL, READ|WRITE, PREFETCH, NOWAIT)
- **REQ-REQ-004**: Request entries SHALL contain feature flags (CHECKSUM, COMPRESS_OK, HOT_HINT)
- **REQ-REQ-005**: Request entries SHALL contain want_lsn (64-bit) for read consistency
- **REQ-REQ-006**: Request entries SHALL contain ctx (64-bit) opaque pointer for completion matching

### 5.2 Completion Structure
- **REQ-CPL-001**: Completion entries SHALL contain ctx for request matching
- **REQ-CPL-002**: Completion entries SHALL contain status codes (0=ok, -EAGAIN=miss, -EIO=error)
- **REQ-CPL-003**: Completion entries SHALL contain page_idx into shared data pages pool
- **REQ-CPL-004**: Completion entries SHALL contain page_gen for staleness validation
- **REQ-CPL-005**: Completion entries SHALL contain page_lsn for flush LSN tracking
- **REQ-CPL-006**: Completion entries SHALL contain lease token for page release
- **REQ-CPL-007**: Completion entries SHALL contain len for variable-sized pages

### 5.3 Lease Management
- **REQ-LSE-001**: The system SHALL implement a lease-based page access model
- **REQ-LSE-002**: DB SHALL receive (page_idx, page_gen, lease) triples for page access
- **REQ-LSE-003**: DB SHALL call Put(lease) to release page pins
- **REQ-LSE-004**: DB SHALL call MarkDirty(lease, new_lsn) before Put for modified pages

## 6. Page State Management Requirements

### 6.1 Page States
- **REQ-PST-001**: Pages SHALL support states: FREE, CLEAN, DIRTY, WRITEBACK, LOCKED
- **REQ-PST-002**: Page metadata SHALL track pin counts for eviction prevention
- **REQ-PST-003**: Page metadata SHALL track last access time/class and hotness counters

## 7. I/O Flow Requirements

### 7.1 Read Operations
- **REQ-RDO-001**: On read miss, enclave SHALL hash (file_id, page_no) to determine cache status
- **REQ-RDO-002**: Enclave SHALL choose victim pages respecting pin counts and dirty state
- **REQ-RDO-003**: Enclave SHALL post READ operations to I/O proxy with iov pointing to DataPages[page_idx]
- **REQ-RDO-004**: System SHALL use registered buffers for zero-copy operations
- **REQ-RDO-005**: Enclave SHALL verify checksums (optional) before completion
- **REQ-RDO-006**: Enclave SHALL post completion with (page_idx, lease) to DB

### 7.2 Write Operations  
- **REQ-WRO-001**: DB SHALL mutate pages in-place in shared memory when holding EXCL lease
- **REQ-WRO-002**: DB SHALL post MarkDirty(lease, lsn) to transition pages to DIRTY state
- **REQ-WRO-003**: Enclave SHALL implement WAL-first discipline for writeback
- **REQ-WRO-004**: Enclave SHALL NOT persist pages with LSN > stable_wal_lsn
- **REQ-WRO-005**: Enclave SHALL schedule writeback after AdvanceStableWAL(lsn) calls
- **REQ-WRO-006**: Writeback SHALL use WRITE (optionally FUA) and FSYNC batching

## 8. Durability Requirements

### 8.1 WAL Integration
- **REQ-DUR-001**: DB SHALL control WAL separately from data page durability
- **REQ-DUR-002**: Enclave SHALL expose FlushToLSN(lsn) operation
- **REQ-DUR-003**: FlushToLSN SHALL complete only when WAL is durable to lsn AND all pages with page_lsn ≤ lsn are persisted
- **REQ-DUR-004**: System SHALL support media-ordered persistence (FSYNC or FUA)
- **REQ-DUR-005**: System SHALL support linking batched WRITEs with terminal FSYNC

## 9. Performance Requirements

### 9.1 NUMA Support
- **REQ-NUM-001**: System SHALL allocate one shared region per NUMA node
- **REQ-NUM-002**: System SHALL provide one cache queue per shared region
- **REQ-NUM-003**: System SHALL pin one vCPU per cache queue to local NUMA node
- **REQ-NUM-004**: I/O proxy threads SHALL be colocated with corresponding vCPUs
- **REQ-NUM-005**: System SHALL use dedicated io_uring per queue

### 9.2 Memory Optimization
- **REQ-MEM-001**: System SHOULD use hugepages (2M/1G) to reduce TLB pressure
- **REQ-MEM-002**: Data pages SHOULD be aligned to hugepage boundaries
- **REQ-MEM-003**: System SHALL register data pages with io_uring using FOLL_LONGTERM

### 9.3 Batching and Throughput
- **REQ-BAT-001**: System SHALL support batched fill/completion operations
- **REQ-BAT-002**: System SHALL support batched io_uring_enter calls
- **REQ-BAT-003**: System SHOULD support SQPOLL mode for io_uring
- **REQ-BAT-004**: Enclave SHALL support speculative prefetch reads
- **REQ-BAT-005**: System SHALL implement backpressure when queue depth is low

## 10. Optional Feature Requirements

### 10.1 Compression
- **REQ-CMP-001**: System MAY support page compression in the enclave
- **REQ-CMP-002**: Compressed pages SHALL be decompressed before handout to DB
- **REQ-CMP-003**: System MAY support on-demand inflation for memory savings

### 10.2 Checksums
- **REQ-CHK-001**: System MAY validate checksums on page fill
- **REQ-CHK-002**: System MAY recompute checksums on writeback
- **REQ-CHK-003**: Per-page checksum/format MAY be stored in Page Table

## 11. Failure and Recovery Requirements

### 11.1 DB Crash Recovery
- **REQ-DBR-001**: Enclave SHALL detect DB inactivity
- **REQ-DBR-002**: Enclave SHALL revoke all leases after grace period on DB crash
- **REQ-DBR-003**: Shared memory SHALL remain intact for potential reuse

### 11.2 Enclave Crash Recovery
- **REQ-ECR-001**: Host SHALL detect vCPU exit on enclave crash  
- **REQ-ECR-002**: System SHALL invalidate all leases by bumping generation epoch
- **REQ-ECR-003**: DB SHALL see mismatched page_gen and re-request on next use
- **REQ-ECR-004**: Data in shared memory SHALL be considered unsafe until revalidated

### 11.3 I/O Proxy Failure Recovery
- **REQ-IPR-001**: System SHALL detect I/O proxy failure
- **REQ-IPR-002**: System SHALL fail inflight operations with -EIO  
- **REQ-IPR-003**: System SHALL support io_uring teardown and reinitialization
- **REQ-IPR-004**: Cache SHALL remain valid but cold after I/O proxy restart

## 12. Security Requirements

### 12.1 Isolation
- **REQ-SEC-001**: Guest SHALL be unprivileged with limited write access to owned shared region
- **REQ-SEC-002**: System MAY split region into RO data pages and RW metadata using separate KVM memory slots
- **REQ-SEC-003**: System SHALL validate all ring indices in both directions
- **REQ-SEC-004**: System SHALL validate all lease tokens in both directions

## 13. API Requirements

### 13.1 Database API
- **REQ-API-001**: System SHALL provide get_page(file_id, page_no, mode, want_lsn) → handle {ptr, lease, gen}
- **REQ-API-002**: System SHALL provide mark_dirty(handle, lsn)
- **REQ-API-003**: System SHALL provide put(handle)  
- **REQ-API-004**: System SHALL provide flush_to_lsn(lsn)
- **REQ-API-005**: All operations SHALL have non-blocking variants returning EAGAIN if NOWAIT

### 13.2 Eviction and Revocation
- **REQ-EVI-001**: Enclave SHALL support pushing REVOKE(lease) messages
- **REQ-EVI-002**: DB SHALL respond with PUT(lease) if not pinned or NACK if actively used
- **REQ-EVI-003**: Enclave SHALL retry revocation with deadlines to avoid livelock
- **REQ-EVI-004**: System SHALL reserve emergency pool for new misses during revocation

## 14. Implementation Requirements

### 14.1 Shared Region Setup
- **REQ-IMP-001**: System SHALL use memfd_create and ftruncate for region allocation
- **REQ-IMP-002**: System SHALL mmap region into host and carve subregions
- **REQ-IMP-003**: System SHALL register data pages with io_uring_register_buffers

### 14.2 KVM Setup  
- **REQ-IMP-004**: System SHALL create VM and map memfd via KVM_SET_USER_MEMORY_REGION
- **REQ-IMP-005**: System SHALL start 1 vCPU per cache queue
- **REQ-IMP-006**: Each vCPU SHALL run minimal polling loop

### 14.3 I/O Proxy Setup
- **REQ-IMP-007**: System SHALL create one io_uring per queue with depth 256-1024
- **REQ-IMP-008**: System SHALL build iovecs directly to DataPages[page_idx] offsets
- **REQ-IMP-009**: System SHALL use FIXED buffers and optionally IOPOLL for performance

## 15. Quality Attributes

### 15.1 Performance Targets
- **REQ-PER-001**: System SHALL achieve zero-copy reads/writes between DB and cache
- **REQ-PER-002**: System SHALL provide deterministic CPU isolation for cache logic  
- **REQ-PER-003**: System SHALL minimize TLB pressure through hugepage usage

### 15.2 Maintainability
- **REQ-MNT-001**: System SHALL provide clean migration path for future kernel driver implementation
- **REQ-MNT-002**: Host/DB API SHALL remain stable across implementation changes

### 15.3 Scalability
- **REQ-SCA-001**: System SHALL support multiple NUMA nodes
- **REQ-SCA-002**: System SHALL support multiple concurrent DB workers per cache queue