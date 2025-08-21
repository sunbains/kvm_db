#!/bin/bash
# debug_kernel.sh - Debug kernel module compilation issues

set -e

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

log_info "Kernel Module Compilation Debug"
log_info "==============================="

# Get kernel version
KERNEL_VERSION=$(uname -r)
log_info "Kernel version: $KERNEL_VERSION"

# Check kernel build directory
KERNEL_BUILD_DIR="/lib/modules/$KERNEL_VERSION/build"
log_info "Checking kernel build directory: $KERNEL_BUILD_DIR"

if [ ! -d "$KERNEL_BUILD_DIR" ]; then
    log_error "Kernel build directory not found!"
    log_info "Install kernel headers:"
    log_info "  Ubuntu/Debian: sudo apt install linux-headers-$KERNEL_VERSION"
    log_info "  RHEL/Fedora:   sudo dnf install kernel-headers kernel-devel"
    exit 1
else
    log_success "Kernel build directory found"
fi

# Check essential files in kernel build directory
log_info "Checking essential kernel build files..."

REQUIRED_FILES=(
    "Makefile"
    "Module.symvers"
    "include/linux/module.h"
    "include/linux/kernel.h"
    "include/linux/init.h"
    "scripts/mod/modpost"
)

for file in "${REQUIRED_FILES[@]}"; do
    if [ -e "$KERNEL_BUILD_DIR/$file" ]; then
        log_success "Found: $file"
    else
        log_warning "Missing: $file"
    fi
done

# Check if we can read kernel config
if [ -f "$KERNEL_BUILD_DIR/.config" ]; then
    log_success "Kernel config found"
    
    # Check some important config options
    log_info "Checking kernel configuration..."
    
    if grep -q "CONFIG_MODULES=y" "$KERNEL_BUILD_DIR/.config"; then
        log_success "Loadable module support enabled"
    else
        log_error "Loadable module support disabled!"
    fi
    
    if grep -q "CONFIG_MODVERSIONS=y" "$KERNEL_BUILD_DIR/.config"; then
        log_info "Module versioning enabled"
    fi
else
    log_warning "Kernel config not found at $KERNEL_BUILD_DIR/.config"
fi

# Check compiler
log_info "Checking compiler..."

if command -v gcc &> /dev/null; then
    GCC_VERSION=$(gcc --version | head -n1)
    log_success "GCC found: $GCC_VERSION"
else
    log_error "GCC not found!"
    log_info "Install GCC: sudo apt install gcc"
fi

# Check make
if command -v make &> /dev/null; then
    MAKE_VERSION=$(make --version | head -n1)
    log_success "Make found: $MAKE_VERSION"
else
    log_error "Make not found!"
    log_info "Install make: sudo apt install make"
fi

# Check for essential development packages
log_info "Checking development packages..."

PACKAGES=(
    "build-essential"
    "linux-headers-$KERNEL_VERSION"
)

for package in "${PACKAGES[@]}"; do
    if dpkg -l | grep -q "^ii.*$package"; then
        log_success "Package installed: $package"
    else
        log_warning "Package missing: $package"
        log_info "Install with: sudo apt install $package"
    fi
done

# Create test directory
TEST_DIR="/tmp/kernel_module_test_$$"
log_info "Creating test in: $TEST_DIR"
mkdir -p "$TEST_DIR"

# Create minimal test module
cat > "$TEST_DIR/test_module.c" << 'EOF'
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Test");
MODULE_DESCRIPTION("Test Module");
MODULE_VERSION("1.0");

static int __init test_init(void)
{
    printk(KERN_INFO "Test module loaded\n");
    return 0;
}

static void __exit test_exit(void)
{
    printk(KERN_INFO "Test module unloaded\n");
}

module_init(test_init);
module_exit(test_exit);
EOF

# Create Makefile
cat > "$TEST_DIR/Makefile" << EOF
obj-m += test_module.o

all:
	\$(MAKE) -C $KERNEL_BUILD_DIR M=\$(PWD) modules

clean:
	\$(MAKE) -C $KERNEL_BUILD_DIR M=\$(PWD) clean

.PHONY: all clean
EOF

log_info "Test files created. Attempting compilation..."

cd "$TEST_DIR"

# Try to compile with verbose output
log_info "Running make with verbose output..."
echo "----------------------------------------"

if make V=1; then
    log_success "Kernel module compilation test PASSED!"
    
    if [ -f "test_module.ko" ]; then
        log_success "Module file created: test_module.ko"
        file test_module.ko
        ls -la test_module.ko
    fi
else
    log_error "Kernel module compilation test FAILED!"
    
    echo ""
    log_info "Trying to get more detailed error information..."
    
    # Check if there are any obvious issues
    if ! gcc -v &> /dev/null; then
        log_error "GCC is not working properly"
    fi
    
    # Try a simple compilation without the kernel build system
    log_info "Testing basic compilation..."
    if gcc -c test_module.c -I"$KERNEL_BUILD_DIR/include" -I"$KERNEL_BUILD_DIR/arch/x86/include" 2>&1; then
        log_info "Basic compilation works, issue is with kernel build system"
    else
        log_error "Basic compilation also fails"
    fi
fi

echo "----------------------------------------"

# Check permissions
log_info "Checking permissions..."
ls -la "$KERNEL_BUILD_DIR" | head -5

# Cleanup
cd /
rm -rf "$TEST_DIR"

log_info "Debug complete. Check the output above for issues."

# Provide specific recommendations
echo ""
log_info "Common solutions:"
log_info "1. Install missing packages:"
log_info "   sudo apt update"
log_info "   sudo apt install build-essential linux-headers-\$(uname -r)"
echo ""
log_info "2. If using a custom kernel, ensure kernel headers match:"
log_info "   dpkg -l | grep linux-headers"
echo ""
log_info "3. If still failing, try:"
log_info "   sudo apt install --reinstall linux-headers-\$(uname -r)"
echo ""
log_info "4. For virtual machines, ensure you have the right kernel variant:"
log_info "   sudo apt install linux-headers-\$(uname -r) linux-headers-generic"

