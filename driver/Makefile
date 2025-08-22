# Makefile for uringblk io_uring-first Block Driver

# Module name
MODULE_NAME := uringblk_driver

# Source files
obj-m += $(MODULE_NAME).o
uringblk_driver-objs := uringblk_main.o uringblk_sysfs.o

# Kernel build directory (automatically detected)
KERNEL_DIR ?= /lib/modules/$(shell uname -r)/build

# Current directory
PWD := $(shell pwd)

# Compiler flags
EXTRA_CFLAGS += -Wall -Wextra -Wno-unused-parameter
EXTRA_CFLAGS += -Wno-sign-compare -Wno-unused-function
EXTRA_CFLAGS += -DDEBUG

# Test program settings
TEST_PROGRAM := uringblk_test
TEST_CFLAGS := -Wall -Wextra -O2 -std=c99
TEST_LIBS := -luring

# Default target
all: module test

# Build the kernel module
module:
	@echo "Building uringblk driver kernel module..."
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules
	@echo "Build complete. Module: $(MODULE_NAME).ko"

# Build test program
test: $(TEST_PROGRAM)

$(TEST_PROGRAM): uringblk_test.c uringblk_driver.h
	@echo "Building test program..."
	$(CC) $(TEST_CFLAGS) -o $(TEST_PROGRAM) uringblk_test.c $(TEST_LIBS)
	@echo "Test program built: $(TEST_PROGRAM)"

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
	rm -f *.mod.c *.mod *.o *.ko *.symvers *.order
	rm -f $(TEST_PROGRAM)
	@echo "Clean complete."

