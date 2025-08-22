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

# Add WAL kernel driver as external project
function(add_wal_driver)
    check_kernel_module_support()

    if(NOT KERNEL_BUILD_AVAILABLE)
        message(STATUS "WAL kernel driver build: DISABLED")
        return()
    endif()

    message(STATUS "WAL kernel driver build: ENABLED")

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
        "${DRIVER_SOURCE_DIR}/Makefile.wal.bak"
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
    
    # Also set in cache so it persists
    set(WAL_DRIVER_AVAILABLE TRUE CACHE BOOL "WAL driver is available" FORCE)
    set(WAL_DRIVER_BUILD_DIR "${DRIVER_BINARY_DIR}" CACHE PATH "WAL driver build directory" FORCE)

    # Store the driver build option for later use
    set(BUILD_DRIVER_BY_DEFAULT_OPTION OFF CACHE BOOL "Build kernel driver as part of default build")
    
    # Add to clean target
    set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_MAKE_CLEAN_FILES
        "${DRIVER_BINARY_DIR}"
    )
endfunction()

# Add uringblk kernel driver as external project
function(add_uringblk_driver)
    check_kernel_module_support()

    if(NOT KERNEL_BUILD_AVAILABLE)
        message(STATUS "uringblk kernel driver build: DISABLED")
        return()
    endif()

    message(STATUS "uringblk kernel driver build: ENABLED")

    # Define driver source directory
    set(DRIVER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/driver")
    set(URINGBLK_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/driver/uringblk")

    # Create driver build directory
    file(MAKE_DIRECTORY "${URINGBLK_BINARY_DIR}")

    # Copy uringblk driver sources to build directory
    file(GLOB URINGBLK_SOURCES "${DRIVER_SOURCE_DIR}/uringblk_*.c" "${DRIVER_SOURCE_DIR}/uringblk_*.h")
    foreach(source ${URINGBLK_SOURCES})
        get_filename_component(filename ${source} NAME)
        configure_file(
            "${source}"
            "${URINGBLK_BINARY_DIR}/${filename}"
            COPYONLY
        )
    endforeach()
    
    # Copy Makefile.uringblk as Makefile
    configure_file(
        "${DRIVER_SOURCE_DIR}/Makefile.uringblk"
        "${URINGBLK_BINARY_DIR}/Makefile"
        COPYONLY
    )

    # Add custom target to build the uringblk kernel module
    add_custom_target(uringblk_driver_build
        COMMAND ${MAKE_PROGRAM} KERNEL_DIR=${KERNEL_BUILD_DIR}
        WORKING_DIRECTORY "${URINGBLK_BINARY_DIR}"
        COMMENT "Building uringblk kernel driver"
        VERBATIM
    )

    # Add custom target to clean the uringblk kernel module
    add_custom_target(uringblk_driver_clean
        COMMAND ${MAKE_PROGRAM} clean KERNEL_DIR=${KERNEL_BUILD_DIR}
        WORKING_DIRECTORY "${URINGBLK_BINARY_DIR}"
        COMMENT "Cleaning uringblk kernel driver"
        VERBATIM
    )

    # Add custom target to load the uringblk kernel module (requires root)
    add_custom_target(uringblk_driver_load
        COMMAND ${MAKE_PROGRAM} load KERNEL_DIR=${KERNEL_BUILD_DIR}
        WORKING_DIRECTORY "${URINGBLK_BINARY_DIR}"
        COMMENT "Loading uringblk kernel driver (requires root)"
        VERBATIM
        DEPENDS uringblk_driver_build
    )

    # Add custom target to unload the uringblk kernel module (requires root)
    add_custom_target(uringblk_driver_unload
        COMMAND ${MAKE_PROGRAM} unload KERNEL_DIR=${KERNEL_BUILD_DIR}
        WORKING_DIRECTORY "${URINGBLK_BINARY_DIR}"
        COMMENT "Unloading uringblk kernel driver (requires root)"
        VERBATIM
    )

    # Add custom target to reload the uringblk kernel module (requires root)
    add_custom_target(uringblk_driver_reload
        COMMAND ${MAKE_PROGRAM} reload KERNEL_DIR=${KERNEL_BUILD_DIR}
        WORKING_DIRECTORY "${URINGBLK_BINARY_DIR}"
        COMMENT "Reloading uringblk kernel driver (requires root)"
        VERBATIM
        DEPENDS uringblk_driver_build
    )

    # Add custom target to test the uringblk driver
    add_custom_target(uringblk_driver_test
        COMMAND ${MAKE_PROGRAM} run-test KERNEL_DIR=${KERNEL_BUILD_DIR}
        WORKING_DIRECTORY "${URINGBLK_BINARY_DIR}"
        COMMENT "Testing uringblk kernel driver"
        VERBATIM
        DEPENDS uringblk_driver_build
    )

    # Add custom target for comprehensive testing
    add_custom_target(uringblk_driver_benchmark
        COMMAND ${MAKE_PROGRAM} benchmark KERNEL_DIR=${KERNEL_BUILD_DIR}
        WORKING_DIRECTORY "${URINGBLK_BINARY_DIR}"
        COMMENT "Benchmarking uringblk kernel driver"
        VERBATIM
        DEPENDS uringblk_driver_build
    )

    # Add custom target to show driver status
    add_custom_target(uringblk_driver_status
        COMMAND ${MAKE_PROGRAM} status KERNEL_DIR=${KERNEL_BUILD_DIR}
        WORKING_DIRECTORY "${URINGBLK_BINARY_DIR}"
        COMMENT "Checking uringblk kernel driver status"
        VERBATIM
    )

    # Add custom target to show driver stats
    add_custom_target(uringblk_driver_stats
        COMMAND ${MAKE_PROGRAM} stats KERNEL_DIR=${KERNEL_BUILD_DIR}
        WORKING_DIRECTORY "${URINGBLK_BINARY_DIR}"
        COMMENT "Showing uringblk kernel driver statistics"
        VERBATIM
    )

    # Add custom target to show dmesg logs
    add_custom_target(uringblk_driver_dmesg
        COMMAND ${MAKE_PROGRAM} dmesg KERNEL_DIR=${KERNEL_BUILD_DIR}
        WORKING_DIRECTORY "${URINGBLK_BINARY_DIR}"
        COMMENT "Showing uringblk kernel driver messages"
        VERBATIM
    )

    # Add custom target to install the uringblk kernel module
    add_custom_target(uringblk_driver_install
        COMMAND ${MAKE_PROGRAM} install KERNEL_DIR=${KERNEL_BUILD_DIR}
        WORKING_DIRECTORY "${URINGBLK_BINARY_DIR}"
        COMMENT "Installing uringblk kernel driver (requires root)"
        VERBATIM
        DEPENDS uringblk_driver_build
    )

    # Export variables for use in other CMake files
    set(URINGBLK_DRIVER_AVAILABLE TRUE PARENT_SCOPE)
    set(URINGBLK_DRIVER_BUILD_DIR "${URINGBLK_BINARY_DIR}" PARENT_SCOPE)
    
    # Also set in cache so it persists
    set(URINGBLK_DRIVER_AVAILABLE TRUE CACHE BOOL "uringblk driver is available" FORCE)
    set(URINGBLK_DRIVER_BUILD_DIR "${URINGBLK_BINARY_DIR}" CACHE PATH "uringblk driver build directory" FORCE)
    
    # Add to clean target
    set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_MAKE_CLEAN_FILES
        "${URINGBLK_BINARY_DIR}"
    )
endfunction()

# Add test program for the WAL driver
function(add_wal_test_program)
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

# Add test program for the uringblk driver
function(add_uringblk_test_program)
    if(NOT URINGBLK_DRIVER_AVAILABLE)
        return()
    endif()

    set(TEST_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/driver")
    set(URINGBLK_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/driver/uringblk")

    # Check if test program source exists
    if(EXISTS "${TEST_SOURCE_DIR}/uringblk_test.c")
        # The uringblk test program is built by the driver's Makefile
        # We just need to ensure liburing is available
        find_library(LIBURING_LIB uring)
        if(LIBURING_LIB)
            message(STATUS "uringblk driver test program: ENABLED (built by driver Makefile)")
        else()
            message(WARNING "liburing not found. uringblk test program may not build.")
            message(STATUS "Install liburing development package:")
            message(STATUS "  Ubuntu/Debian: sudo apt install liburing-dev")
            message(STATUS "  RHEL/Fedora:   sudo dnf install liburing-devel")
        endif()
    else()
        message(STATUS "uringblk driver test program source not found")
    endif()
endfunction()

# Function to add driver dependencies to main target (call after target is created)
function(add_driver_dependencies target_name)
    if(WAL_DRIVER_AVAILABLE AND BUILD_DRIVER_BY_DEFAULT_OPTION)
        if(TARGET ${target_name})
            add_dependencies(${target_name} wal_driver_build)
            message(STATUS "Added WAL driver build dependency to ${target_name}")
        else()
            message(WARNING "Target ${target_name} does not exist, cannot add WAL driver dependency")
        endif()
    endif()
    
    if(URINGBLK_DRIVER_AVAILABLE AND BUILD_DRIVER_BY_DEFAULT_OPTION)
        if(TARGET ${target_name})
            add_dependencies(${target_name} uringblk_driver_build)
            message(STATUS "Added uringblk driver build dependency to ${target_name}")
        else()
            message(WARNING "Target ${target_name} does not exist, cannot add uringblk driver dependency")
        endif()
    endif()
endfunction()

# Integration function to call from main CMakeLists.txt
function(integrate_wal_driver)
    message(STATUS "Integrating kernel drivers...")

    # Add WAL driver build targets
    add_wal_driver()

    # Add uringblk driver build targets
    add_uringblk_driver()

    # Add test programs
    add_wal_test_program()
    add_uringblk_test_program()

    if(WAL_DRIVER_AVAILABLE OR URINGBLK_DRIVER_AVAILABLE)
        message(STATUS "Driver integration: SUCCESS")
        message(STATUS "Available targets:")
        
        if(WAL_DRIVER_AVAILABLE)
            message(STATUS "  WAL Driver:")
            message(STATUS "    wal_driver_build   - Build the WAL kernel module")
            message(STATUS "    wal_driver_load    - Load the WAL kernel module (requires root)")
            message(STATUS "    wal_driver_unload  - Unload the WAL kernel module (requires root)")
            message(STATUS "    wal_driver_reload  - Reload the WAL kernel module (requires root)")
            message(STATUS "    wal_driver_test    - Test the WAL kernel module")
            message(STATUS "    wal_driver_install - Install the WAL kernel module (requires root)")
            if(TARGET wal_test)
                message(STATUS "    run_wal_test       - Run the WAL driver test program")
            endif()
        endif()
        
        if(URINGBLK_DRIVER_AVAILABLE)
            message(STATUS "  uringblk Driver:")
            message(STATUS "    uringblk_driver_build     - Build the uringblk kernel module")
            message(STATUS "    uringblk_driver_load      - Load the uringblk kernel module (requires root)")
            message(STATUS "    uringblk_driver_unload    - Unload the uringblk kernel module (requires root)")
            message(STATUS "    uringblk_driver_reload    - Reload the uringblk kernel module (requires root)")
            message(STATUS "    uringblk_driver_test      - Test the uringblk kernel module")
            message(STATUS "    uringblk_driver_benchmark - Benchmark the uringblk kernel module")
            message(STATUS "    uringblk_driver_status    - Check uringblk driver status")
            message(STATUS "    uringblk_driver_stats     - Show uringblk driver statistics")
            message(STATUS "    uringblk_driver_dmesg     - Show uringblk driver kernel messages")
            message(STATUS "    uringblk_driver_install   - Install the uringblk kernel module (requires root)")
        endif()
        
        message(STATUS "")
        message(STATUS "Call add_driver_dependencies(kvm_db) after creating the main target")
        message(STATUS "to optionally build the drivers as part of the default build.")
        
        # Propagate variables to parent scope
        set(WAL_DRIVER_AVAILABLE ${WAL_DRIVER_AVAILABLE} PARENT_SCOPE)
        set(WAL_DRIVER_BUILD_DIR "${WAL_DRIVER_BUILD_DIR}" PARENT_SCOPE)
        set(URINGBLK_DRIVER_AVAILABLE ${URINGBLK_DRIVER_AVAILABLE} PARENT_SCOPE)
        set(URINGBLK_DRIVER_BUILD_DIR "${URINGBLK_DRIVER_BUILD_DIR}" PARENT_SCOPE)
    else()
        message(STATUS "Driver integration: DISABLED")
        message(STATUS "Install kernel headers to enable driver builds")
        set(WAL_DRIVER_AVAILABLE FALSE PARENT_SCOPE)
        set(URINGBLK_DRIVER_AVAILABLE FALSE PARENT_SCOPE)
    endif()
endfunction()

