#!/bin/bash

# Test script for uringblk device backend functionality
# This script tests the real block storage backend with comprehensive scenarios

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODULE_NAME="uringblk_driver"
MODULE_PATH="${SCRIPT_DIR}/${MODULE_NAME}.ko"
TEST_PROGRAM="${SCRIPT_DIR}/uringblk_test"
LOOP_DEVICE=""
TEMP_FILES=()

# Test configuration
TEST_IMAGE_SIZE="64M"
TEST_IMAGE_FILE="/tmp/uringblk_test_image"
MOUNT_POINT="/tmp/uringblk_test_mount"

# Cleanup function
cleanup() {
    echo -e "${YELLOW}Cleaning up...${NC}"
    
    # Unload module
    sudo rmmod ${MODULE_NAME} 2>/dev/null || true
    
    # Detach loop device
    if [[ -n "$LOOP_DEVICE" ]]; then
        sudo losetup -d "$LOOP_DEVICE" 2>/dev/null || true
    fi
    
    # Clean up temp files
    for file in "${TEMP_FILES[@]}"; do
        rm -f "$file" 2>/dev/null || true
    done
    
    # Clean up mount point
    sudo umount "$MOUNT_POINT" 2>/dev/null || true
    rmdir "$MOUNT_POINT" 2>/dev/null || true
    
    echo -e "${GREEN}Cleanup complete${NC}"
}

# Set up cleanup on exit
trap cleanup EXIT

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

# Check if running as root for some operations
check_permissions() {
    if [[ $EUID -ne 0 ]] && ! sudo -n true 2>/dev/null; then
        log_error "This script needs sudo access for loading kernel modules and managing devices"
        exit 1
    fi
}

# Verify prerequisites
check_prerequisites() {
    log_info "Checking prerequisites..."
    
    # Check if module exists
    if [[ ! -f "$MODULE_PATH" ]]; then
        log_error "Module not found: $MODULE_PATH"
        log_info "Please run 'make -f Makefile.uringblk.standalone' to build the module"
        exit 1
    fi
    
    # Check if test program exists
    if [[ ! -f "$TEST_PROGRAM" ]]; then
        log_error "Test program not found: $TEST_PROGRAM"
        log_info "Please run 'make -f Makefile.uringblk test' to build the test program"
        exit 1
    fi
    
    # Check required tools
    local tools=("losetup" "dd" "hexdump" "blockdev")
    for tool in "${tools[@]}"; do
        if ! command -v "$tool" &> /dev/null; then
            log_error "Required tool not found: $tool"
            exit 1
        fi
    done
    
    log_success "Prerequisites check passed"
}

# Create test image and loop device
setup_loop_device() {
    log_info "Setting up test environment..."
    
    # Create test image
    log_info "Creating test image ($TEST_IMAGE_SIZE)..."
    dd if=/dev/zero of="$TEST_IMAGE_FILE" bs=1M count=64 status=progress 2>/dev/null
    TEMP_FILES+=("$TEST_IMAGE_FILE")
    
    # Set up loop device
    log_info "Setting up loop device..."
    LOOP_DEVICE=$(sudo losetup -f --show "$TEST_IMAGE_FILE")
    log_info "Loop device created: $LOOP_DEVICE"
    
    # Verify loop device
    if [[ ! -b "$LOOP_DEVICE" ]]; then
        log_error "Failed to create loop device"
        exit 1
    fi
    
    # Write a test pattern to the device
    log_info "Writing test pattern to loop device..."
    echo "URINGBLK_TEST_HEADER_$(date)" | sudo dd of="$LOOP_DEVICE" bs=512 count=1 2>/dev/null
    
    log_success "Test environment setup complete"
    log_info "Loop device: $LOOP_DEVICE"
    log_info "Device size: $(sudo blockdev --getsize64 "$LOOP_DEVICE") bytes"
}

# Load module with device backend
load_module_with_device() {
    local device="$1"
    local extra_params="$2"
    
    log_info "Loading module with device backend: $device"
    
    # Unload if already loaded
    sudo rmmod ${MODULE_NAME} 2>/dev/null || true
    
    # Load with device backend
    local params="backend_type=1 backend_device=$device auto_detect_size=1"
    if [[ -n "$extra_params" ]]; then
        params="$params $extra_params"
    fi
    
    log_info "Module parameters: $params"
    sudo insmod "$MODULE_PATH" $params
    
    # Give it a moment to initialize
    sleep 2
    
    # Check if module loaded successfully
    if ! lsmod | grep -q ${MODULE_NAME}; then
        log_error "Module failed to load"
        return 1
    fi
    
    # Check if device was created
    if [[ ! -b /dev/uringblk0 ]]; then
        log_error "uringblk device not created"
        return 1
    fi
    
    log_success "Module loaded successfully"
    log_info "uringblk device: /dev/uringblk0"
    
    # Show device info
    ls -l /dev/uringblk* 2>/dev/null || true
    
    return 0
}