# Install the module (requires root)
install: module
	@echo "Installing uringblk driver module..."
	sudo $(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules_install
	sudo depmod -a
	@echo "Module installed. Use 'sudo modprobe $(MODULE_NAME)' to load."

# Load the module
load: module
	@echo "Loading uringblk driver module..."
	sudo insmod ./$(MODULE_NAME).ko
	@echo "Module loaded. Check dmesg for status."
	@echo "Device should be available at /dev/uringblk0"
	@ls -l /dev/uringblk* 2>/dev/null || echo "Device nodes not found - check dmesg"

# Unload the module
unload:
	@echo "Unloading uringblk driver module..."
	sudo rmmod $(MODULE_NAME) || true
	@echo "Module unloaded."

# Reload the module (unload + load)
reload: unload load

# Check module status
status:
	@echo "Checking uringblk driver status..."
	@if lsmod | grep -q $(MODULE_NAME); then \
		echo "Module $(MODULE_NAME) is loaded."; \
		ls -l /dev/uringblk* 2>/dev/null || echo "Device files not found."; \
		echo "Sysfs attributes:"; \
		find /sys/block/uringblk*/uringblk/ -type f 2>/dev/null | head -10; \
	else \
		echo "Module $(MODULE_NAME) is not loaded."; \
	fi

# Show recent kernel messages related to uringblk driver
dmesg:
	@echo "Recent uringblk driver messages:"
	@dmesg | grep -i uringblk | tail -20 || echo "No uringblk driver messages found."

# Test the device (requires module to be loaded and test program built)
run-test: test status
	@echo "Running uringblk tests..."
	@if [ -b /dev/uringblk0 ] && [ -x ./$(TEST_PROGRAM) ]; then \
		echo "Running basic tests..."; \
		sudo ./$(TEST_PROGRAM) -d /dev/uringblk0; \
		echo ""; \
		echo "Running admin command tests..."; \
		sudo ./$(TEST_PROGRAM) -d /dev/uringblk0 -a; \
		echo ""; \
		echo "Running performance test with polling..."; \
		sudo ./$(TEST_PROGRAM) -d /dev/uringblk0 -p -c 100; \
	else \
		echo "Device /dev/uringblk0 not found or test program not built."; \
		echo "Make sure module is loaded and test program is compiled."; \
	fi

# Test with different configurations
test-poll: test
	@echo "Testing with polling enabled..."
	sudo ./$(TEST_PROGRAM) -d /dev/uringblk0 -p -c 500

test-fixed: test
	@echo "Testing with fixed buffers..."
	sudo ./$(TEST_PROGRAM) -d /dev/uringblk0 -f -c 500

test-admin: test
	@echo "Testing admin commands..."
	sudo ./$(TEST_PROGRAM) -d /dev/uringblk0 -a

# Performance benchmark
benchmark: test status
	@echo "Running performance benchmarks..."
	@if [ -b /dev/uringblk0 ] && [ -x ./$(TEST_PROGRAM) ]; then \
		echo "Standard I/O:"; \
		sudo ./$(TEST_PROGRAM) -d /dev/uringblk0 -c 2000 -q 64; \
		echo ""; \
		echo "Polling I/O:"; \
		sudo ./$(TEST_PROGRAM) -d /dev/uringblk0 -p -c 2000 -q 64; \
		echo ""; \
		echo "Fixed buffers + polling:"; \
		sudo ./$(TEST_PROGRAM) -d /dev/uringblk0 -p -f -c 2000 -q 64; \
	else \
		echo "Device or test program not available"; \
	fi

# Show device statistics via sysfs
stats:
	@echo "uringblk device statistics:"
	@if [ -d /sys/block/uringblk0/uringblk ]; then \
		echo "Read operations: $$(cat /sys/block/uringblk0/uringblk/read_ops 2>/dev/null || echo 'N/A')"; \
		echo "Write operations: $$(cat /sys/block/uringblk0/uringblk/write_ops 2>/dev/null || echo 'N/A')"; \
		echo "Read bytes: $$(cat /sys/block/uringblk0/uringblk/read_bytes 2>/dev/null || echo 'N/A')"; \
		echo "Write bytes: $$(cat /sys/block/uringblk0/uringblk/write_bytes 2>/dev/null || echo 'N/A')"; \
		echo "Flush operations: $$(cat /sys/block/uringblk0/uringblk/flush_ops 2>/dev/null || echo 'N/A')"; \
		echo "Queue full events: $$(cat /sys/block/uringblk0/uringblk/queue_full_events 2>/dev/null || echo 'N/A')"; \
		echo "Features: $$(cat /sys/block/uringblk0/uringblk/features 2>/dev/null || echo 'N/A')"; \
		echo "Firmware: $$(cat /sys/block/uringblk0/uringblk/firmware_rev 2>/dev/null || echo 'N/A')"; \
	else \
		echo "Sysfs attributes not found. Is the module loaded?"; \
	fi

# Reset device statistics
reset-stats:
	@echo "Resetting device statistics..."
	@if [ -f /sys/block/uringblk0/uringblk/stats_reset ]; then \
		echo 1 | sudo tee /sys/block/uringblk0/uringblk/stats_reset > /dev/null; \
		echo "Statistics reset."; \
	else \
		echo "Stats reset not available."; \
	fi

# Development helper: quick rebuild and test cycle
dev: clean reload run-test

# Show all available sysfs attributes
sysfs-attrs:
	@echo "Available sysfs attributes:"
	@find /sys/block/uringblk*/uringblk/ -type f 2>/dev/null | while read f; do \
		echo "$$f: $$(cat "$$f" 2>/dev/null || echo 'read error')"; \
	done || echo "No sysfs attributes found"

# Module parameter testing
test-params:
	@echo "Testing module parameters..."
	sudo rmmod $(MODULE_NAME) 2>/dev/null || true
	@echo "Loading with custom parameters..."
	sudo insmod ./$(MODULE_NAME).ko nr_hw_queues=2 queue_depth=512 capacity_mb=512
	@echo "Module loaded with custom parameters"
	@echo "Check module info:"
	modinfo $(MODULE_NAME).ko | grep parm || echo "No parameters found"

# Show help
help:
	@echo "uringblk Driver Makefile Help"
	@echo "=============================="
	@echo "Available targets:"
	@echo "  all/module      - Build the kernel module"
	@echo "  test           - Build test program"
	@echo "  clean          - Clean build artifacts"
	@echo "  install        - Install module to system"
	@echo "  load           - Load the module (insmod)"
	@echo "  unload         - Unload the module (rmmod)"
	@echo "  reload         - Unload and reload module"
	@echo "  status         - Check module and device status"
	@echo "  dmesg          - Show recent uringblk driver kernel messages"
	@echo "  run-test       - Run comprehensive tests"
	@echo "  test-poll      - Test with polling enabled"
	@echo "  test-fixed     - Test with fixed buffers"
	@echo "  test-admin     - Test admin commands"
	@echo "  benchmark      - Run performance benchmarks"
	@echo "  stats          - Show device statistics"
	@echo "  reset-stats    - Reset device statistics"
	@echo "  sysfs-attrs    - Show all sysfs attributes"
	@echo "  test-params    - Test module parameters"
	@echo "  dev            - Clean, rebuild, reload, and test"
	@echo "  help           - Show this help"
	@echo ""
	@echo "Module Parameters:"
	@echo "  nr_hw_queues=N     - Number of hardware queues (default: 4)"
	@echo "  queue_depth=N      - Queue depth per HW queue (default: 1024)"
	@echo "  capacity_mb=N      - Device capacity in MB (default: 1024)"
	@echo "  enable_poll=0/1    - Enable polling support (default: 1)"
	@echo "  enable_discard=0/1 - Enable discard support (default: 1)"
	@echo "  write_cache=0/1    - Enable write cache (default: 1)"
	@echo ""
	@echo "Usage examples:"
	@echo "  make                    # Build module and test program"
	@echo "  make load               # Load module with defaults"
	@echo "  make run-test           # Run all tests"
	@echo "  make benchmark          # Run performance tests"
	@echo "  make stats              # Show statistics"
	@echo "  make dmesg              # Check kernel messages"

.PHONY: all module test clean install load unload reload status dmesg run-test test-poll test-fixed test-admin benchmark stats reset-stats sysfs-attrs test-params dev help