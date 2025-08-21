# uringblk: io_uring-first Linux Block Driver Specification

**Version:** 1.0  
**Date:** January 2025  
**Author:** KVM Database Project  

## Table of Contents

1. [Introduction](#introduction)
2. [Scope and Goals](#scope-and-goals)
3. [Architecture Overview](#architecture-overview)
4. [Device Model](#device-model)
5. [io_uring Interface](#io_uring-interface)
6. [Admin Command Interface](#admin-command-interface)
7. [Performance Requirements](#performance-requirements)
8. [Error Handling](#error-handling)
9. [Configuration](#configuration)
10. [Testing and Validation](#testing-and-validation)
11. [Implementation Notes](#implementation-notes)
12. [Future Extensions](#future-extensions)

## 1. Introduction

The uringblk driver is a high-performance Linux kernel block device driver that implements an "io_uring-first" architecture. Unlike traditional block drivers that treat io_uring as an additional interface, uringblk is designed from the ground up to prioritize io_uring operations while maintaining full compatibility with the Linux block layer.

This specification defines the interface, behavior, and implementation requirements for the uringblk driver.

## 2. Scope and Goals

### 2.1 Primary Goals

- **High Performance**: Target ≥1M IOPS with minimal CPU overhead
- **Low Latency**: p50 ≤ 20μs, p99 ≤ 200μs for 4K operations with polling
- **Modern Interface**: io_uring-first design with URING_CMD admin interface
- **Scalability**: Multi-queue architecture with NUMA awareness
- **Compatibility**: Full Linux block layer integration

### 2.2 Primary Use Cases

- High-frequency database transaction logs (WAL)
- Low-latency caching layers
- Stream processing buffers
- High-throughput log ingestion systems
- Performance testing and benchmarking

### 2.3 Non-Goals

- Network block transport protocols (iSCSI, NBD)
- Filesystem-specific optimizations
- Hardware-specific optimizations (implementation uses virtual storage)

## 3. Architecture Overview

### 3.1 System Architecture

```
┌─────────────────┐    ┌─────────────────┐
│   Application   │    │   Application   │
└─────────────────┘    └─────────────────┘
         │                       │
         ▼                       ▼
┌─────────────────┐    ┌─────────────────┐
│   io_uring      │    │   Legacy I/O    │
│   Interface     │    │   (read/write)  │
└─────────────────┘    └─────────────────┘
         │                       │
         └───────────┬───────────┘
                     ▼
         ┌─────────────────────────┐
         │    Linux Block Layer    │
         │      (blk-mq)          │
         └─────────────────────────┘
                     │
         ┌───────────┴───────────┐
         ▼                       ▼
┌─────────────────┐    ┌─────────────────┐
│  Data Plane     │    │  Admin Plane    │
│  (I/O Requests) │    │  (URING_CMD)    │
└─────────────────┘    └─────────────────┘
         │                       │
         └───────────┬───────────┘
                     ▼
         ┌─────────────────────────┐
         │    uringblk Driver      │
         └─────────────────────────┘
                     │
                     ▼
         ┌─────────────────────────┐
         │   Virtual Storage       │
         │   Backend               │
         └─────────────────────────┘
```

### 3.2 Component Overview

**Data Plane Components:**
- **blk-mq Tag Set**: Multi-queue request management
- **Hardware Queues**: Per-CPU I/O queues for scalability
- **Request Processing**: Zero-copy I/O path with DMA support
- **Completion Path**: Interrupt-driven or polling-based completion

**Admin Plane Components:**
- **URING_CMD Handler**: Command processing and validation
- **ABI Management**: Versioned command interface
- **Statistics Engine**: Performance metrics collection
- **Configuration Management**: Runtime parameter control

## 4. Device Model

### 4.1 Device Characteristics

- **Device Type**: Block device with dynamic major number
- **Device Node**: `/dev/uringblk<N>` (e.g., `/dev/uringblk0`)
- **Partitioning**: Supported via standard Linux partitioning
- **Sector Size**: Configurable (512B - 4KB), default 512B
- **Capacity**: Configurable via module parameter, default 1GB

### 4.2 Feature Support

| Feature | Support | Description |
|---------|---------|-------------|
| **Direct I/O** | Required | Zero-copy I/O path |
| **Polling** | Optional | `IORING_SETUP_IOPOLL` support |
| **Fixed Buffers** | Recommended | `IORING_REGISTER_BUFFERS` optimization |
| **Write Cache** | Optional | Configurable write-back/write-through |
| **FUA** | Required | Force Unit Access support |
| **FLUSH** | Required | Cache flush operations |
| **Discard/TRIM** | Optional | Block discard operations |
| **Write Zeroes** | Optional | Efficient zero-fill operations |

### 4.3 Queue Limits

| Parameter | Default | Range | Description |
|-----------|---------|-------|-------------|
| `max_hw_sectors` | 8192 | 1-65536 | Maximum sectors per request |
| `max_segments` | 128 | 1-256 | Maximum scatter-gather segments |
| `max_segment_size` | 1MB | 4KB-4MB | Maximum segment size |
| `queue_depth` | 1024 | 32-4096 | Requests per hardware queue |
| `nr_hw_queues` | 4 | 1-64 | Number of hardware queues |

## 5. io_uring Interface

### 5.1 Supported Operations

#### 5.1.1 Data Operations

```c
// Standard I/O operations
IORING_OP_READ          // Block read operations
IORING_OP_READV         // Vectored read operations  
IORING_OP_READ_FIXED    // Read with registered buffers
IORING_OP_WRITE         // Block write operations
IORING_OP_WRITEV        // Vectored write operations
IORING_OP_WRITE_FIXED   // Write with registered buffers
IORING_OP_FSYNC         // Force cache flush (maps to REQ_OP_FLUSH)
```

#### 5.1.2 Admin Operations

```c
IORING_OP_URING_CMD     // Driver-specific admin commands
```

### 5.2 Ring Setup Requirements

#### 5.2.1 Basic Setup

```c
struct io_uring ring;
int fd = open("/dev/uringblk0", O_RDWR | O_DIRECT);
io_uring_queue_init(queue_depth, &ring, 0);
```

#### 5.2.2 Polling Setup

```c
// Enable polling for lowest latency
io_uring_queue_init(queue_depth, &ring, IORING_SETUP_IOPOLL);
```

#### 5.2.3 Fixed Buffer Setup

```c
// Register buffers for zero-copy I/O
struct iovec iovecs[buffer_count];
// ... initialize iovecs ...
io_uring_register_buffers(&ring, iovecs, buffer_count);
```

### 5.3 Operation Semantics

#### 5.3.1 Alignment Requirements

- **Address Alignment**: All I/O addresses must be aligned to logical sector size
- **Length Alignment**: All I/O lengths must be multiples of logical sector size
- **Buffer Alignment**: Recommended 4KB alignment for optimal performance

#### 5.3.2 Ordering Guarantees

- **Default**: No ordering guarantees between operations
- **IOSQE_IO_LINK**: Sequential execution of linked operations
- **IOSQE_IO_DRAIN**: Wait for all previous operations before execution
- **FUA Operations**: Guaranteed ordering with respect to previous writes

#### 5.3.3 Error Handling

- **Success**: `cqe->res` contains number of bytes transferred
- **Error**: `cqe->res` contains negative errno value
- **Partial I/O**: Not supported - operations are atomic

### 5.4 Polling Interface

#### 5.4.1 Setup Requirements

```c
// Ring must be created with IORING_SETUP_IOPOLL
struct io_uring ring;
io_uring_queue_init(depth, &ring, IORING_SETUP_IOPOLL);

// Only direct I/O operations support polling
int fd = open("/dev/uringblk0", O_RDWR | O_DIRECT);
```

#### 5.4.2 Polling Loop

```c
while (pending_operations > 0) {
    struct io_uring_cqe *cqe;
    int ret = io_uring_peek_cqe(&ring, &cqe);
    if (ret == 0) {
        // Process completion
        handle_completion(cqe);
        io_uring_cqe_seen(&ring, cqe);
        pending_operations--;
    } else {
        // No completions ready, continue polling
        cpu_relax(); // Optional: reduce CPU usage
    }
}
```

## 6. Admin Command Interface

### 6.1 Command Structure

#### 6.1.1 Command Header

```c
struct uringblk_ucmd_hdr {
    __u16 abi_major;        // ABI major version (must match driver)
    __u16 abi_minor;        // ABI minor version  
    __u16 opcode;           // Command opcode
    __u16 flags;            // Command flags (reserved, must be 0)
    __u32 payload_len;      // Length of command payload
    // Command-specific payload follows
} __packed;
```

#### 6.1.2 ABI Versioning

- **Major Version**: Currently 1, incremented for breaking changes
- **Minor Version**: Currently 0, incremented for backward-compatible additions
- **Compatibility**: Driver rejects commands with unknown major version
- **Extension**: New fields added to end of structures for minor version updates

### 6.2 Supported Commands

#### 6.2.1 IDENTIFY (0x01)

**Purpose**: Retrieve device identification and capabilities

**Request**: Header only (no payload)

**Response**: `struct uringblk_identify`
```c
struct uringblk_identify {
    __u8  model[40];              // Device model string
    __u8  firmware[16];           // Firmware version string
    __u32 logical_block_size;     // Logical sector size in bytes
    __u32 physical_block_size;    // Physical sector size in bytes
    __u64 capacity_sectors;       // Total capacity in sectors
    __u64 features_bitmap;        // Supported features (see feature flags)
    __u32 queue_count;            // Number of hardware queues
    __u32 queue_depth;            // Depth of each hardware queue
    __u32 max_segments;           // Maximum scatter-gather segments
    __u32 max_segment_size;       // Maximum segment size in bytes
    __u32 dma_alignment;          // Required DMA alignment
    __u32 io_min;                 // Minimum I/O size
    __u32 io_opt;                 // Optimal I/O size
    __u32 discard_granularity;    // Discard granularity
    __u64 discard_max_bytes;      // Maximum discard size
} __packed;
```

#### 6.2.2 GET_LIMITS (0x02)

**Purpose**: Retrieve current queue and I/O limits

**Request**: Header only

**Response**: `struct uringblk_limits`
```c
struct uringblk_limits {
    __u32 max_hw_sectors_kb;      // Maximum sectors per request (KB)
    __u32 max_sectors_kb;         // Current max sectors limit (KB)
    __u32 nr_hw_queues;           // Number of hardware queues
    __u32 queue_depth;            // Queue depth per hardware queue
    __u32 max_segments;           // Maximum segments per request
    __u32 max_segment_size;       // Maximum segment size
    __u32 dma_alignment;          // DMA alignment requirement
    __u32 io_min;                 // Minimum I/O size
    __u32 io_opt;                 // Optimal I/O size
    __u32 discard_granularity;    // Discard granularity
    __u64 discard_max_bytes;      // Maximum discard size
} __packed;
```

#### 6.2.3 GET_FEATURES (0x03)

**Purpose**: Retrieve current feature bitmap

**Request**: Header only

**Response**: `__u64` feature bitmap

**Feature Flags**:
```c
#define URINGBLK_FEAT_WRITE_CACHE   (1ULL << 0)  // Write cache enabled
#define URINGBLK_FEAT_FUA           (1ULL << 1)  // Force Unit Access
#define URINGBLK_FEAT_FLUSH         (1ULL << 2)  // Cache flush support
#define URINGBLK_FEAT_DISCARD       (1ULL << 3)  // Discard/TRIM support
#define URINGBLK_FEAT_WRITE_ZEROES  (1ULL << 4)  // Write zeroes support
#define URINGBLK_FEAT_ZONED         (1ULL << 5)  // Zoned block device
#define URINGBLK_FEAT_POLLING       (1ULL << 6)  // Polling support
```

#### 6.2.4 GET_GEOMETRY (0x05)

**Purpose**: Retrieve device geometry information

**Request**: Header only

**Response**: `struct uringblk_geometry`
```c
struct uringblk_geometry {
    __u64 capacity_sectors;       // Total capacity in sectors
    __u32 logical_block_size;     // Logical block size
    __u32 physical_block_size;    // Physical block size
    __u16 cylinders;              // Emulated cylinders
    __u8  heads;                  // Emulated heads
    __u8  sectors_per_track;      // Emulated sectors per track
} __packed;
```

#### 6.2.5 GET_STATS (0x06)

**Purpose**: Retrieve device performance statistics

**Request**: Header only

**Response**: `struct uringblk_stats`
```c
struct uringblk_stats {
    __u64 read_ops;               // Total read operations
    __u64 write_ops;              // Total write operations
    __u64 flush_ops;              // Total flush operations
    __u64 discard_ops;            // Total discard operations
    __u64 read_sectors;           // Total sectors read
    __u64 write_sectors;          // Total sectors written
    __u64 read_bytes;             // Total bytes read
    __u64 write_bytes;            // Total bytes written
    __u64 queue_full_events;      // Queue full occurrences
    __u64 media_errors;           // Media error count
    __u64 retries;                // Retry count
    __u32 p50_read_latency_us;    // 50th percentile read latency (μs)
    __u32 p99_read_latency_us;    // 99th percentile read latency (μs)
    __u32 p50_write_latency_us;   // 50th percentile write latency (μs)
    __u32 p99_write_latency_us;   // 99th percentile write latency (μs)
} __packed;
```

### 6.3 Command Submission

#### 6.3.1 Basic Command Submission

```c
// Prepare command buffer
char cmd_buf[sizeof(struct uringblk_ucmd_hdr) + payload_size];
struct uringblk_ucmd_hdr *hdr = (struct uringblk_ucmd_hdr *)cmd_buf;

hdr->abi_major = URINGBLK_ABI_MAJOR;
hdr->abi_minor = URINGBLK_ABI_MINOR;
hdr->opcode = URINGBLK_UCMD_IDENTIFY;
hdr->flags = 0;
hdr->payload_len = payload_size;

// Copy payload if needed
if (payload_size > 0) {
    memcpy(cmd_buf + sizeof(*hdr), payload_data, payload_size);
}

// Submit via io_uring
struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
io_uring_prep_cmd(sqe, fd, 0, 0, 0, cmd_buf, sizeof(cmd_buf));

int ret = io_uring_submit(&ring);
```

#### 6.3.2 Command Completion

```c
struct io_uring_cqe *cqe;
int ret = io_uring_wait_cqe(&ring, &cqe);

if (cqe->res < 0) {
    // Command failed, cqe->res contains -errno
    fprintf(stderr, "Command failed: %s\n", strerror(-cqe->res));
} else {
    // Command succeeded, cqe->res contains response size
    // Response data is in cmd_buf + sizeof(header)
    process_response(cmd_buf + sizeof(struct uringblk_ucmd_hdr), cqe->res);
}

io_uring_cqe_seen(&ring, cqe);
```

## 7. Performance Requirements

### 7.1 Latency Targets

| Operation Type | p50 Target | p99 Target | Conditions |
|----------------|------------|------------|------------|
| 4K Random Read | ≤ 20μs | ≤ 200μs | Polling, warmed cache |
| 4K Random Write | ≤ 30μs | ≤ 300μs | Polling, write-back cache |
| Sequential Read | ≤ 50μs | ≤ 500μs | 64KB I/O, polling |
| Sequential Write | ≤ 100μs | ≤ 1000μs | 64KB I/O, write-back |

### 7.2 Throughput Targets

| Workload | IOPS Target | Bandwidth Target | Conditions |
|----------|-------------|------------------|------------|
| 4K Random Read | ≥ 1M | ≥ 4 GB/s | 16-core, polling, registered buffers |
| 4K Random Write | ≥ 500K | ≥ 2 GB/s | 16-core, polling, write-back |
| 64K Sequential Read | ≥ 100K | ≥ 6 GB/s | Large queues, multiple threads |
| 64K Sequential Write | ≥ 50K | ≥ 3 GB/s | Write-back cache enabled |

### 7.3 CPU Efficiency Targets

- **CPU per IOPS**: ≤ 1.5 CPU cycles per byte transferred
- **CPU per MB/s**: ≤ 5% CPU utilization per GB/s
- **Queue processing**: ≤ 100ns per request in fast path
- **Memory bandwidth**: ≤ 10% of theoretical maximum

### 7.4 Scalability Requirements

- **Queue scaling**: Linear performance scaling up to 16 hardware queues
- **Core scaling**: ≥ 80% efficiency scaling across cores
- **Memory scaling**: Support for ≥ 1GB registered buffer pools
- **NUMA awareness**: Optimal performance on multi-socket systems

## 8. Error Handling

### 8.1 Error Categories

#### 8.1.1 I/O Errors

| Error Code | Condition | Recovery |
|------------|-----------|----------|
| `-EIO` | Media/device error | Application retry or failover |
| `-ENOSPC` | Device full | Application cleanup or resize |
| `-ETIMEDOUT` | Operation timeout | Application retry |
| `-ENOMEM` | Memory allocation failure | Reduce queue depth or wait |

#### 8.1.2 Request Errors

| Error Code | Condition | Recovery |
|------------|-----------|----------|
| `-EINVAL` | Invalid parameters | Fix request parameters |
| `-EOPNOTSUPP` | Unsupported operation | Use alternative operation |
| `-EAGAIN` | Resource temporarily unavailable | Retry operation |
| `-EOVERFLOW` | Request too large | Split request |

#### 8.1.3 Admin Command Errors

| Error Code | Condition | Recovery |
|------------|-----------|----------|
| `-EPROTO` | Malformed command | Fix command format |
| `-EFAULT` | Bad user pointer | Fix memory mapping |
| `-EOVERFLOW` | Payload too large | Reduce payload size |
| `-EINVAL` | Invalid ABI version | Update client or driver |

### 8.2 Error Recovery

#### 8.2.1 Transient Errors

- **Automatic Retry**: Driver may internally retry transient media errors
- **Backoff Strategy**: Exponential backoff for repeated failures
- **Timeout Handling**: Configurable operation timeouts
- **Resource Recovery**: Automatic cleanup of failed operations

#### 8.2.2 Permanent Errors

- **Error Reporting**: Clear error codes in completion queue entries
- **State Preservation**: Device remains operational after errors
- **Graceful Degradation**: Disable failing features while maintaining core functionality
- **Administrative Notification**: Log critical errors to kernel log

### 8.3 Error Injection (Testing)

#### 8.3.1 Supported Error Types

- **Media Errors**: Simulate read/write failures at specific LBAs
- **Timeout Errors**: Inject artificial delays and timeouts
- **Resource Exhaustion**: Simulate memory and queue full conditions
- **Protocol Errors**: Inject malformed commands and responses

#### 8.3.2 Error Injection Interface

Error injection controlled via sysfs attributes under `/sys/block/uringblk0/uringblk/`:

```bash
# Inject read errors at LBA 1000
echo "read,1000,5" > error_inject

# Inject timeout errors (10% probability)
echo "timeout,0,10" > error_inject

# Clear error injection
echo "clear" > error_inject
```

## 9. Configuration

### 9.1 Module Parameters

#### 9.1.1 Performance Parameters

```c
// Number of hardware queues (default: number of online CPUs)
module_param(nr_hw_queues, uint, 0644);

// Queue depth per hardware queue (default: 1024)
module_param(queue_depth, uint, 0644);

// Enable polling support (default: true)
module_param(enable_poll, bool, 0644);
```

#### 9.1.2 Feature Parameters

```c
// Enable discard/TRIM support (default: true)
module_param(enable_discard, bool, 0644);

// Write cache mode: true=write-back, false=write-through (default: true)
module_param(write_cache, bool, 0644);

// Enable write zeroes optimization (default: true)
module_param(enable_write_zeroes, bool, 0644);
```

#### 9.1.3 Device Parameters

```c
// Device capacity in MB (default: 1024)
module_param(capacity_mb, uint, 0644);

// Logical block size in bytes (default: 512)
module_param(logical_block_size, uint, 0444);

// Maximum I/O size in KB (default: 4096)
module_param(max_io_size_kb, uint, 0644);
```

### 9.2 Runtime Configuration

#### 9.2.1 sysfs Attributes

**Device Information** (`/sys/block/uringblk0/uringblk/`):
```bash
model                    # Device model string (RO)
firmware_rev            # Firmware revision (RO)
features                # Feature bitmap (RO)
capacity                # Device capacity in bytes (RO)
```

**Performance Configuration**:
```bash
nr_hw_queues            # Number of hardware queues (RO)
queue_depth             # Queue depth per HW queue (RO)
poll_enabled            # Polling support status (RO)
write_cache             # Write cache mode (RO)
```

**Statistics**:
```bash
read_ops                # Total read operations (RO)
write_ops               # Total write operations (RO)  
read_bytes              # Total bytes read (RO)
write_bytes             # Total bytes written (RO)
flush_ops               # Total flush operations (RO)
discard_ops             # Total discard operations (RO)
queue_full_events       # Queue full occurrences (RO)
media_errors            # Media error count (RO)
```

**Control**:
```bash
stats_reset             # Reset statistics (WO, write "1")
error_inject            # Error injection control (RW)
```

#### 9.2.2 Configuration Examples

```bash
# Load with high-performance settings
sudo insmod uringblk_driver.ko \
    nr_hw_queues=16 \
    queue_depth=2048 \
    capacity_mb=4096 \
    logical_block_size=4096

# Load with low-latency settings
sudo insmod uringblk_driver.ko \
    nr_hw_queues=8 \
    queue_depth=256 \
    enable_poll=1 \
    write_cache=0

# Load for testing
sudo insmod uringblk_driver.ko \
    capacity_mb=100 \
    enable_discard=0 \
    max_io_size_kb=1024
```

### 9.3 Performance Tuning

#### 9.3.1 High IOPS Workloads

```bash
# Optimize for 4K random operations
nr_hw_queues=<num_cores>
queue_depth=1024
enable_poll=1
logical_block_size=4096
write_cache=1
```

#### 9.3.2 Low Latency Workloads

```bash
# Optimize for minimal latency
nr_hw_queues=<num_cores>
queue_depth=64
enable_poll=1
write_cache=0          # Write-through for consistency
max_io_size_kb=256     # Smaller max I/O size
```

#### 9.3.3 High Bandwidth Workloads

```bash
# Optimize for sequential throughput
nr_hw_queues=4         # Fewer queues, deeper
queue_depth=4096
enable_poll=0          # Interrupt-driven for efficiency
max_io_size_kb=4096    # Large I/O sizes
```

## 10. Testing and Validation

### 10.1 Functional Testing

#### 10.1.1 Basic I/O Testing

```bash
# Test basic read/write operations
./uringblk_test -d /dev/uringblk0 --basic

# Test with different I/O sizes
for size in 512 4096 65536; do
    ./uringblk_test -d /dev/uringblk0 --io-size=$size
done

# Test error conditions
./uringblk_test -d /dev/uringblk0 --error-tests
```

#### 10.1.2 Feature Testing

```bash
# Test polling mode
./uringblk_test -d /dev/uringblk0 --poll

# Test fixed buffers
./uringblk_test -d /dev/uringblk0 --fixed-buffers

# Test admin commands
./uringblk_test -d /dev/uringblk0 --admin-commands

# Test flush and FUA
./uringblk_test -d /dev/uringblk0 --sync-tests
```

#### 10.1.3 Stress Testing

```bash
# Long-running stability test
./uringblk_test -d /dev/uringblk0 --duration=3600 --concurrent=16

# Memory pressure test
./uringblk_test -d /dev/uringblk0 --memory-pressure

# Queue full conditions
./uringblk_test -d /dev/uringblk0 --queue-stress
```

### 10.2 Performance Testing

#### 10.2.1 Latency Testing

```bash
# Measure latency distribution
./uringblk_test -d /dev/uringblk0 --latency-test --io-size=4096 --depth=1

# Test polling vs interrupt latency
./uringblk_test -d /dev/uringblk0 --latency-comparison

# Latency under load
./uringblk_test -d /dev/uringblk0 --loaded-latency --background-ios=10000
```

#### 10.2.2 Throughput Testing

```bash
# IOPS testing
./uringblk_test -d /dev/uringblk0 --iops-test --io-size=4096 --depth=64

# Bandwidth testing  
./uringblk_test -d /dev/uringblk0 --bandwidth-test --io-size=65536 --depth=32

# Scaling test
for depth in 1 4 16 64 256 1024; do
    ./uringblk_test -d /dev/uringblk0 --depth=$depth --duration=30
done
```

#### 10.2.3 Comparative Testing

```bash
# Compare with other block devices
./compare_devices.sh /dev/uringblk0 /dev/ram0 /dev/null

# Compare I/O interfaces
./interface_comparison.sh /dev/uringblk0  # io_uring vs sync I/O

# Feature impact testing
./feature_impact.sh /dev/uringblk0        # polling vs interrupt, etc.
```

### 10.3 Compatibility Testing

#### 10.3.1 Kernel Version Testing

Test across supported kernel versions:
- Linux 5.15 LTS (minimum)
- Linux 6.1 LTS 
- Linux 6.6 LTS (current)
- Latest stable kernel

#### 10.3.2 Application Compatibility

```bash
# Standard tools
dd if=/dev/zero of=/dev/uringblk0 bs=4k count=1000
hdparm -t /dev/uringblk0
iostat 1 10

# Filesystem creation
mkfs.ext4 /dev/uringblk0
mount /dev/uringblk0 /mnt/test
bonnie++ -d /mnt/test

# Database simulation
./db_simulator --device=/dev/uringblk0 --workload=oltp
```

#### 10.3.3 Integration Testing

```bash
# systemd integration
systemctl status uringblk
journalctl -u uringblk

# udev rules
udevadm info /dev/uringblk0
udevadm test /sys/block/uringblk0

# Container compatibility
docker run --device=/dev/uringblk0 test-image
```

## 11. Implementation Notes

### 11.1 Kernel API Usage

#### 11.1.1 blk-mq Integration

```c
// Tag set initialization
static struct blk_mq_ops uringblk_mq_ops = {
    .queue_rq = uringblk_queue_rq,
    .init_hctx = uringblk_init_hctx,
    .exit_hctx = uringblk_exit_hctx,
    .poll = uringblk_poll,
};

// Queue limits setup
blk_queue_logical_block_size(queue, logical_block_size);
blk_queue_physical_block_size(queue, physical_block_size);
blk_queue_max_hw_sectors(queue, max_sectors);
blk_queue_max_segments(queue, max_segments);
```

#### 11.1.2 io_uring Integration

```c
// URING_CMD handler
static int uringblk_uring_cmd(struct io_uring_cmd *cmd)
{
    struct uringblk_ucmd_hdr *hdr = cmd->cmd;
    
    // Validate ABI version
    if (hdr->abi_major != URINGBLK_ABI_MAJOR)
        return -EINVAL;
        
    // Process command
    switch (hdr->opcode) {
    case URINGBLK_UCMD_IDENTIFY:
        return handle_identify_cmd(cmd);
    // ... other commands
    }
}

// Polling implementation
static int uringblk_poll(struct blk_mq_hw_ctx *hctx, 
                        struct io_uring_cmd *ioucmd)
{
    // Check for completed requests
    return blk_mq_poll_noretry(hctx, ioucmd);
}
```

### 11.2 Memory Management

#### 11.2.1 DMA Considerations

```c
// Ensure proper DMA alignment
#define URINGBLK_DMA_ALIGN 4096

// Validate user buffers for DMA safety
static bool is_dma_safe_buffer(void __user *buf, size_t len)
{
    unsigned long addr = (unsigned long)buf;
    return (addr & (URINGBLK_DMA_ALIGN - 1)) == 0 &&
           (len & (logical_block_size - 1)) == 0;
}
```

#### 11.2.2 Virtual Storage Backend

```c
// Allocate virtual storage
dev->storage = vzalloc(capacity);
if (!dev->storage)
    return -ENOMEM;

// I/O processing
static void uringblk_process_io(struct request *rq)
{
    loff_t pos = blk_rq_pos(rq) << SECTOR_SHIFT;
    
    rq_for_each_segment(bvec, rq, iter) {
        void *kaddr = kmap_atomic(bvec.bv_page);
        void *buffer = kaddr + bvec.bv_offset;
        
        if (rq_data_dir(rq) == READ)
            memcpy(buffer, dev->storage + pos, bvec.bv_len);
        else
            memcpy(dev->storage + pos, buffer, bvec.bv_len);
            
        kunmap_atomic(kaddr);
        pos += bvec.bv_len;
    }
}
```

### 11.3 Performance Optimization

#### 11.3.1 Lock-Free Fast Path

```c
// Per-queue context to avoid locking
struct uringblk_queue {
    struct uringblk_device *dev;
    unsigned int queue_num;
    atomic_t active_requests;
    // No locks in fast path
};

// Lockless completion
static void uringblk_complete_request(struct request *rq)
{
    struct uringblk_queue *queue = rq->mq_hctx->driver_data;
    
    // Update statistics locklessly
    this_cpu_inc(queue->completions);
    
    blk_mq_end_request(rq, BLK_STS_OK);
}
```

#### 11.3.2 CPU Cache Optimization

```c
// Align hot data structures
struct uringblk_queue {
    // Hot data first
    struct uringblk_device *dev;
    unsigned int queue_num;
    
    // Separate cache lines for different access patterns
    ____cacheline_aligned_in_smp
    atomic64_t read_ops;
    atomic64_t write_ops;
    
    ____cacheline_aligned_in_smp  
    // Cold data last
    struct dentry *debug_dir;
};
```

### 11.4 Error Handling Implementation

#### 11.4.1 Request Error Processing

```c
static blk_status_t uringblk_handle_error(struct request *rq, int error)
{
    struct uringblk_device *dev = rq->q->queuedata;
    
    // Update error statistics
    atomic64_inc(&dev->stats.total_errors);
    
    switch (error) {
    case -ENOMEM:
        atomic64_inc(&dev->stats.memory_errors);
        return BLK_STS_RESOURCE;
    case -EIO:
        atomic64_inc(&dev->stats.media_errors);
        return BLK_STS_IOERR;
    case -ENOSPC:
        return BLK_STS_NOSPC;
    default:
        return BLK_STS_IOERR;
    }
}
```

#### 11.4.2 Admin Command Error Handling

```c
static int uringblk_validate_cmd(struct uringblk_ucmd_hdr *hdr)
{
    if (hdr->abi_major != URINGBLK_ABI_MAJOR)
        return -EINVAL;
        
    if (hdr->payload_len > PAGE_SIZE)
        return -EOVERFLOW;
        
    if (hdr->flags != 0)
        return -EINVAL;
        
    return 0;
}
```

## 12. Future Extensions

### 12.1 Planned Features

#### 12.1.1 Zoned Block Device Support

- **Zone Management**: Reset, Open, Close, Finish operations via URING_CMD
- **Zone Append**: Atomic append operations with LBA return
- **Zone Report**: Zone status and configuration information
- **Write Pointer Management**: Automatic write pointer tracking

#### 12.1.2 Advanced Error Injection

- **Probabilistic Errors**: Configurable error rates by operation type
- **Latency Injection**: Artificial delays for testing timeout handling  
- **Partial Failures**: Simulate partial I/O completion scenarios
- **Recovery Testing**: Automatic error recovery validation

#### 12.1.3 Hardware Acceleration Interface

- **Plugin Architecture**: Support for hardware-specific backends
- **RDMA Integration**: Remote Direct Memory Access for networked storage
- **NVMe Pass-through**: Direct NVMe command submission
- **Encryption Offload**: Hardware-accelerated encryption/decryption

### 12.2 Performance Enhancements

#### 12.2.1 Advanced Polling

- **Adaptive Polling**: Dynamic polling timeout based on load
- **Batched Completions**: Process multiple completions per poll cycle
- **CPU Affinity**: Automatic CPU affinity optimization for polling threads
- **Hybrid Mode**: Automatic switching between polling and interrupt modes

#### 12.2.2 Memory Optimizations

- **Huge Pages**: Support for transparent huge page allocations
- **NUMA Optimization**: NUMA-aware memory allocation and queue placement
- **Memory Compression**: Optional compression for virtual storage backend
- **Direct Memory Access**: Bypass kernel buffers for registered buffer I/O

#### 12.2.3 Advanced Queueing

- **Priority Queues**: Support for I/O priority classes
- **Quality of Service**: Bandwidth and IOPS throttling
- **Deadline Scheduling**: Ensure latency SLAs for critical operations
- **Multi-tenant Support**: Isolation between different users/applications

### 12.3 Monitoring and Observability

#### 12.3.1 Enhanced Telemetry

- **Histogram Metrics**: Detailed latency and size distributions
- **Trace Integration**: Integration with Linux trace infrastructure
- **Prometheus Export**: Native Prometheus metrics export
- **Real-time Dashboards**: Built-in web interface for monitoring

#### 12.3.2 Debugging Features

- **Flight Recorder**: Continuous recording of recent operations
- **Request Tracing**: End-to-end tracing of individual requests
- **Performance Profiling**: Built-in profiling and bottleneck identification
- **Crash Analysis**: Automatic crash dump and analysis tools

### 12.4 API Extensions

#### 12.4.1 Extended URING_CMD Interface

- **Streaming Commands**: Long-running command operations
- **Asynchronous Admin**: Non-blocking admin command execution
- **Bulk Operations**: Batch processing of multiple admin commands
- **Event Notifications**: Asynchronous device event delivery

#### 12.4.2 Enhanced Configuration

- **Dynamic Reconfiguration**: Runtime queue topology changes
- **Policy Management**: Complex I/O scheduling and routing policies
- **Resource Limits**: Per-application resource quotas and limits
- **Security Policies**: Fine-grained access control and auditing

### 12.5 Ecosystem Integration

#### 12.5.1 Container Support

- **Device Passthrough**: Secure device sharing in containers
- **Resource Isolation**: Per-container performance isolation
- **Metrics Export**: Container-aware performance metrics
- **Policy Enforcement**: Container-specific I/O policies

#### 12.5.2 Cloud Integration

- **Cloud Storage Backends**: Integration with cloud block storage services
- **Multi-region Support**: Cross-region replication and failover
- **Auto-scaling**: Automatic capacity and performance scaling
- **Cost Optimization**: Usage-based cost tracking and optimization

---

**End of Specification**

This specification defines the complete interface and implementation requirements for the uringblk io_uring-first Linux block driver. It serves as the authoritative reference for developers, users, and system integrators working with the driver.

For implementation details, see the source code in the `driver/` directory. For usage examples and testing procedures, see the accompanying README and test programs.