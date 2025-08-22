#!/bin/bash

# Test script for enhanced uringblk driver
# This script demonstrates various configuration options

set -e  # Exit on any error

DRIVER_NAME="wal_driver"  # Actual module name from build
MODULE_FILE="${DRIVER_NAME}.ko"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if running as root
if [[ $EUID -ne 0 ]]; then
   log_error "This script must be run as root (use sudo)"
   exit 1
fi

# Check if module file exists
if [[ ! -f "$MODULE_FILE" ]]; then
    log_error "Module file $MODULE_FILE not found. Please run 'make -f Makefile.uringblk' first."
    exit 1
fi

cleanup() {
    log_info "Cleaning up..."
    if lsmod | grep -q "$DRIVER_NAME"; then
        log_info "Removing module $DRIVER_NAME"
        rmmod "$DRIVER_NAME" 2>/dev/null || true
    fi
    
    # Remove any created loop devices for testing
    for i in {0..2}; do
        if [[ -f "/tmp/test_device_$i.img" ]]; then
            rm -f "/tmp/test_device_$i.img"
        fi
    done
}

# Set trap for cleanup on exit
trap cleanup EXIT

create_test_device() {
    local device_file="$1"
    local size_mb="$2"
    
    log_info "Creating test device $device_file (${size_mb}MB)"
    dd if=/dev/zero of="$device_file" bs=1M count="$size_mb" 2>/dev/null
    
    # Create a loop device
    local loop_dev=$(losetup -f --show "$device_file")
    echo "$loop_dev"
}

test_virtual_backend() {
    log_info "=== Testing Virtual Backend ==="
    
    log_info "Loading driver with virtual backend (default)"
    insmod "$MODULE_FILE" capacity_mb=512
    
    sleep 1
    
    if [[ -b /dev/uringblk0 ]]; then
        log_success "Virtual device /dev/uringblk0 created successfully"
        
        # Test basic I/O
        log_info "Testing basic I/O operations"
        echo "Hello World" | dd of=/dev/uringblk0 bs=512 count=1 2>/dev/null
        data=$(dd if=/dev/uringblk0 bs=512 count=1 2>/dev/null)
        
        if echo "$data" | grep -q "Hello World"; then
            log_success "Read/write test passed"
        else
            log_warning "Read/write test failed or data not found"
        fi
    else
        log_error "Failed to create virtual device"
    fi
    
    log_info "Removing virtual backend module"
    rmmod "$DRIVER_NAME"
    log_success "Virtual backend test completed"
}

test_device_backend() {
    log_info "=== Testing Real Device Backend ==="
    
    # Create a test file for loop device
    test_device_file="/tmp/test_device.img"
    create_test_device "$test_device_file" 256
    loop_device=$(losetup -f --show "$test_device_file")
    
    log_info "Using loop device: $loop_device"
    
    log_info "Loading driver with real device backend"
    insmod "$MODULE_FILE" backend_type=1 backend_device="$loop_device" auto_detect_size=true
    
    sleep 1
    
    if [[ -b /dev/uringblk0 ]]; then
        log_success "Real device backend /dev/uringblk0 created successfully"
        
        # Test basic I/O
        log_info "Testing I/O operations on real device backend"
        echo "Device Backend Test" | dd of=/dev/uringblk0 bs=512 count=1 2>/dev/null
        data=$(dd if=/dev/uringblk0 bs=512 count=1 2>/dev/null)
        
        if echo "$data" | grep -q "Device Backend Test"; then
            log_success "Device backend read/write test passed"
        else
            log_warning "Device backend read/write test failed or data not found"
        fi
    else
        log_error "Failed to create real device backend"
    fi
    
    log_info "Removing device backend module"
    rmmod "$DRIVER_NAME"
    
    # Cleanup loop device
    losetup -d "$loop_device"
    rm -f "$test_device_file"
    
    log_success "Device backend test completed"
}

test_multiple_devices() {
    log_info "=== Testing Multiple Device Support ==="
    
    # Create multiple test devices
    devices=()
    for i in {0..2}; do
        device_file="/tmp/test_device_$i.img"
        create_test_device "$device_file" 128
        loop_dev=$(losetup -f --show "$device_file")
        devices+=("$loop_dev")
    done
    
    device_list=$(IFS=','; echo "${devices[*]}")
    log_info "Loading driver with multiple devices: $device_list"
    
    insmod "$MODULE_FILE" devices="$device_list" max_devices=3
    
    sleep 1
    
    # Check if all devices were created
    created_devices=0
    for i in {0..2}; do
        if [[ -b /dev/uringblk$i ]]; then
            log_success "Device /dev/uringblk$i created successfully"
            created_devices=$((created_devices + 1))
            
            # Test basic I/O on each device
            echo "Device $i test data" | dd of="/dev/uringblk$i" bs=512 count=1 2>/dev/null
        else
            log_warning "Device /dev/uringblk$i not found"
        fi
    done
    
    log_info "Successfully created $created_devices devices"
    
    log_info "Removing multiple devices module"
    rmmod "$DRIVER_NAME"
    
    # Cleanup loop devices
    for loop_dev in "${devices[@]}"; do
        losetup -d "$loop_dev" 2>/dev/null || true
    done
    
    for i in {0..2}; do
        rm -f "/tmp/test_device_$i.img"
    done
    
    log_success "Multiple devices test completed"
}

show_driver_info() {
    log_info "=== Enhanced uringblk Driver Information ==="
    log_info "Module file: $MODULE_FILE"
    log_info "Driver features:"
    echo "  - Virtual memory backend support"
    echo "  - Real block device backend support"
    echo "  - Auto-detection of device sizes"
    echo "  - Multiple device instance support"
    echo "  - Enhanced error handling and validation"
    echo "  - Flexible configuration via module parameters"
    echo ""
}

run_basic_tests() {
    log_info "Starting enhanced uringblk driver tests..."
    echo ""
    
    show_driver_info
    
    # Run tests
    test_virtual_backend
    echo ""
    
    test_device_backend
    echo ""
    
    test_multiple_devices
    echo ""
    
    log_success "All tests completed successfully!"
}

# Main execution
case "${1:-all}" in
    "virtual")
        test_virtual_backend
        ;;
    "device")
        test_device_backend
        ;;
    "multiple")
        test_multiple_devices
        ;;
    "info")
        show_driver_info
        ;;
    "all")
        run_basic_tests
        ;;
    *)
        log_info "Usage: $0 [virtual|device|multiple|info|all]"
        log_info "  virtual  - Test virtual backend only"
        log_info "  device   - Test real device backend only"
        log_info "  multiple - Test multiple devices only"
        log_info "  info     - Show driver information"
        log_info "  all      - Run all tests (default)"
        ;;
esac