cmake_minimum_required(VERSION 3.25...3.30)

project(KVMDb
    VERSION 1.0.0
    DESCRIPTION "C++23 KVM API capability probe using modern features"
    LANGUAGES CXX
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
    "${CMAKE_CURRENT_BINARY_DIR}/include/kvm_db/config.h"
    @ONLY
)

# Main executable
add_executable(kvm_db src/kvm_db.cc)

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

# Linux-specific libraries
target_link_libraries(kvm_db PRIVATE pthread)

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