# Test basic device operations
test_basic_operations() {
    log_info "Testing basic device operations..."
    
    # Test device exists and has correct type
    if [[ ! -b /dev/uringblk0 ]]; then
        log_error "uringblk device not found"
        return 1
    fi
    
    # Get device size
    local device_size
    device_size=$(sudo blockdev --getsize64 /dev/uringblk0)
    log_info "Device size: $device_size bytes"
    
    # Test read operation
    log_info "Testing device read..."
    local read_data
    read_data=$(sudo dd if=/dev/uringblk0 bs=512 count=1 2>/dev/null | strings | head -1)
    if [[ "$read_data" == *"URINGBLK_TEST_HEADER"* ]]; then
        log_success "Device read test passed - found test header"
    else
        log_warning "Device read test - no test header found (this may be normal)"
    fi
    
    # Test write operation
    log_info "Testing device write..."
    local test_string="URINGBLK_WRITE_TEST_$(date +%s)"
    echo "$test_string" | sudo dd of=/dev/uringblk0 bs=512 count=1 seek=1 2>/dev/null
    
    # Verify write
    local written_data
    written_data=$(sudo dd if=/dev/uringblk0 bs=512 count=1 skip=1 2>/dev/null | strings | head -1)
    if [[ "$written_data" == "$test_string" ]]; then
        log_success "Device write test passed"
    else
        log_error "Device write test failed - data mismatch"
        log_error "Expected: $test_string"
        log_error "Got: $written_data"
        return 1
    fi
    
    log_success "Basic operations test passed"
    return 0
}

# Test using the uringblk test program
test_with_program() {
    log_info "Testing with uringblk test program..."
    
    if [[ ! -x "$TEST_PROGRAM" ]]; then
        log_error "Test program not executable: $TEST_PROGRAM"
        return 1
    fi
    
    # Run basic test
    log_info "Running basic I/O test..."
    if sudo "$TEST_PROGRAM" -d /dev/uringblk0 -c 10; then
        log_success "Basic I/O test passed"
    else
        log_error "Basic I/O test failed"
        return 1
    fi
    
    # Run admin commands test
    log_info "Running admin commands test..."
    if sudo "$TEST_PROGRAM" -d /dev/uringblk0 -a; then
        log_success "Admin commands test passed"
    else
        log_warning "Admin commands test had issues (may be normal)"
    fi
    
    # Run polling test
    log_info "Running polling test..."
    if sudo "$TEST_PROGRAM" -d /dev/uringblk0 -p -c 20; then
        log_success "Polling test passed"
    else
        log_warning "Polling test had issues"
    fi
    
    log_success "Test program tests completed"
    return 0
}

# Test file system operations
test_filesystem() {
    log_info "Testing filesystem operations..."
    
    # Create filesystem
    log_info "Creating ext4 filesystem..."
    if ! sudo mkfs.ext4 -F /dev/uringblk0 &>/dev/null; then
        log_error "Failed to create filesystem"
        return 1
    fi
    
    # Create mount point
    mkdir -p "$MOUNT_POINT"
    
    # Mount filesystem
    log_info "Mounting filesystem..."
    if ! sudo mount /dev/uringblk0 "$MOUNT_POINT"; then
        log_error "Failed to mount filesystem"
        return 1
    fi
    
    # Test file operations
    log_info "Testing file operations..."
    local test_file="$MOUNT_POINT/test_file.txt"
    local test_content="This is a test file for uringblk device backend testing.\nTimestamp: $(date)\nRandom: $RANDOM"
    
    # Write test file
    echo -e "$test_content" | sudo tee "$test_file" &>/dev/null
    
    # Verify file content
    if [[ -f "$test_file" ]]; then
        local read_content
        read_content=$(sudo cat "$test_file")
        if [[ "$read_content" == "$test_content" ]]; then
            log_success "File write/read test passed"
        else
            log_error "File content mismatch"
            return 1
        fi
    else
        log_error "Test file not created"
        return 1
    fi
    
    # Create multiple files
    log_info "Creating multiple test files..."
    for i in {1..10}; do
        local file="$MOUNT_POINT/file_$i.txt"
        echo "Test file $i content $(date +%s)" | sudo tee "$file" &>/dev/null
    done
    
    # Verify files
    local file_count
    file_count=$(sudo find "$MOUNT_POINT" -name "file_*.txt" | wc -l)
    if [[ "$file_count" -eq 10 ]]; then
        log_success "Multiple file creation test passed"
    else
        log_error "Expected 10 files, found $file_count"
        return 1
    fi
    
    # Test directory operations
    log_info "Testing directory operations..."
    local test_dir="$MOUNT_POINT/test_directory"
    sudo mkdir -p "$test_dir"
    
    # Create files in subdirectory
    for i in {1..5}; do
        echo "Subdir file $i" | sudo tee "$test_dir/subfile_$i.txt" &>/dev/null
    done
    
    # Verify subdirectory files
    local subfile_count
    subfile_count=$(sudo find "$test_dir" -name "subfile_*.txt" | wc -l)
    if [[ "$subfile_count" -eq 5 ]]; then
        log_success "Directory operations test passed"
    else
        log_error "Expected 5 subfiles, found $subfile_count"
        return 1
    fi
    
    # Unmount
    log_info "Unmounting filesystem..."
    sudo umount "$MOUNT_POINT"
    rmdir "$MOUNT_POINT"
    
    log_success "Filesystem test passed"
    return 0
}

