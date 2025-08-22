# Linux Block Driver with io_uring-first User Interface

## 1. Scope & Goals

**Goal:** Define a Linux block driver whose user-facing fast path is io_uring. The device exposes a standard `/dev/<name>` block node and fully integrates with the block layer (blk-mq), while offering a modern, low-overhead, polling-capable I/O path and driver-specific admin commands via `IORING_OP_URING_CMD`.

### Primary use cases
- High-throughput, low-latency sequential and random I/O (databases, log devices, caches)
- Latency-sensitive polling (users with `IORING_SETUP_IOPOLL`)
- Driver-specific admin/control commands (identify, get geometry, zones, firmware ops) without ioctl

### Non-Goals
- Networked block transport definition (iSCSI/NBD specifics are out of scope)
- Filesystem semantics beyond what the block layer already provides

---

## 2. Device Model

- **Type:** Block device (major/minor allocated dynamically)
- **Node:** `/dev/<name>` (optionally with partitions via `add_disk()`)
- **Sysfs:** `/sys/block/<name>/…` for queue limits, features, stats
- **Capabilities:**
  - Logical sector size: 512–4096 bytes (discoverable)
  - Physical block size: ≥ logical sector size (discoverable)
  - Max I/O size, max segments, max segment size, DMA alignment (discoverable)
  - Optional: Write cache, FUA, FLUSH, Discard/Trim, Write Zeroes
  - Optional: Zoned block device (conventional or host-managed)

---

## 3. Architecture Overview

### 3.1 Data Plane (blk-mq)
- **Tag set:** One blk-mq tag set, N hardware queues (HWQs), mapped to CPU sets
- **Queue count:** tunable via module param/sysfs (`nr_hw_queues`, default = num online CPUs or per NUMA node)
- **Request flow:** `submit_bio` → blk-mq request allocation → per-queue driver dispatch → device DMA → completion (`blk_mq_complete_request`)
- **Completion path:** Lockless (per-queue). Support for both interrupt-driven and polled completion

### 3.2 Polling
- Implement `blk_poll()` so processes using io_uring rings created with `IORING_SETUP_IOPOLL` can poll for completions without interrupts
- **Poll granularity:** per-HWQ; bounded busy-wait with backoff (driver internal)

### 3.3 DMA & Memory
- Use DMA-mapping API with IOMMU awareness
- Enforce queue/segment limits to avoid IOMMU faults
- Bounce/copy only when required (unaligned or non-DMA-safe buffers); otherwise zero-copy from user pages pinned by io_uring registered buffers

---

## 4. io_uring Semantics

### 4.1 Data I/O ops

Applications use standard I/O opcodes on the block file descriptor:
- `IORING_OP_READ` / `IORING_OP_READV` / `IORING_OP_READ_FIXED`
- `IORING_OP_WRITE` / `IORING_OP_WRITEV` / `IORING_OP_WRITE_FIXED`
- `IORING_OP_FSYNC` (maps to `REQ_OP_FLUSH` if supported; else `EOPNOTSUPP`)

#### Required Support
- Direct I/O path for all io_uring reads/writes. Buffered is allowed but not the fast path; recommend `O_DIRECT`
- Fixed buffers (`IORING_REGISTER_BUFFERS`) encouraged for lowest overhead
- File table registration (`IORING_REGISTER_FILES`) optional optimization

#### Nowait behavior
- If the driver cannot allocate a request without sleeping (e.g., queue full) and the submission is flagged `NOWAIT`, return `-EAGAIN` promptly; io_uring may defer or the app can retry

#### Ordering
- **Default:** Block layer provides per-request completion ordering; no global ordering guarantee
- Use `IOSQE_IO_LINK` or `IOSQE_DRAIN` for app-defined sequencing
- FUA and FLUSH ensure media order (see 6.3)

### 4.2 Polling rings
When users create rings with `IORING_SETUP_IOPOLL`, driver must:
- Avoid completion IRQs for those requests (poll-only completion)
- Provide progress via `blk_poll()` until request completes
- **Constraint:** iopoll is only defined for direct I/O; enforce or fail with `-EOPNOTSUPP`

### 4.3 Driver Admin via URING_CMD

Use `IORING_OP_URING_CMD` for control-plane commands to avoid ioctl. Define a compact ABI:

```c
enum myblk_ucmd {
  MYBLK_UCMD_IDENTIFY      = 0x01, // returns identify struct
  MYBLK_UCMD_GET_LIMITS    = 0x02, // queue/seg limits
  MYBLK_UCMD_GET_FEATURES  = 0x03, // bitmap
  MYBLK_UCMD_SET_FEATURES  = 0x04, // bitmap mask
  MYBLK_UCMD_GET_GEOMETRY  = 0x05, // capacity, lba size, pbs
  MYBLK_UCMD_GET_STATS     = 0x06, // iostat snapshot
  MYBLK_UCMD_ZONE_MGMT     = 0x10, // sub-op: reset/open/finish
  MYBLK_UCMD_FIRMWARE_OP   = 0x20, // stage/commit, optional
};
```

