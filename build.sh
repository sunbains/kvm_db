#!/bin/bash
# build.sh - Build script for kvm_db with WAL driver integration

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
BUILD_DIR="build"
BUILD_TYPE="${BUILD_TYPE:-Release}"
DRIVER_BUILD="${DRIVER_BUILD:-ON}"
DRIVER_TESTS="${DRIVER_TESTS:-ON}"

print_usage() {
    echo "Usage: $0 [options] [target]"
    echo ""
    echo "Options:"
    echo "  -h, --help          Show this help"
    echo "  -d, --debug         Build in Debug mode"
    echo "  -r, --release       Build in Release mode (default)"
    echo "  --no-driver         Disable driver build"
    echo "  --no-tests          Disable driver tests"
    echo "  -c, --clean         Clean build directory"
    echo "  -v, --verbose       Verbose build output"
    echo ""
    echo "Targets:"
    echo "  configure           Configure build (default)"
    echo "  build               Build everything"
    echo "  driver              Build WAL driver only"
    echo "  driver-load         Load WAL driver (requires sudo)"
    echo "  driver-unload       Unload WAL driver (requires sudo)"
    echo "  driver-reload       Reload WAL driver (requires sudo)"
    echo "  driver-test         Test WAL driver"
    echo "  driver-status       Check WAL driver status"
    echo "  test                Run all tests"
    echo "  install             Install everything"
    echo "  clean               Clean build directory"
    echo ""
    echo "Examples:"
    echo "  $0                  # Configure and build"
    echo "  $0 build            # Build everything"
    echo "  $0 driver-load      # Load driver (requires sudo)"
    echo "  $0 driver-test      # Test driver functionality"
    echo "  $0 --debug build    # Debug build"
    echo "  $0 --clean          # Clean and reconfigure"
}

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

check_requirements() {
    log_info "Checking requirements..."
    
    # Check for CMake
    if ! command -v cmake &> /dev/null; then
        log_error "CMake not found. Please install CMake 3.25 or later."
        exit 1
    fi
    
    # Check CMake version
    CMAKE_VERSION=$(cmake --version | head -n1 | cut -d' ' -f3)
    log_info "Found CMake version: $CMAKE_VERSION"
    
    # Check for make
    if ! command -v make &> /dev/null; then
        log_error "make not found. Please install make."
        exit 1
    fi
    
    # Check for C++ compiler
    if ! command -v g++ &> /dev/null && ! command -v clang++ &> /dev/null; then
        log_error "C++ compiler not found. Please install g++ or clang++."
        exit 1
    fi
    
    # Check for kernel headers if driver build is enabled
    if [ "$DRIVER_BUILD" = "ON" ]; then
        KERNEL_VERSION=$(uname -r)
        KERNEL_HEADERS="/lib/modules/$KERNEL_VERSION/build"
        
        if [ ! -d "$KERNEL_HEADERS" ]; then
            log_warning "Kernel headers not found at $KERNEL_HEADERS"
            log_warning "Driver build will be disabled."
            log_info "To enable driver build, install kernel headers:"
            log_info "  Ubuntu/Debian: sudo apt install linux-headers-\$(uname -r)"
            log_info "  RHEL/Fedora:   sudo dnf install kernel-headers kernel-devel"
        else
            log_success "Kernel headers found at $KERNEL_HEADERS"
        fi
    fi
    
    # Check for /dev/kvm
    if [ -e "/dev/kvm" ]; then
        log_success "KVM device found at /dev/kvm"
    else
        log_warning "KVM device not found. Some functionality may be limited."
        log_info "Make sure KVM is enabled and you have permissions to access /dev/kvm"
    fi
}

configure_build() {
    log_info "Configuring build..."
    
    # Create build directory
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    # Configure with CMake
    cmake \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DBUILD_DRIVER_BY_DEFAULT="$DRIVER_BUILD" \
        -DBUILD_DRIVER_TESTS="$DRIVER_TESTS" \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        ..
    
    log_success "Build configured successfully"
    cd ..
}

