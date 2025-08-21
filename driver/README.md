# WAL Device Driver

This is a Linux kernel driver for Write-Ahead Log (WAL) devices that provides both character and block device interfaces.

## Overview

The WAL driver creates two devices:
- **`/dev/rwal`** - Character device (major=240, minor=0)
- **`/dev/wal`** - Block device (major=240, minor=1)

### Device Behavior

**Character Device (`/dev/rwal`)**:
- **Read operations**: Returns "Hello from WAL\n"
- **Write operations**: Captures and logs written data to kernel log

**Block Device (`/dev/wal`)**:
- **Read operations**: Returns "Hello from WAL\n" pattern
- **Write operations**: Captures and logs written data to kernel log
- **Block size**: 512 bytes
- **Virtual capacity**: 1MB (2048 sectors)

## Files

- **`wal_driver.h`** - Header file with driver interface definitions
- **`wal_driver.c`** - Main kernel driver implementation
- **`Makefile`** - Build system for the kernel module
- **`wal_test.c`** - Userspace test program
- **`test_Makefile`** - Build system for the test program

## Building the Driver

### Prerequisites

- Linux kernel headers for your running kernel
- GCC compiler
- Make

Install kernel headers:
```bash
# Ubuntu/Debian
sudo apt install linux-headers-$(uname -r)

# RHEL/CentOS/Fedora
sudo dnf install kernel-headers kernel-devel

# Arch Linux
sudo pacman -S linux-headers
```

### Compile the Driver

```bash
# Build the kernel module
make

# Or build with specific kernel
make KERNEL_DIR=/path/to/kernel/source
```

### Load the Driver

```bash
# Load the module
make load

# Or manually
sudo insmod wal_driver.ko
```

### Check Status

```bash
# Check if module is loaded
make status

# View recent kernel messages
make dmesg

# Check device files
ls -l /dev/rwal /dev/wal
```

## Using the Driver

### Basic Operations

```bash
# Read from character device
cat /dev/rwal

# Write to character device
echo "Hello World" > /dev/rwal

# Read from block device (512 bytes)
dd if=/dev/wal bs=512 count=1 | hexdump -C

# Write to block device
echo "Block data" | dd of=/dev/wal bs=512 count=1
```

### Monitor Kernel Messages

```bash
# Watch kernel messages in real-time
sudo dmesg -w | grep wal_driver

# Or view recent messages
dmesg | grep wal_driver | tail -20
```

### View Statistics

```bash
# View driver statistics
cat /proc/wal_driver
```

## Test Program

Build and run the test program:

```bash
# Build test program
make -f test_Makefile

# Run all tests
./wal_test

# Run specific tests
./wal_test -c          # Character device only
./wal_test -b          # Block device only
./wal_test -i          # IOCTL commands only
./wal_test -e          # Device information only

# Show help
./wal_test -h
```

## IOCTL Commands

The driver supports several ioctl commands:

```c
#include "wal_driver.h"

int fd = open("/dev/rwal", O_RDWR);

// Get current statistics
struct wal_status status;
ioctl(fd, WAL_IOC_GET_STATUS, &status);

// Reset statistics
ioctl(fd, WAL_IOC_RESET);

// Set driver mode
enum wal_mode mode = WAL_MODE_DEBUG;
ioctl(fd, WAL_IOC_SET_MODE, &mode);
```

### Driver Modes

- **`WAL_MODE_NORMAL`** (0) - Normal operation with basic logging
- **`WAL_MODE_DEBUG`** (1) - Verbose debugging output
- **`WAL_MODE_QUIET`** (2) - Minimal logging

## Development Workflow

```bash
# Quick development cycle
make dev              # Clean, rebuild, reload, and test

# Individual steps
make clean            # Clean build artifacts
make                  # Build module
make reload           # Unload and reload module
make test             # Test devices
```

## Troubleshooting

### Module Won't Load

1. Check kernel version compatibility:
   ```bash
   uname -r
   ls /lib/modules/$(uname -r)/build
   ```

2. Check for compilation errors:
   ```bash
   make clean
   make
   ```

3. Check system logs:
   ```bash
   dmesg | tail -20
   ```

### Device Files Missing

1. Check if module is loaded:
   ```bash
   lsmod | grep wal_driver
   ```

2. Create device nodes manually:
   ```bash
   make mknod
   ```

3. Check permissions:
   ```bash
   ls -l /dev/rwal /dev/wal
   sudo chmod 666 /dev/rwal /dev/wal
   ```

### Permission Denied

1. Check device permissions:
   ```bash
   ls -l /dev/rwal /dev/wal
   ```

2. Fix permissions:
   ```bash
   sudo chmod 666 /dev/rwal /dev/wal
   ```

3. Add user to appropriate group (if needed):
   ```bash
   sudo usermod -a -G disk $USER
   ```

## Unloading the Driver

```bash
# Unload the module
make unload

# Or manually
sudo rmmod wal_driver

# Remove device nodes (if needed)
make rmnod
```

## Integration with KVM Database

This driver is designed to work with the KVM Database project. Once loaded, the `/dev/rwal` and `/dev/wal` devices will be available for the userspace application to open and use for testing WAL operations.

The userspace application can:
1. Create the device nodes using `mknod()` system calls
2. Open the devices for reading and writing
3. Perform I/O operations that will be logged by the kernel driver
4. Use ioctl commands to control driver behavior and get statistics

## License

This driver is licensed under the GPL v2 license, compatible with the Linux kernel.

## Support

For issues or questions:
1. Check kernel logs: `dmesg | grep wal_driver`
2. Verify prerequisites are installed
3. Ensure running as root for module operations
4. Check device permissions and existence

