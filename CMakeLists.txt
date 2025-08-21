cmake_minimum_required(VERSION 3.25...3.30)

project(kvm_db
    VERSION 1.0.0
    LANGUAGES C CXX
    DESCRIPTION "C++23 KVM API capability probe using modern features"
)

# Only do these if this is the main project and not included via add_subdirectory
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    # Set global properties for clean project organization
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)

    # Include helpful modules
    include(GNUInstallDirs)
    include(CTest)

    # Add custom CMake modules path
    list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

    # Export compile commands for language servers (clangd, etc.)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

# Create interface targets for project-wide configuration
add_library(kvm_db_options INTERFACE)
add_library(kvm_db_warnings INTERFACE)

# Configure project options and warnings
include(cmake/ProjectOptions.cmake)
include(cmake/CompilerWarnings.cmake)
configure_project_options(kvm_db_options)
configure_project_warnings(kvm_db_warnings)

# Platform detection and Linux requirements
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(WARNING "This project is designed for Linux systems. Building on ${CMAKE_SYSTEM_NAME} may not work.")
endif()

# Check for required Linux headers using C++ compilation
include(CheckIncludeFileCXX)
include(CheckCXXSourceCompiles)

# Test linux/kvm.h with proper compilation context
check_cxx_source_compiles("
    #include <linux/kvm.h>
    int main() {
        int api_version = KVM_API_VERSION;
        return 0;
    }
" HAVE_LINUX_KVM_H)

if(NOT HAVE_LINUX_KVM_H)
    message(FATAL_ERROR "linux/kvm.h not found or not usable. This project requires Linux KVM headers.
    Try: sudo apt install linux-headers-$(uname -r)  # Ubuntu/Debian
    Or:  sudo dnf install kernel-headers             # RHEL/Fedora")
endif()

# Check for other required headers using C++ context
check_include_file_cxx("sys/ioctl.h" HAVE_SYS_IOCTL_H)
check_include_file_cxx("fcntl.h" HAVE_FCNTL_H)
check_include_file_cxx("unistd.h" HAVE_UNISTD_H)

# Additional verification that we can actually use KVM ioctls
check_cxx_source_compiles("
    #include <fcntl.h>
    #include <sys/ioctl.h>
    #include <linux/kvm.h>
    int main() {
        int fd = open(\"/dev/null\", O_RDONLY); // Don't actually open /dev/kvm in configure
        int version = KVM_GET_API_VERSION;
        int check_ext = KVM_CHECK_EXTENSION;
        return 0;
    }
" HAVE_KVM_IOCTLS)

if(NOT HAVE_KVM_IOCTLS)
    message(WARNING "KVM ioctl definitions not found. The build may not work correctly.")
endif()

# C++23 feature detection
include(cmake/CXX23Features.cmake)
detect_cxx23_features()

# Generate configuration header
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.h.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/kvm_db/config.h"
    @ONLY
)

# WAL Driver Integration
# Include and integrate the WAL kernel driver build system
include(cmake/DriverIntegration.cmake)
integrate_wal_driver()

# Option to control driver build behavior
option(BUILD_DRIVER_BY_DEFAULT "Build kernel driver as part of default build" OFF)
option(BUILD_DRIVER_TESTS "Build driver test programs" ON)

# Main executable
add_executable(kvm_db src/kvm_db.cc src/kvm_probe.cc src/kvm_wal.cc)

# Configure target properties
target_compile_features(kvm_db PRIVATE cxx_std_23)

# Link to our interface libraries
target_link_libraries(kvm_db PRIVATE
    kvm_db_options
    kvm_db_warnings
)

# Include directories
target_include_directories(kvm_db PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# If driver is available, add driver headers to include path
if(WAL_DRIVER_AVAILABLE)
    target_include_directories(kvm_db PRIVATE
        "${WAL_DRIVER_BUILD_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/driver"
    )
    target_compile_definitions(kvm_db PRIVATE HAVE_WAL_DRIVER=1)
endif()

# Linux-specific libraries
target_link_libraries(kvm_db PRIVATE pthread)

# Check if we need to link filesystem library separately (older GCC)
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
    #include <filesystem>
    int main() {
        std::filesystem::exists(\"/tmp\");
        return 0;
    }
" FILESYSTEM_NO_LINK_NEEDED)

if(NOT FILESYSTEM_NO_LINK_NEEDED)
    # Try linking with stdc++fs for older GCC versions
    target_link_libraries(kvm_db PRIVATE stdc++fs)
endif()

# Add subdirectories if they exist
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests" AND CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    add_subdirectory(tests)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/docs" AND CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    add_subdirectory(docs)
endif()

# Installation rules
include(cmake/Installation.cmake)
configure_installation()

# Print build summary
message(STATUS "")
message(STATUS "kvm_db Build Configuration Summary:")
message(STATUS "==================================")
message(STATUS "  C++ Standard:           C++23")
message(STATUS "  Build Type:             ${CMAKE_BUILD_TYPE}")
message(STATUS "  Install Prefix:         ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  KVM Headers:            ${HAVE_LINUX_KVM_H}")
if(WAL_DRIVER_AVAILABLE)
    message(STATUS "  WAL Driver:             ENABLED")
    message(STATUS "  Driver Build Dir:       ${WAL_DRIVER_BUILD_DIR}")
    message(STATUS "  Build Driver Default:   ${BUILD_DRIVER_BY_DEFAULT}")
    message(STATUS "  Build Driver Tests:     ${BUILD_DRIVER_TESTS}")
else()
    message(STATUS "  WAL Driver:             DISABLED")
endif()
message(STATUS "")

# Help message for driver usage
if(WAL_DRIVER_AVAILABLE)
    message(STATUS "WAL Driver Build Targets:")
    message(STATUS "  make wal_driver_build   - Build the kernel module")
    message(STATUS "  make wal_driver_load    - Load the kernel module (requires sudo)")
    message(STATUS "  make wal_driver_unload  - Unload the kernel module (requires sudo)")
    message(STATUS "  make wal_driver_reload  - Reload the kernel module (requires sudo)")
    message(STATUS "  make wal_driver_test    - Test the kernel module")
    message(STATUS "  make wal_driver_install - Install the kernel module (requires sudo)")
    if(TARGET wal_test)
        message(STATUS "  make run_wal_test       - Run the driver test program")
    endif()
    message(STATUS "")
endif()