build_project() {
    log_info "Building project..."
    
    if [ ! -d "$BUILD_DIR" ]; then
        configure_build
    fi
    
    cd "$BUILD_DIR"
    
    if [ "$VERBOSE" = "ON" ]; then
        make -j$(nproc) VERBOSE=1
    else
        make -j$(nproc)
    fi
    
    # Build driver if enabled
    if [ "$DRIVER_BUILD" = "ON" ]; then
        log_info "Building WAL driver as part of default build..."
        if make help 2>/dev/null | grep -q wal_driver_build; then
            make wal_driver_build
            log_success "WAL driver built successfully"
        else
            log_warning "WAL driver targets not available. Skipping driver build."
        fi
    fi
    
    log_success "Build completed successfully"
    cd ..
}

build_driver() {
    log_info "Building WAL driver..."
    
    if [ ! -d "$BUILD_DIR" ]; then
        configure_build
    fi
    
    cd "$BUILD_DIR"
    
    # Check if driver targets are available
    if ! make help 2>/dev/null | grep -q wal_driver_build; then
        log_error "WAL driver targets not available. Driver build may be disabled."
        log_info "Make sure kernel headers are installed and try reconfiguring."
        cd ..
        exit 1
    fi
    
    make wal_driver_build
    log_success "WAL driver built successfully"
    cd ..
}

load_driver() {
    log_info "Loading WAL driver..."
    
    if [ ! -d "$BUILD_DIR" ]; then
        log_error "Build directory not found. Run 'build' first."
        exit 1
    fi
    
    cd "$BUILD_DIR"
    
    # Build driver first if not built
    if [ ! -f "driver/wal_driver.ko" ]; then
        log_info "Driver not built yet. Building first..."
        make wal_driver_build
    fi
    
    # Check if running as root
    if [ "$EUID" -ne 0 ]; then
        log_info "Root privileges required. Running with sudo..."
        sudo make wal_driver_load
    else
        make wal_driver_load
    fi
    
    log_success "WAL driver loaded successfully"
    log_info "Driver devices should be available at:"
    log_info "  Character device: /dev/rwal"
    log_info "  Block device:     /dev/wal"
    log_info "  Proc entry:       /proc/wal_driver"
    cd ..
}

unload_driver() {
    log_info "Unloading WAL driver..."
    
    if [ ! -d "$BUILD_DIR" ]; then
        log_error "Build directory not found."
        exit 1
    fi
    
    cd "$BUILD_DIR"
    
    # Check if running as root
    if [ "$EUID" -ne 0 ]; then
        log_info "Root privileges required. Running with sudo..."
        sudo make wal_driver_unload
    else
        make wal_driver_unload
    fi
    
    log_success "WAL driver unloaded successfully"
    cd ..
}

reload_driver() {
    log_info "Reloading WAL driver..."
    
    if [ ! -d "$BUILD_DIR" ]; then
        log_error "Build directory not found. Run 'build' first."
        exit 1
    fi
    
    cd "$BUILD_DIR"
    
    # Check if running as root
    if [ "$EUID" -ne 0 ]; then
        log_info "Root privileges required. Running with sudo..."
        sudo make wal_driver_reload
    else
        make wal_driver_reload
    fi
    
    log_success "WAL driver reloaded successfully"
    cd ..
}

test_driver() {
    log_info "Testing WAL driver..."
    
    if [ ! -d "$BUILD_DIR" ]; then
        log_error "Build directory not found. Run 'build' first."
        exit 1
    fi
    
    cd "$BUILD_DIR"
    
    # Check if driver is loaded
    if ! lsmod | grep -q wal_driver; then
        log_warning "WAL driver not loaded. Loading it first..."
        if [ "$EUID" -ne 0 ]; then
            sudo make wal_driver_load
        else
            make wal_driver_load
        fi
    fi
    
    # Check if test program exists
    if [ ! -f "driver/wal_test" ]; then
        log_info "Test program not built. Building it first..."
        make wal_test
    fi
    
    # Run tests
    log_info "Running WAL driver tests..."
    make run_wal_test
    
    log_success "WAL driver tests completed"
    cd ..
}

driver_status() {
    log_info "Checking WAL driver status..."
    
    if [ ! -d "$BUILD_DIR" ]; then
        log_error "Build directory not found."
        exit 1
    fi
    
    cd "$BUILD_DIR"
    make wal_driver_status
    echo ""
    log_info "Recent kernel messages:"
    make wal_driver_dmesg
    cd ..
}

