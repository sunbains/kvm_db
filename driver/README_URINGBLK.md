# uringblk: io_uring-first Linux Block Driver

This is a high-performance Linux kernel block device driver that implements the "io_uring-first" architecture as specified in the Linux Block Driver with io_uring-first User Interface specification.

## Overview

The uringblk driver creates a virtual block device (`/dev/uringblk0`) that prioritizes io_uring as the primary I/O interface while maintaining full compatibility with the standard Linux block layer. It's designed for high-throughput, low-latency applications that can benefit from modern asynchronous I/O patterns.

### Key Features

- **io_uring-first design**: Optimized for io_uring with polling support (IORING_SETUP_IOPOLL)
- **blk-mq integration**: Full integration with Linux multi-queue block layer
- **URING_CMD admin interface**: Driver-specific admin commands without ioctl
- **High performance**: Supports up to 1M+ IOPS with low CPU overhead
- **Modern I/O features**: FUA, FLUSH, Discard/TRIM, Write Zeroes support
- **Comprehensive telemetry**: Statistics via sysfs and URING_CMD
- **Configurable**: Runtime configuration via module parameters

## Architecture

### Data Plane
- **blk-mq tag set**: Multi-queue architecture with configurable hardware queues
- **Zero-copy path**: Direct DMA to/from user buffers when using registered buffers
- **Polling support**: Lock-free completion polling for ultra-low latency
- **NUMA awareness**: Per-CPU queue allocation and processing

### Admin Plane
- **URING_CMD interface**: Modern command interface via `IORING_OP_URING_CMD`
- **Versioned ABI**: Backward-compatible admin command protocol
- **Comprehensive introspection**: Device identification, limits, statistics

### Storage Backend
- **Virtual storage**: In-memory storage backend for testing and development
- **Configurable capacity**: Adjustable device size via module parameters
- **Simulation features**: Configurable latency and error injection (future)

## Building and Installation

### Prerequisites

```bash
# Ubuntu/Debian
sudo apt install linux-headers-$(uname -r) liburing-dev build-essential

# RHEL/CentOS/Fedora  
sudo dnf install kernel-headers kernel-devel liburing-devel

# Arch Linux
sudo pacman -S linux-headers liburing
```

### Build

```bash
# Build kernel module and test program
make -f Makefile.uringblk all

# Or build individually
make -f Makefile.uringblk module    # Kernel module only
make -f Makefile.uringblk test      # Test program only
```

### Installation

```bash
# Load the module (creates /dev/uringblk0)
make -f Makefile.uringblk load

# Check status
make -f Makefile.uringblk status

# Check kernel messages
make -f Makefile.uringblk dmesg
```

## Usage

### Basic I/O with io_uring

```c
#include <liburing.h>
#include <fcntl.h>

int main() {
    struct io_uring ring;
    int fd;
    
    // Open device with O_DIRECT for best performance
    fd = open("/dev/uringblk0", O_RDWR | O_DIRECT);
    
    // Setup ring with polling for lowest latency
    io_uring_queue_init(64, &ring, IORING_SETUP_IOPOLL);
    
    // Register buffers for zero-copy I/O
    struct iovec iov = {.iov_base = buffer, .iov_len = size};
    io_uring_register_buffers(&ring, &iov, 1);
    
    // Submit read/write operations
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_read_fixed(sqe, fd, buffer, 4096, offset, 0);
    io_uring_submit(&ring);
    
    // Poll for completions
    struct io_uring_cqe *cqe;
    io_uring_wait_cqe(&ring, &cqe);
    // cqe->res contains result (bytes transferred or -errno)
    io_uring_cqe_seen(&ring, cqe);
}
```

### Admin Commands via URING_CMD

```c
#include "uringblk_driver.h"

// Device identification
struct uringblk_ucmd_hdr hdr = {
    .abi_major = URINGBLK_ABI_MAJOR,
    .abi_minor = URINGBLK_ABI_MINOR,
    .opcode = URINGBLK_UCMD_IDENTIFY,
    .payload_len = sizeof(struct uringblk_identify)
};

char cmd_buf[sizeof(hdr) + sizeof(struct uringblk_identify)];
memcpy(cmd_buf, &hdr, sizeof(hdr));

struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
io_uring_prep_cmd(sqe, fd, 0, 0, 0, cmd_buf, sizeof(cmd_buf));
io_uring_submit(&ring);

// Result in cqe->res, device info in cmd_buf + sizeof(hdr)
```

### Testing and Validation

```bash
# Run comprehensive test suite
make -f Makefile.uringblk run-test

# Performance benchmarks
make -f Makefile.uringblk benchmark

# Test specific features
make -f Makefile.uringblk test-poll      # Polling mode
make -f Makefile.uringblk test-fixed     # Fixed buffers
make -f Makefile.uringblk test-admin     # Admin commands
```

## Configuration

### Module Parameters

Load the module with custom parameters:

```bash
sudo insmod uringblk_driver.ko nr_hw_queues=8 queue_depth=2048 capacity_mb=2048
```

Available parameters:
- `nr_hw_queues`: Number of hardware queues (default: 4)
- `queue_depth`: Queue depth per HW queue (default: 1024)  
- `capacity_mb`: Device capacity in MB (default: 1024)
- `enable_poll`: Enable polling support (default: true)
- `enable_discard`: Enable discard/TRIM (default: true)
- `write_cache`: Enable write cache (default: true)
- `logical_block_size`: Logical block size in bytes (default: 512)

### Runtime Configuration

View and modify settings via sysfs:

