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

    file(WRITE "${TEST_DIR}/Makefile"
        "obj-m += test_module.o\n"
        "all:\n"
        "\t\$(MAKE) -C ${KERNEL_BUILD_DIR} M=\$(shell pwd) modules\n"
        "clean:\n"
        "\t\$(MAKE) -C ${KERNEL_BUILD_DIR} M=\$(shell pwd) clean\n"
    )

    # Try to build the test module
    execute_process(
        COMMAND ${MAKE_PROGRAM} clean
        WORKING_DIRECTORY "${TEST_DIR}"
        OUTPUT_QUIET ERROR_QUIET
    )

    execute_process(
        COMMAND ${MAKE_PROGRAM}
        WORKING_DIRECTORY "${TEST_DIR}"
        RESULT_VARIABLE test_result
        OUTPUT_VARIABLE test_output
        ERROR_VARIABLE test_error
    )

    if(test_result EQUAL 0)
        message(STATUS "Kernel module compilation test: SUCCESS")
        set(KERNEL_BUILD_AVAILABLE TRUE PARENT_SCOPE)
    else()
        message(WARNING "Kernel module compilation test: FAILED")
        message(STATUS "Build command: ${MAKE_PROGRAM} -C ${TEST_DIR}")
        message(STATUS "Exit code: ${test_result}")
        if(test_output)
            message(STATUS "Build output:")
            message(STATUS "${test_output}")
        endif()
        if(test_error)
            message(STATUS "Build errors:")
            message(STATUS "${test_error}")
        endif()
        message(STATUS "Driver build will be disabled. Check kernel headers installation.")
        set(KERNEL_BUILD_AVAILABLE FALSE PARENT_SCOPE)
    endif()

    # Cleanup test
    execute_process(
        COMMAND ${MAKE_PROGRAM} clean
        WORKING_DIRECTORY "${TEST_DIR}"
        OUTPUT_QUIET ERROR_QUIET
    )
    file(REMOVE_RECURSE "${TEST_DIR}")
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

    # Copy driver sources to build directory
    configure_file(
        "${DRIVER_SOURCE_DIR}/wal_driver.h"
        "${DRIVER_BINARY_DIR}/wal_driver.h"
        COPYONLY
    )
    configure_file(
        "${DRIVER_SOURCE_DIR}/wal_driver_main.c"
        "${DRIVER_BINARY_DIR}/wal_driver_main.c"
        COPYONLY
    )
    configure_file(
        "${DRIVER_SOURCE_DIR}/Makefile"
        "${DRIVER_BINARY_DIR}/Makefile"
        COPYONLY
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

    # Export variables for use in other CMake files
    set(WAL_DRIVER_AVAILABLE TRUE PARENT_SCOPE)
    set(WAL_DRIVER_BUILD_DIR "${DRIVER_BINARY_DIR}" PARENT_SCOPE)

    # Store the driver build option for later use
    set(BUILD_DRIVER_BY_DEFAULT_OPTION OFF CACHE BOOL "Build kernel driver as part of default build")
    
    # Add to clean target
    set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_MAKE_CLEAN_FILES
        "${DRIVER_BINARY_DIR}"
    )
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
        # Build test program
        add_executable(wal_test
            "${TEST_SOURCE_DIR}/wal_test.c"
        )

        # Include driver headers
        target_include_directories(wal_test PRIVATE
            "${TEST_SOURCE_DIR}"
            "${TEST_BINARY_DIR}"
        )

        # Set output directory
        set_target_properties(wal_test PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${TEST_BINARY_DIR}"
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

# Function to add driver dependencies to main target (call after target is created)
function(add_driver_dependencies target_name)
    if(WAL_DRIVER_AVAILABLE AND BUILD_DRIVER_BY_DEFAULT_OPTION)
        if(TARGET ${target_name})
            add_dependencies(${target_name} wal_driver_build)
            message(STATUS "Added driver build dependency to ${target_name}")
        else()
            message(WARNING "Target ${target_name} does not exist, cannot add driver dependency")
        endif()
    endif()
endfunction()

# Integration function to call from main CMakeLists.txt
function(integrate_wal_driver)
    message(STATUS "Integrating WAL kernel driver...")

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
        message(STATUS "")
        message(STATUS "Call add_driver_dependencies(kvm_db) after creating the main target")
        message(STATUS "to optionally build the driver as part of the default build.")
    else()
        message(STATUS "WAL driver integration: DISABLED")
        message(STATUS "Install kernel headers to enable driver build")
    endif()
endfunction()