run_tests() {
    log_info "Running tests..."
    
    if [ ! -d "$BUILD_DIR" ]; then
        log_error "Build directory not found. Run 'build' first."
        exit 1
    fi
    
    cd "$BUILD_DIR"
    
    # Run CTest if available
    if command -v ctest &> /dev/null; then
        ctest --output-on-failure
    else
        log_warning "CTest not available. Running manual tests..."
        
        # Run the main program
        if [ -f "kvm_db" ]; then
            log_info "Running main program..."
            ./kvm_db
        fi
        
        # Run driver tests if available
        if [ -f "driver/wal_test" ]; then
            log_info "Running driver tests..."
            make run_wal_test
        fi
    fi
    
    log_success "Tests completed"
    cd ..
}

install_project() {
    log_info "Installing project..."
    
    if [ ! -d "$BUILD_DIR" ]; then
        log_error "Build directory not found. Run 'build' first."
        exit 1
    fi
    
    cd "$BUILD_DIR"
    
    # Check if running as root
    if [ "$EUID" -ne 0 ]; then
        log_info "Root privileges required. Running with sudo..."
        sudo make install
    else
        make install
    fi
    
    log_success "Project installed successfully"
    cd ..
}

clean_build() {
    log_info "Cleaning build directory..."
    
    if [ -d "$BUILD_DIR" ]; then
        rm -rf "$BUILD_DIR"
        log_success "Build directory cleaned"
    else
        log_info "Build directory does not exist"
    fi
}

show_help_targets() {
    if [ -d "$BUILD_DIR" ]; then
        cd "$BUILD_DIR"
        log_info "Available make targets:"
        make help 2>/dev/null | grep -E "(wal_driver|test|install)" || true
        cd ..
    fi
}

# Parse command line arguments
VERBOSE="OFF"
TARGET="configure"

while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            print_usage
            exit 0
            ;;
        -d|--debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        -r|--release)
            BUILD_TYPE="Release"
            shift
            ;;
        --no-driver)
            DRIVER_BUILD="OFF"
            shift
            ;;
        --no-tests)
            DRIVER_TESTS="OFF"
            shift
            ;;
        -c|--clean)
            clean_build
            exit 0
            ;;
        -v|--verbose)
            VERBOSE="ON"
            shift
            ;;
        configure|build|driver|driver-load|driver-unload|driver-reload|driver-test|driver-status|test|install|clean|help-targets)
            TARGET="$1"
            shift
            ;;
        *)
            log_error "Unknown option: $1"
            print_usage
            exit 1
            ;;
    esac
done

# Main execution
echo ""
log_info "kvm_db Build Script"
log_info "==================="
log_info "Build Type: $BUILD_TYPE"
log_info "Driver Build: $DRIVER_BUILD"
log_info "Driver Tests: $DRIVER_TESTS"
log_info "Target: $TARGET"
echo ""

# Check requirements
check_requirements
echo ""

# Execute target
case $TARGET in
    configure)
        configure_build
        ;;
    build)
        build_project
        ;;
    driver)
        build_driver
        ;;
    driver-load)
        load_driver
        ;;
    driver-unload)
        unload_driver
        ;;
    driver-reload)
        reload_driver
        ;;
    driver-test)
        test_driver
        ;;
    driver-status)
        driver_status
        ;;
    test)
        run_tests
        ;;
    install)
        install_project
        ;;
    clean)
        clean_build
        ;;
    help-targets)
        show_help_targets
        ;;
    *)
        log_error "Unknown target: $TARGET"
        print_usage
        exit 1
        ;;
esac

echo ""
log_success "Script completed successfully"

# Show next steps
if [ "$TARGET" = "configure" ]; then
    echo ""
    log_info "Next steps:"
    log_info "  $0 build          # Build everything"
    log_info "  $0 driver-load    # Load WAL driver"
    log_info "  $0 driver-test    # Test WAL driver"
    log_info "  $0 help-targets   # Show available targets"
elif [ "$TARGET" = "build" ]; then
    echo ""
    log_info "Build completed. Next steps:"
    log_info "  $0 driver-load    # Load WAL driver"
    log_info "  $0 test           # Run tests"
    log_info "  $0 install        # Install project"
fi