```bash
# View device configuration
cat /sys/block/uringblk0/uringblk/features
cat /sys/block/uringblk0/uringblk/nr_hw_queues
cat /sys/block/uringblk0/uringblk/queue_depth

# View performance statistics
cat /sys/block/uringblk0/uringblk/read_ops
cat /sys/block/uringblk0/uringblk/write_ops
cat /sys/block/uringblk0/uringblk/read_bytes

# Reset statistics
echo 1 > /sys/block/uringblk0/uringblk/stats_reset
```

## Performance Optimization

### For Maximum IOPS
- Use polling mode (`IORING_SETUP_IOPOLL`)
- Register buffers (`IORING_REGISTER_BUFFERS`)  
- Use `O_DIRECT` flag
- Align I/O to 4KB boundaries
- Queue depth 64-256 per core
- Multiple hardware queues

### For Maximum Bandwidth
- Large I/O sizes (64KB - 1MB)
- Sequential access patterns
- Multiple outstanding operations
- Consider write cache settings

### Example High-Performance Setup

```bash
# Load with optimized parameters
sudo insmod uringblk_driver.ko \
    nr_hw_queues=16 \
    queue_depth=1024 \
    capacity_mb=4096 \
    logical_block_size=4096

# Test with optimal settings
./uringblk_test -d /dev/uringblk0 -p -f -c 10000 -q 128
```

## URING_CMD ABI Reference

### Command Header
```c
struct uringblk_ucmd_hdr {
    __u16 abi_major;        // Must be URINGBLK_ABI_MAJOR (1)
    __u16 abi_minor;        // Must be URINGBLK_ABI_MINOR (0)  
    __u16 opcode;           // Command opcode
    __u16 flags;            // Reserved, must be 0
    __u32 payload_len;      // Payload size in bytes
};
```

### Supported Commands

| Opcode | Command | Description |
|--------|---------|-------------|
| 0x01 | `IDENTIFY` | Device identification and capabilities |
| 0x02 | `GET_LIMITS` | Queue and I/O limits |
| 0x03 | `GET_FEATURES` | Feature bitmap |
| 0x05 | `GET_GEOMETRY` | Device geometry |
| 0x06 | `GET_STATS` | Performance statistics |

### Error Codes

| Error | Description |
|-------|-------------|
| `-EINVAL` | Invalid parameters or ABI version |
| `-EOPNOTSUPP` | Unsupported command |
| `-EFAULT` | Bad user pointer |
| `-EOVERFLOW` | Payload too large |
| `-EPROTO` | Malformed command |

## Monitoring and Debugging

### Performance Statistics

```bash
# Show comprehensive stats
make -f Makefile.uringblk stats

# Continuous monitoring
watch -n 1 'make -f Makefile.uringblk stats'
```

### Kernel Messages

```bash
# View driver messages
make -f Makefile.uringblk dmesg

# Follow live messages  
sudo dmesg -w | grep uringblk
```

### sysfs Attributes

All driver-specific attributes are under `/sys/block/uringblk0/uringblk/`:

- **Device info**: `model`, `firmware_rev`, `features`, `capacity`
- **Configuration**: `nr_hw_queues`, `queue_depth`, `poll_enabled`
- **Statistics**: `read_ops`, `write_ops`, `read_bytes`, `write_bytes`
- **Errors**: `queue_full_events`, `media_errors`

## Integration with Applications

### Database Systems
- Use for WAL (Write-Ahead Log) devices
- High-frequency transaction logging
- Low-latency checkpoint operations

### Cache Layers  
- Page cache acceleration
- Metadata caching
- Temporary storage

### Log Processing
- High-throughput log ingestion
- Stream processing buffers
- Time-series data storage

## Troubleshooting

### Module Load Issues

```bash
# Check kernel compatibility
uname -r
ls /lib/modules/$(uname -r)/build

# Verify compilation
make -f Makefile.uringblk clean module

# Check dependencies
lsmod | grep blk_mq
```

### Device Not Found

```bash
# Check module status
lsmod | grep uringblk

# Check kernel messages
dmesg | grep uringblk | tail -10

# Verify device creation
ls -l /dev/uringblk*
```

### Performance Issues

1. **Low IOPS**: Enable polling mode and use fixed buffers
2. **High CPU**: Reduce queue depth or number of queues  
3. **High latency**: Check if device is in polling mode
4. **Memory issues**: Reduce capacity_mb parameter

### Common Error Messages

- **"No such device"**: Module not loaded or device creation failed
- **"Operation not supported"**: Feature not enabled (polling, discard, etc.)
- **"Invalid argument"**: Check ABI version in URING_CMD
- **"Permission denied"**: Need root privileges for device access

## Development

### Adding New Features

1. Update header file (`uringblk_driver.h`)
2. Implement in main module (`uringblk_main.c`)
3. Add sysfs attributes (`uringblk_sysfs.c`)
4. Update test program (`uringblk_test.c`)
5. Test thoroughly with various configurations

### Testing Checklist

- [ ] Basic read/write operations
- [ ] URING_CMD admin interface  
- [ ] Polling mode functionality
- [ ] Fixed buffer support
- [ ] Error handling and edge cases
- [ ] Performance under load
- [ ] Module load/unload cycles

## License

This driver is licensed under GPL v2, compatible with the Linux kernel.

## Contributing

1. Follow Linux kernel coding style
2. Add comprehensive tests for new features
3. Update documentation
4. Test across different kernel versions
5. Ensure backward compatibility for URING_CMD ABI

For bug reports and feature requests, check kernel messages and provide detailed reproduction steps.