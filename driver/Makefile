# Makefile for WAL Driver Kernel Module

# Module name
MODULE_NAME := wal_driver

# Source files
obj-m += $(MODULE_NAME).o
wal_driver-objs := wal_driver_main.o

# Kernel build directory (automatically detected)
KERNEL_DIR ?= /lib/modules/$(shell uname -r)/build

# Current directory
PWD := $(shell pwd)

# Compiler flags
#EXTRA_CFLAGS += -Wall -Wextra -Werror
#EXTRA_CFLAGS += -DDEBUG

# Alternative: More targeted warning flags (-Werror causes problems in kernel headers)
EXTRA_CFLAGS += -Wall -Wextra -Wno-unused-parameter
EXTRA_CFLAGS += -Wno-sign-compare -Wno-unused-function

# Default target
all: module

# Build the kernel module
module:
	@echo "Building WAL driver kernel module..."
	@$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules 2>&1 | grep -v "warning: the compiler differs from the one used to build the kernel" || true
	@echo "Build complete. Module: $(MODULE_NAME).ko"

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
	rm -f *.mod.c *.mod *.o *.ko *.symvers *.order
	@echo "Clean complete."

# Install the module (requires root)
install: module
	@echo "Installing WAL driver module..."
	sudo $(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules_install
	sudo depmod -a
	@echo "Module installed. Use 'sudo modprobe $(MODULE_NAME)' to load."

# Load the module
load: module
	@echo "Loading WAL driver module..."
	sudo insmod ./$(MODULE_NAME).ko
	@echo "Module loaded. Check dmesg for status."
	@echo "Devices should be available at:"
	@echo "  Character device: /dev/rwal"
	@echo "  Block device:     /dev/wal"

# Unload the module
unload:
	@echo "Unloading WAL driver module..."
	sudo rmmod $(MODULE_NAME) || true
	@echo "Module unloaded."

# Reload the module (unload + load)
reload: unload load

# Check module status
status:
	@echo "Checking WAL driver status..."
	@if lsmod | grep -q $(MODULE_NAME); then \
		echo "Module $(MODULE_NAME) is loaded."; \
		ls -l /dev/rwal /dev/wal 2>/dev/null || echo "Device files not found."; \
		cat /proc/wal_driver 2>/dev/null || echo "Proc entry not available."; \
	else \
		echo "Module $(MODULE_NAME) is not loaded."; \
	fi

# Show recent kernel messages related to WAL driver
dmesg:
	@echo "Recent WAL driver messages:"
	@dmesg | grep -i wal_driver | tail -20 || echo "No WAL driver messages found."

# Test the devices (requires module to be loaded)
test: status
	@echo "Testing WAL devices..."
	@if [ -c /dev/rwal ] && [ -b /dev/wal ]; then \
		echo "Device files exist and have correct types."; \
		echo "Testing character device read:"; \
		timeout 5 cat /dev/rwal || echo "Character device read test completed."; \
		echo "Testing character device write:"; \
		echo "Hello from userspace!" | timeout 5 tee /dev/rwal > /dev/null || echo "Character device write test completed."; \
		echo "Testing block device (read 512 bytes):"; \
		timeout 5 dd if=/dev/wal bs=512 count=1 2>/dev/null | hexdump -C | head -5 || echo "Block device read test completed."; \
		echo "Testing block device write:"; \
		echo "Block test data" | timeout 5 dd of=/dev/wal bs=16 count=1 2>/dev/null || echo "Block device write test completed."; \
		echo "Check dmesg for captured data."; \
	else \
		echo "Device files not found or incorrect types. Make sure module is loaded."; \
	fi

# Create device nodes manually (if not created automatically)
mknod:
	@echo "Creating device nodes manually..."
	sudo mknod /dev/rwal c 240 0 2>/dev/null || echo "/dev/rwal already exists or failed to create"
	sudo mknod /dev/wal b 240 1 2>/dev/null || echo "/dev/wal already exists or failed to create"
	sudo chmod 666 /dev/rwal /dev/wal 2>/dev/null || echo "Failed to set permissions"
	@echo "Device nodes created (if needed)."

# Remove device nodes
rmnod:
	@echo "Removing device nodes..."
	sudo rm -f /dev/rwal /dev/wal
	@echo "Device nodes removed."

# Development helper: quick rebuild and reload
dev: clean reload test

# Show help
help:
	@echo "WAL Driver Makefile Help"
	@echo "========================"
	@echo "Available targets:"
	@echo "  all/module  - Build the kernel module"
	@echo "  clean       - Clean build artifacts"
	@echo "  install     - Install module to system"
	@echo "  load        - Load the module (insmod)"
	@echo "  unload      - Unload the module (rmmod)"
	@echo "  reload      - Unload and reload module"
	@echo "  status      - Check module and device status"
	@echo "  dmesg       - Show recent WAL driver kernel messages"
	@echo "  test        - Test device functionality"
	@echo "  mknod       - Create device nodes manually"
	@echo "  rmnod       - Remove device nodes"
	@echo "  dev         - Clean, rebuild, reload, and test"
	@echo "  help        - Show this help"
	@echo ""
	@echo "Usage examples:"
	@echo "  make            # Build module"
	@echo "  make load       # Load module"
	@echo "  make test       # Test devices"
	@echo "  make dmesg      # Check kernel messages"
	@echo "  make unload     # Unload module"

.PHONY: all module clean install load unload reload status dmesg test mknod rmnod dev help