- **Submission buffer layout** (user→kernel): header + opcode + payload (packed, little-endian)
- **Completion:** `cqe->res` = >=0 payload length or feature value; negative `-errno` on failure. Optional `cqe->flags` bits for out-of-band info (e.g., zone append LBA returned as res)

#### Versioning
- Include `u16 abi_major`, `u16 abi_minor` in IDENTIFY and reject unknown major

---

## 5. Queue Limits & Discoverability

The following are surfaced via sysfs and via `GET_LIMITS`:
- `max_hw_sectors_kb`
- `max_sectors_kb`
- `nr_hw_queues`
- `queue_depth` (per HWQ)
- `max_segments`, `max_segment_size`
- `dma_alignment`, `io_min`, `io_opt`
- `discard_granularity`, `discard_max_bytes` (if supported)

### Default targets (non-binding; tuneable):
- `queue_depth`: 1024 per HWQ
- `max_sectors_kb`: 4096
- `max_segments`: 128
- `dma_alignment`: 4096 bytes

---

## 6. Command Set Mapping

### 6.1 Read/Write
- **Alignment:** Reads/writes must be multiple of logical sector size; unaligned → `-EINVAL`
- **Max I/O size:** Enforced per queue limits. Split by core if larger

### 6.2 Discard & Write Zeroes
- **Discard/Trim:** supported via `BLKDISCARD` ioctl, and optionally via `URING_CMD` sub-ops for batch/async management
- **Write Zeroes:** set `REQ_OP_WRITE_ZEROES` for zero-fill. If unsupported, fallback to explicit write from driver zero-page (may be slower)

### 6.3 Flush & FUA
- **Flush:** `IORING_OP_FSYNC` → `REQ_OP_FLUSH` (persist volatile write cache)
- **FUA:** Writes with `REQ_FUA` if user requests `O_DSYNC` semantics; ensure write reaches stable media before completion
- **Barriers:** `IOSQE_IO_LINK` + `FSYNC` gives a simple write-then-flush chain

### 6.4 Zoned Block (optional)
- **Types:** Conventional or host-managed
- **Ops:** Zone report (identify or `URING_CMD`), zone reset/open/finish (zone mgmt sub-ops)
- **Zone Append:** Provide `URING_CMD_ZONE_APPEND` or embed as WRITE with special flag; on success, return appended LBA in `cqe->res`

---

## 7. Error Model & Status

### Completion codes (negative `-errno` in `cqe->res`):
- `-EINVAL` (misaligned, out of range, bad opcode)
- `-EOPNOTSUPP` (feature not supported)
- `-EAGAIN` (NOWAIT resource pressure)
- `-ENOSPC` (no space / zone full)
- `-EIO` (device error)
- `-EPROTO` (malformed URING_CMD)
- `-EFAULT` (bad userspace pointer for non-fixed buffers)
- `-EOVERFLOW` (payload too small/large)

**Partial completions:** Not used for block data I/O; ops are all-or-nothing per request. Split at submit time if needed.

**Retries:** Driver may internally retry transient media errors; ensure eventual completion or `-EIO`.

---

## 8. Performance / Scaling Requirements

### Targets (indicative, not normative):
- **Latency:** p50 ≤ 20µs (iopoll) for 4K read on a warmed queue; p99 ≤ 200µs under moderate load
- **Throughput:** ≥ 1M IOPS (4K) on a 16-core host with registered buffers and iopoll, assuming capable hardware
- **CPU:** ≤ 1.5 CPU cycles/byte on large sequential I/O with vector-gather

### Mechanics:
- Zero-copy fast path with registered buffers (`FOLL_LONGTERM`) and scatter-gather DMA
- Per-CPU submission and completion with minimal cross-CPU contention; avoid global locks
- Batched submit/complete: encourage apps to batch SQEs/CQEs (e.g., `SQPOLL`, `DEFER_TASKRUN`, `COOP_TASKRUN` friendly)

---

## 9. Resource Management

### 9.1 Memory Pinning
- Registered buffers are long-term pinned; reject registration if the system cannot satisfy (return `-ENOMEM`)
- Document potential interaction with CMA/HUGEPAGEs; recommend reasonable buffer sizes and deregistration on idle

### 9.2 Queue Depth & Backpressure
- Expose per-queue depth in sysfs; enforce cap
- When full and NOWAIT: `-EAGAIN`. Otherwise sleep until tags free

### 9.3 NUMA
- Allocate per-queue structures and DMA resources from local NUMA node
- Optional: expose `queue_numa_node` and allow admin to remap queues

---

## 10. Reliability & Ordering
- **Crash consistency:** Respect block layer flush/FUA semantics
- **Write cache:** Advertise `write_cache` mode (write-back/write-through)
- **Power loss:** If device has volatile caches, flush on suspend/shutdown
- **Barriers:** Applications needing strict ordering should use LINK + FSYNC or FUA writes

---

## 11. Telemetry & Introspection

### 11.1 Sysfs
- `/sys/block/<name>/queue/*` (limits), `/sys/block/<name>/stat` (iostat)
- Driver-specific attributes under `/sys/block/<name>/myblk/*`:
  - `features`, `firmware_rev`, `controller_temp`, `nr_hw_queues`, `poll_enabled`

