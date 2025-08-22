# Enhanced uringblk Driver - Real Block Device Support

## Overview

The enhanced uringblk driver now provides comprehensive support for real block devices with configurable load parameters. It maintains the original virtual filesystem capability while adding robust real block device backend support.

## Key Features

1. **Dual Backend Support**: Virtual memory-based and real block device backends
2. **Auto-detection**: Automatic device size detection for real block devices
3. **Multiple Device Support**: Support for multiple uringblk devices simultaneously
4. **Flexible Configuration**: Comprehensive module parameters for device configuration
5. **Enhanced Error Handling**: Detailed error reporting and device validation

## Module Parameters

### Basic Configuration
- `backend_type` (int, default: 0): Backend type (0=virtual, 1=device)
- `capacity_mb` (uint, default: 1024): Device capacity in MB for virtual backend
- `logical_block_size` (uint, default: 512): Logical block size in bytes

### Hardware Queue Configuration
- `nr_hw_queues` (uint, default: 4): Number of hardware queues
- `queue_depth` (uint, default: 1024): Queue depth per hardware queue
- `enable_poll` (bool, default: true): Enable polling support
- `enable_discard` (bool, default: true): Enable discard/TRIM support
- `write_cache` (bool, default: true): Enable write cache

### Real Block Device Configuration
- `backend_device` (string, default: ""): Backend device path (e.g., /dev/sda1)
- `auto_detect_size` (bool, default: true): Auto-detect device size for real block devices
- `devices` (string, default: ""): Comma-separated list of device paths
- `max_devices` (int, default: 1): Maximum number of uringblk devices to create

## Usage Examples

### 1. Virtual Backend (Default)
```bash
# Load driver with virtual backend
sudo modprobe uringblk

# Create 2GB virtual device
sudo modprobe uringblk capacity_mb=2048
```

### 2. Single Real Block Device
```bash
# Use a real block device
sudo modprobe uringblk backend_type=1 backend_device=/dev/sdb1

# With custom capacity (will be auto-detected if larger than device)
sudo modprobe uringblk backend_type=1 backend_device=/dev/sdb1 capacity_mb=1024
```

### 3. Multiple Real Block Devices
```bash
# Multiple devices from comma-separated list
sudo modprobe uringblk devices=/dev/sdb1,/dev/sdc1,/dev/sdd1 max_devices=3

# Mix of configuration
sudo modprobe uringblk devices="/dev/sdb1, /dev/sdc1" auto_detect_size=true
```

### 4. Performance Tuning
```bash
# High performance configuration
sudo modprobe uringblk backend_type=1 backend_device=/dev/nvme0n1p1 \
    nr_hw_queues=8 queue_depth=2048 enable_poll=true
```

## Device Mapping

The driver creates block devices with the following naming pattern:
- First device: `/dev/uringblk0`
- Second device: `/dev/uringblk1`
- And so on...

## Backend Architecture

### Virtual Backend
- Uses `vzalloc()` for memory allocation
- Supports up to `capacity_mb` size
- All operations are memory-based (immediate completion)
- Ideal for testing and development

### Real Block Device Backend
- Opens the specified block device with `O_RDWR | O_LARGEFILE`
- Auto-detects device size using `bdev_nr_sectors()`
- Performs validation checks (permissions, device type, accessibility)
- Uses `kernel_read()` and `kernel_write()` for I/O operations
- Supports `vfs_fsync()` for flush operations
- Supports `blkdev_issue_discard()` for discard operations

## Error Handling

The driver provides comprehensive error handling:

1. **Device Access Errors**: Permission denied, device busy, read-only
2. **Validation Errors**: Invalid device type, zero size, path too long
3. **I/O Errors**: Read/write failures with detailed error reporting
4. **Configuration Errors**: Invalid backend type, missing device path

## Safety Features

1. **Path Validation**: Checks device path length and accessibility
2. **Device Type Verification**: Ensures target is a block device
3. **Size Validation**: Prevents capacity exceeding device size
4. **Read Test**: Performs initial read test to verify device access
5. **Read-only Detection**: Warns when device is mounted read-only

## Performance Considerations

1. **Multiple Queues**: Use `nr_hw_queues` to match your CPU cores
2. **Queue Depth**: Increase `queue_depth` for high IOPS workloads
3. **Polling**: Enable `enable_poll` for low latency applications
4. **Block Size**: Match `logical_block_size` to your workload requirements

## Troubleshooting

### Common Issues

1. **Permission Denied**: Ensure the device is accessible and not exclusively locked
2. **Device Busy**: Check if the device is mounted or used by another driver
3. **Zero Size Device**: Verify the block device has valid partition table
4. **Read-only Device**: Check mount status and device permissions

### Debug Information

Enable kernel debug messages:
```bash
echo 8 > /proc/sys/kernel/printk
```

Check driver messages:
```bash
dmesg | grep uringblk
```

### Module Removal

```bash
sudo rmmod uringblk
```

Note: Ensure all uringblk devices are unmounted before removing the module.

## Integration

The enhanced driver maintains full API compatibility with the original uringblk specification while providing robust real block device support. Applications using the driver require no modifications when switching between virtual and real backends.