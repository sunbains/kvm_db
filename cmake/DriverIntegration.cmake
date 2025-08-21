# cmake/DriverIntegration.cmake - Kernel driver integration for CMake

# Find kernel build directory
function(find_kernel_build_dir)
    if(NOT DEFINED KERNEL_BUILD_DIR)
        # Try to auto-detect kernel build directory
        execute_process(
            COMMAND uname -r
            OUTPUT_VARIABLE KERNEL_VERSION
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        set(KERNEL_BUILD_CANDIDATES
            "/lib/modules/${KERNEL_VERSION}/build"
            "/usr/src/linux-headers-${KERNEL_VERSION}"
            "/usr/src/kernels/${KERNEL_VERSION}"
        )

        foreach(candidate ${KERNEL_BUILD_CANDIDATES})
            if(EXISTS "${candidate}")
                set(KERNEL_BUILD_DIR "${candidate}" CACHE PATH "Kernel build directory")
                break()
            endif()
        endforeach()
    endif()

    if(NOT EXISTS "${KERNEL_BUILD_DIR}")
        message(WARNING "Kernel build directory not found. Driver build will be disabled.")
        message(STATUS "Install kernel headers with:")
        message(STATUS "  Ubuntu/Debian: sudo apt install linux-headers-$(uname -r)")
        message(STATUS "  RHEL/Fedora:   sudo dnf install kernel-headers kernel-devel")
        set(KERNEL_BUILD_AVAILABLE FALSE PARENT_SCOPE)
    else()
        message(STATUS "Found kernel build directory: ${KERNEL_BUILD_DIR}")
        set(KERNEL_BUILD_AVAILABLE TRUE PARENT_SCOPE)
    endif()
endfunction()

# Check if we can build kernel modules
function(check_kernel_module_support)
    find_kernel_build_dir()

    if(NOT KERNEL_BUILD_AVAILABLE)
        return()
    endif()

    # Check if we have necessary tools
    find_program(MAKE_PROGRAM make)
    if(NOT MAKE_PROGRAM)
        message(WARNING "make program not found. Driver build disabled.")
        set(KERNEL_BUILD_AVAILABLE FALSE PARENT_SCOPE)
        return()
    endif()

    # Test if we can compile a simple kernel module
    set(TEST_DIR "${CMAKE_BINARY_DIR}/kernel_test")
    file(MAKE_DIRECTORY "${TEST_DIR}")

    # Create a minimal test module
    file(WRITE "${TEST_DIR}/test_module.c"
        "#include <linux/init.h>\n"
        "#include <linux/module.h>\n"
        "#include <linux/kernel.h>\n"
        "MODULE_LICENSE(\"GPL\");\n"
        "static int __init test_init(void) { return 0; }\n"
        "static void __exit test_exit(void) {}\n"
        "module_init(test_init);\n"
        "module_exit(test_exit);\n"
    )

    # Create proper Makefile with PWD variable
    file(WRITE "${TEST_DIR}/Makefile"
        "# Makefile for kernel module test\n"
        "obj-m += test_module.o\n"
        "\n"
        "# Define PWD if not set\n"
        "PWD := $(shell pwd)\n"
        "\n"
        "all:\n"
        "\t$(MAKE) -C ${KERNEL_BUILD_DIR} M=$(PWD) modules\n"
        "\n"
        "clean:\n"
        "\t$(MAKE) -C ${KERNEL_BUILD_DIR} M=$(PWD) clean\n"
        "\t@rm -f *.mod.c *.mod *.o *.ko *.symvers *.order\n"
    )

    # Try to build the test module with better error reporting
    execute_process(
        COMMAND ${MAKE_PROGRAM} -C "${TEST_DIR}" clean
        OUTPUT_QUIET ERROR_QUIET
    )

    execute_process(
        COMMAND ${MAKE_PROGRAM} -C "${TEST_DIR}"
        RESULT_VARIABLE test_result
        OUTPUT_VARIABLE test_output
        ERROR_VARIABLE test_error
    )

    # Cleanup test
    execute_process(
        COMMAND ${MAKE_PROGRAM} -C "${TEST_DIR}" clean
        OUTPUT_QUIET ERROR_QUIET
    )
    file(REMOVE_RECURSE "${TEST_DIR}")

    if(test_result EQUAL 0)
        message(STATUS "Kernel module compilation test: SUCCESS")
        set(KERNEL_BUILD_AVAILABLE TRUE PARENT_SCOPE)
    else()
        message(WARNING "Kernel module compilation test: FAILED")
        message(STATUS "Build output:")
        message(STATUS "${test_output}")
        message(STATUS "Error output:")
        message(STATUS "${test_error}")
        message(STATUS "Driver build will be disabled. Check kernel headers installation.")
        set(KERNEL_BUILD_AVAILABLE FALSE PARENT_SCOPE)
    endif()
endfunction()

# Add kernel driver as external project
function(add_kernel_driver)
    check_kernel_module_support()

    if(NOT KERNEL_BUILD_AVAILABLE)
        message(STATUS "Kernel driver build: DISABLED")
        return()
    endif()

    message(STATUS "Kernel driver build: ENABLED")

    # Define driver source directory
    set(DRIVER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/driver")
    set(DRIVER_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/driver")

    # Create driver build directory
    file(MAKE_DIRECTORY "${DRIVER_BINARY_DIR}")

    # Check if driver sources exist
    if(NOT EXISTS "${DRIVER_SOURCE_DIR}/wal_driver.h")
        message(WARNING "WAL driver header not found: ${DRIVER_SOURCE_DIR}/wal_driver.h")
        set(KERNEL_BUILD_AVAILABLE FALSE PARENT_SCOPE)
        return()
    endif()

    # Copy driver sources to build directory with proper source file detection
    configure_file(
        "${DRIVER_SOURCE_DIR}/wal_driver.h"
        "${DRIVER_BINARY_DIR}/wal_driver.h"
        COPYONLY
    )

    # Check for either wal_driver.c or wal_driver_main.c
    if(EXISTS "${DRIVER_SOURCE_DIR}/wal_driver_main.c")
        configure_file(
            "${DRIVER_SOURCE_DIR}/wal_driver_main.c"
            "${DRIVER_BINARY_DIR}/wal_driver_main.c"
            COPYONLY
        )
        set(DRIVER_SOURCE_FILE "wal_driver_main.c")
    elseif(EXISTS "${DRIVER_SOURCE_DIR}/wal_driver.c")
        configure_file(
            "${DRIVER_SOURCE_DIR}/wal_driver.c"
            "${DRIVER_BINARY_DIR}/wal_driver.c"
            COPYONLY
        )
        set(DRIVER_SOURCE_FILE "wal_driver.c")
    else()
        message(WARNING "WAL driver source not found. Looking for wal_driver.c or wal_driver_main.c")
        set(KERNEL_BUILD_AVAILABLE FALSE PARENT_SCOPE)
        return()
    endif()

    # Generate object file name from source file name
    string(REGEX REPLACE "\\.c$" ".o" DRIVER_OBJECT_FILE "${DRIVER_SOURCE_FILE}")
    
    # Create a proper Makefile for the driver
    file(WRITE "${DRIVER_BINARY_DIR}/Makefile"
        "# Makefile for WAL Driver Kernel Module\n"
        "\n"
        "# Module name\n"
        "MODULE_NAME := wal_driver\n"
        "\n"
        "# Source files - handle both naming conventions\n"
        "obj-m += \$(MODULE_NAME).o\n"
        "wal_driver-objs := ${DRIVER_OBJECT_FILE}\n"
        "\n"
        "# Kernel build directory\n"
        "KERNEL_DIR ?= ${KERNEL_BUILD_DIR}\n"
        "\n"
        "# Current directory\n"
        "PWD := \$(shell pwd)\n"
        "\n"
        "# Compiler flags\n"
        "EXTRA_CFLAGS += -Wall -Wextra -Wno-unused-parameter\n"
        "EXTRA_CFLAGS += -Wno-sign-compare -Wno-unused-function\n"
        "\n"
        "# Default target\n"
        "all: module\n"
        "\n"
        "# Build the kernel module\n"
        "module:\n"
        "\t@echo \"Building WAL driver kernel module...\"\n"
        "\t\$(MAKE) -C \$(KERNEL_DIR) M=\$(PWD) modules\n"
        "\t@echo \"Build complete. Module: \$(MODULE_NAME).ko\"\n"
        "\n"
        "# Clean build artifacts\n"
        "clean:\n"
        "\t@echo \"Cleaning build artifacts...\"\n"
        "\t\$(MAKE) -C \$(KERNEL_DIR) M=\$(PWD) clean\n"
        "\trm -f *.mod.c *.mod *.o *.ko *.symvers *.order .*.cmd\n"
        "\trm -rf .tmp_versions Module.symvers modules.order\n"
        "\t@echo \"Clean complete.\"\n"
        "\n"
        "# Install the module (requires root)\n"
        "install: module\n"
        "\t@echo \"Installing WAL driver module...\"\n"
        "\tsudo \$(MAKE) -C \$(KERNEL_DIR) M=\$(PWD) modules_install\n"
        "\tsudo depmod -a\n"
        "\t@echo \"Module installed. Use 'sudo modprobe \$(MODULE_NAME)' to load.\"\n"
        "\n"
        "# Load the module\n"
        "load: module\n"
        "\t@echo \"Loading WAL driver module...\"\n"
        "\tsudo insmod ./\$(MODULE_NAME).ko\n"
        "\t@echo \"Module loaded. Check dmesg for status.\"\n"
        "\n"
        "# Unload the module\n"
        "unload:\n"
        "\t@echo \"Unloading WAL driver module...\"\n"
        "\tsudo rmmod \$(MODULE_NAME) || true\n"
        "\t@echo \"Module unloaded.\"\n"
        "\n"
        "# Reload the module (unload + load)\n"
        "reload: unload load\n"
        "\n"
        "# Check module status\n"
        "status:\n"
        "\t@echo \"Checking WAL driver status...\"\n"
        "\t@if lsmod | grep -q \$(MODULE_NAME); then \\\\\n"
        "\t\techo \"Module \$(MODULE_NAME) is loaded.\"; \\\\\n"
        "\t\tls -l /dev/rwal /dev/wal 2>/dev/null || echo \"Device files not found.\"; \\\\\n"
        "\telse \\\\\n"
        "\t\techo \"Module \$(MODULE_NAME) is not loaded.\"; \\\\\n"
        "\tfi\n"
        "\n"
        "# Test the devices (requires module to be loaded)\n"
        "test: status\n"
        "\t@echo \"Testing WAL devices...\"\n"
        "\t@if [ -c /dev/rwal ] && [ -b /dev/wal ]; then \\\\\n"
        "\t\techo \"Device files exist and have correct types.\"; \\\\\n"
        "\t\techo \"Running basic tests...\"; \\\\\n"
        "\t\techo \"Hello test\" | timeout 5 tee /dev/rwal > /dev/null || true; \\\\\n"
        "\t\ttimeout 5 dd if=/dev/wal bs=512 count=1 2>/dev/null | head -c 32 || true; \\\\\n"
        "\t\techo \"Check dmesg for captured data.\"; \\\\\n"
        "\telse \\\\\n"
        "\t\techo \"Device files not found. Make sure module is loaded.\"; \\\\\n"
        "\tfi\n"
        "\n"
        ".PHONY: all module clean install load unload reload status test\n"
    )

    # Add custom target to build the kernel module
    add_custom_target(wal_driver_build
        COMMAND ${MAKE_PROGRAM} KERNEL_DIR=${KERNEL_BUILD_DIR}
        WORKING_DIRECTORY "${DRIVER_BINARY_DIR}"
        COMMENT "Building WAL kernel driver"
        VERBATIM
    )

    # Add custom target to clean the kernel module
    add_custom_target(wal_driver_clean
        COMMAND ${MAKE_PROGRAM} clean KERNEL_DIR=${KERNEL_BUILD_DIR}
        WORKING_DIRECTORY "${DRIVER_BINARY_DIR}"
        COMMENT "Cleaning WAL kernel driver"
        VERBATIM
    )

    # Add custom target to load the kernel module (requires root)
    add_custom_target(wal_driver_load
        COMMAND ${MAKE_PROGRAM} load KERNEL_DIR=${KERNEL_BUILD_DIR}
        WORKING_DIRECTORY "${DRIVER_BINARY_DIR}"
        COMMENT "Loading WAL kernel driver (requires root)"
        VERBATIM
        DEPENDS wal_driver_build
    )

    # Add custom target to unload the kernel module (requires root)
    add_custom_target(wal_driver_unload
        COMMAND ${MAKE_PROGRAM} unload KERNEL_DIR=${KERNEL_BUILD_DIR}
        WORKING_DIRECTORY "${DRIVER_BINARY_DIR}"
        COMMENT "Unloading WAL kernel driver (requires root)"
        VERBATIM
    )

    # Add custom target to reload the kernel module (requires root)
    add_custom_target(wal_driver_reload
        COMMAND ${MAKE_PROGRAM} reload KERNEL_DIR=${KERNEL_BUILD_DIR}
        WORKING_DIRECTORY "${DRIVER_BINARY_DIR}"
        COMMENT "Reloading WAL kernel driver (requires root)"
        VERBATIM
        DEPENDS wal_driver_build
    )

    # Add custom target to test the driver
    add_custom_target(wal_driver_test
        COMMAND ${MAKE_PROGRAM} test KERNEL_DIR=${KERNEL_BUILD_DIR}
        WORKING_DIRECTORY "${DRIVER_BINARY_DIR}"
        COMMENT "Testing WAL kernel driver"
        VERBATIM
        DEPENDS wal_driver_build
    )

    # Add custom target to install the kernel module
    add_custom_target(wal_driver_install
        COMMAND ${MAKE_PROGRAM} install KERNEL_DIR=${KERNEL_BUILD_DIR}
        WORKING_DIRECTORY "${DRIVER_BINARY_DIR}"
        COMMENT "Installing WAL kernel driver (requires root)"
        VERBATIM
        DEPENDS wal_driver_build
    )

    # Optional: Build driver by default if requested
    option(BUILD_DRIVER_BY_DEFAULT "Build kernel driver as part of default build" OFF)
    if(BUILD_DRIVER_BY_DEFAULT)
        add_dependencies(kvm_db wal_driver_build)
    endif()

    # Add to clean target
    set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_MAKE_CLEAN_FILES
        "${DRIVER_BINARY_DIR}"
    )

    # Export variables for use in other CMake files
    set(WAL_DRIVER_AVAILABLE TRUE PARENT_SCOPE)
    set(WAL_DRIVER_BUILD_DIR "${DRIVER_BINARY_DIR}" PARENT_SCOPE)
endfunction()

# Add test program for the driver
function(add_driver_test_program)
    if(NOT WAL_DRIVER_AVAILABLE)
        return()
    endif()

    set(TEST_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/driver")
    set(TEST_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/driver")

    # Check if test program source exists
    if(EXISTS "${TEST_SOURCE_DIR}/wal_test.c")
        # Enable C language for this target
        enable_language(C)
        
        # Build test program
        add_executable(wal_test
            "${TEST_SOURCE_DIR}/wal_test.c"
        )

        # Set language explicitly for the target
        set_target_properties(wal_test PROPERTIES
            LINKER_LANGUAGE C
            C_STANDARD 99
            C_STANDARD_REQUIRED ON
            RUNTIME_OUTPUT_DIRECTORY "${TEST_BINARY_DIR}"
        )

        # Include driver headers
        target_include_directories(wal_test PRIVATE
            "${TEST_SOURCE_DIR}"
            "${TEST_BINARY_DIR}"
        )

        # Add custom target to run the test program
        add_custom_target(run_wal_test
            COMMAND ${CMAKE_CURRENT_BINARY_DIR}/driver/wal_test
            WORKING_DIRECTORY "${TEST_BINARY_DIR}"
            COMMENT "Running WAL driver test program"
            VERBATIM
            DEPENDS wal_test wal_driver_load
        )

        message(STATUS "WAL driver test program: ENABLED")
    else()
        message(STATUS "WAL driver test program source not found")
    endif()
endfunction()

# Integration function to call from main CMakeLists.txt
function(integrate_wal_driver)
    message(STATUS "Integrating WAL kernel driver...")

    # Enable C language for driver test program
    enable_language(C)

    # Add driver build targets
    add_kernel_driver()

    # Add test program
    add_driver_test_program()

    if(WAL_DRIVER_AVAILABLE)
        message(STATUS "WAL driver integration: SUCCESS")
        message(STATUS "Available targets:")
        message(STATUS "  wal_driver_build   - Build the kernel module")
        message(STATUS "  wal_driver_load    - Load the kernel module (requires root)")
        message(STATUS "  wal_driver_unload  - Unload the kernel module (requires root)")
        message(STATUS "  wal_driver_reload  - Reload the kernel module (requires root)")
        message(STATUS "  wal_driver_test    - Test the kernel module")
        message(STATUS "  wal_driver_install - Install the kernel module (requires root)")
        if(TARGET wal_test)
            message(STATUS "  run_wal_test       - Run the driver test program")
        endif()
    else()
        message(STATUS "WAL driver integration: DISABLED")
        message(STATUS "Install kernel headers to enable driver build")
    endif()
endfunction()