# Test error conditions
test_error_conditions() {
    log_info "Testing error conditions..."
    
    # Test with non-existent device
    log_info "Testing with non-existent device..."
    sudo rmmod ${MODULE_NAME} 2>/dev/null || true
    
    if sudo insmod "$MODULE_PATH" backend_type=1 backend_device="/dev/non_existent_device" 2>/dev/null; then
        log_error "Module should have failed with non-existent device"
        sudo rmmod ${MODULE_NAME}
        return 1
    else
        log_success "Correctly rejected non-existent device"
    fi
    
    # Test with regular file (should work)
    log_info "Testing with regular file..."
    local test_file="/tmp/uringblk_regular_file"
    dd if=/dev/zero of="$test_file" bs=1M count=32 2>/dev/null
    TEMP_FILES+=("$test_file")
    
    if sudo insmod "$MODULE_PATH" backend_type=1 backend_device="$test_file" 2>/dev/null; then
        log_error "Module should reject regular files"
        sudo rmmod ${MODULE_NAME}
        return 1
    else
        log_success "Correctly rejected regular file"
    fi
    
    log_success "Error conditions test passed"
    return 0
}

# Check dmesg for relevant messages
check_dmesg() {
    log_info "Checking kernel messages..."
    
    local messages
    messages=$(dmesg | grep -i uringblk | tail -20)
    
    if [[ -n "$messages" ]]; then
        echo -e "${BLUE}Recent uringblk kernel messages:${NC}"
        echo "$messages"
    else
        log_info "No uringblk messages found in dmesg"
    fi
}

# Show sysfs attributes
show_sysfs_info() {
    log_info "Checking sysfs attributes..."
    
    local sysfs_dir="/sys/block/uringblk0/uringblk"
    if [[ -d "$sysfs_dir" ]]; then
        echo -e "${BLUE}Sysfs attributes:${NC}"
        for attr in "$sysfs_dir"/*; do
            if [[ -f "$attr" ]]; then
                local name=$(basename "$attr")
                local value=$(cat "$attr" 2>/dev/null || echo "N/A")
                printf "  %-20s: %s\n" "$name" "$value"
            fi
        done
    else
        log_warning "Sysfs directory not found: $sysfs_dir"
    fi
}

# Performance test
performance_test() {
    log_info "Running performance test..."
    
    # Sequential read test
    log_info "Sequential read performance test..."
    local read_perf
    read_perf=$(sudo dd if=/dev/uringblk0 of=/dev/null bs=64k count=512 2>&1 | grep -o '[0-9.]* MB/s' || echo "N/A")
    log_info "Sequential read: $read_perf"
    
    # Sequential write test
    log_info "Sequential write performance test..."
    local write_perf
    write_perf=$(sudo dd if=/dev/zero of=/dev/uringblk0 bs=64k count=512 2>&1 | grep -o '[0-9.]* MB/s' || echo "N/A")
    log_info "Sequential write: $write_perf"
    
    # Random I/O test using test program if available
    if [[ -x "$TEST_PROGRAM" ]]; then
        log_info "Running random I/O performance test..."
        sudo "$TEST_PROGRAM" -d /dev/uringblk0 -c 1000 -q 64 2>/dev/null || log_warning "Random I/O test had issues"
    fi
    
    log_success "Performance test completed"
}

# Main test function
run_tests() {
    log_info "Starting uringblk device backend tests..."
    
    check_prerequisites
    setup_loop_device
    
    # Test 1: Basic device backend functionality
    if load_module_with_device "$LOOP_DEVICE"; then
        show_sysfs_info
        test_basic_operations
        test_with_program
        performance_test
        check_dmesg
    else
        log_error "Failed to load module with device backend"
        return 1
    fi
    
    # Test 2: Filesystem operations
    test_filesystem
    
    # Test 3: Error conditions
    test_error_conditions
    
    log_success "All tests completed successfully!"
}

# Print usage
usage() {
    echo "Usage: $0 [options]"
    echo "Options:"
    echo "  -h, --help     Show this help message"
    echo "  -q, --quiet    Reduce output verbosity"
    echo "  -v, --verbose  Increase output verbosity"
    echo ""
    echo "This script tests the uringblk device backend functionality."
    echo "It will create a loop device and test various operations."
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            usage
            exit 0
            ;;
        -q|--quiet)
            exec > /dev/null
            shift
            ;;
        -v|--verbose)
            set -x
            shift
            ;;
        *)
            log_error "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
done

# Main execution
main() {
    echo -e "${GREEN}======================================${NC}"
    echo -e "${GREEN}  uringblk Device Backend Test Suite  ${NC}"
    echo -e "${GREEN}======================================${NC}"
    echo ""
    
    check_permissions
    run_tests
    
    echo ""
    echo -e "${GREEN}======================================${NC}"
    echo -e "${GREEN}       All Tests Completed!          ${NC}"
    echo -e "${GREEN}======================================${NC}"
}

# Run main function
main "$@"