### 11.2 URING_CMD GET_STATS
Return snapshot of:
- Completed read/write ops, sectors, latency percentiles (driver histograms), queue full events, retries, media errors

---

## 12. Security & Isolation
- Honor Linux capabilities for privileged admin commands (e.g., firmware ops require `CAP_SYS_ADMIN`)
- Validate all `URING_CMD` payload lengths and copies; never trust user lengths
- Enforce DMA-safe buffers; with IOMMU, set correct domains; without, require alignment/segment limits or bounce

---

## 13. Power Management
- **Suspend/Resume:** Quiesce queues; complete in-flight requests with `-EIO` or resume after restore if device supports it
- **Runtime PM:** Optional; idle HWQs can gate clocks or place device in low-power state; wake on first new submission

---

## 14. Testing & Compliance

### Kernel selftests & kunit
- Submit/read/write/flush with and without fixed buffers
- NOWAIT under queue pressure
- iopoll correctness (no IRQ completions)
- Error injection: media error, ENOSPC, malformed URING_CMD
- Zoned behavior (if enabled): append position correctness

### Userspace tests
- fio with io_uring engine: random/sequential RW, iopoll on/off, buffer registration on/off, depth sweep
- Admin path: identification, limits, stats; negative tests

---

## 15. Minimal Userspace Examples

### 15.1 Fast 4K reads with iopoll and registered buffers (sketch)

```c
// open(O_DIRECT), setup ring with IORING_SETUP_IOPOLL, register buffers & file
// submit N READ_FIXED SQEs with aligned 4K offsets
// busy-poll cqes; on completion check cqe->res == 4096
// optional: link FSYNC after a write batch
```

### 15.2 Admin via URING_CMD (identify)

```c
// Prepare a small struct { abi_major, abi_minor, ... } as output buffer
// SQE: IORING_OP_URING_CMD with cmd = MYBLK_UCMD_IDENTIFY
// cqe->res >= sizeof(identify) on success; parse and verify versions
```

---

## 16. Driver Internal Interfaces (Kernel)
- **Probe:** allocate gendisk, init blk-mq tagset & queues, set queue limits, register disk
- **Queue ops:** `->queue_rq`, optional `->poll` implementation, `->init_hctx`/`->exit_hctx`
- **Limits:** set via `blk_queue_*` helpers (`logical_block_size`, `max_hw_sectors`, `max_segments`, `dma_alignment`, `discard`)
- **Flush:** implement `->prepare_flush`/`->post_flush` (or equivalent) if hardware supports volatile caches
- **Zoned:** set zoned model via `blk_queue_set_zoned`; implement zone mgmt via driver hooks
- **URING_CMD:** implement `->uring_cmd()` (or `->uring_cmd_iopoll()` if supported) to parse opcodes and fill result buffers

---

## 17. Compatibility & Fallbacks
- Works with plain read/write/io_submit as normal block device (io_uring is the preferred path, not the only one)
- If iopoll not supported in hardware/driver, return `-EOPNOTSUPP` on polling rings and complete via IRQ path
- If `URING_CMD` is unavailable on the running kernel, provide equivalent ioctl as a compatibility layer (optional)

---

## 18. Configuration Knobs (Module/Sysfs)
- `nr_hw_queues` (int)
- `queue_depth` (int)
- `enable_poll` (bool)
- `enable_discard` (bool)
- `write_cache` (enum: wb/wt)
- `zoned_mode` (enum: none/conventional/host-managed)

Each change that affects queue topology requires queue quiesce and reinit.

---

## 19. Documentation & ABI Stability
- Ship `Documentation/block/<name>.rst` with:
  - io_uring usage guide and examples
  - URING_CMD ABI (structures, versioning rules)
  - Sysfs attributes reference
- **ABI promises:**
  - URING_CMD major version changes only for breaking changes (reject unknown major)
  - Minor version adds fields in tail-extensible structs; preserve offsets

---

## 20. Reference Performance Guidance (for Users)
- Use `O_DIRECT`, fixed buffers, and `SQPOLL` for lowest CPU overhead
- Batch submissions (depth 64–256) to saturate queues
- Prefer aligned 4K+ I/O matching `io_opt` for sequential workloads
- For strict durability: link writes with FSYNC or use FUA writes and measure cost

---

## Appendix A: Example URING_CMD Structures (packed)

```c
struct myblk_ucmd_hdr {
  __u16 abi_major;
  __u16 abi_minor;
  __u16 opcode;    // myblk_ucmd
  __u16 flags;     // reserved
  __u32 payload_len;
  // followed by payload in/out
};

struct myblk_identify {
  __u8  model[40];
  __u8  firmware[16];
  __u32 logical_block_size;
  __u32 physical_block_size;
  __u64 capacity_sectors;
  __u64 features_bitmap; // see GET_FEATURES
  __u32 queue_count;
  __u32 queue_depth;
};
```

This specification provides everything a kernel implementer needs to wire the device to blk-mq with an io_uring-first contract, and everything an application developer needs to drive high-performance I/O (with optional polling) and call driver administration without falling back to legacy ioctls.