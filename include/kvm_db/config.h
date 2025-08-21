/*
 * config.h.in - Configuration header template for KVMDB
 * 
 * This file is processed by CMake to generate include/kvm_db/config.h
 * with the actual configuration values.
 */

#ifndef KVM_DB_CONFIG_H
#define KVM_DB_CONFIG_H

/* Project information */
#define KVM_DB_VERSION_MAJOR 
#define KVM_DB_VERSION_MINOR 
#define KVM_DB_VERSION_PATCH 
#define KVM_DB_VERSION ""

/* Platform detection */
#define HAVE_LINUX_KVM_H 1
#define HAVE_SYS_IOCTL_H 1
#define HAVE_FCNTL_H 1
#define HAVE_UNISTD_H 1
#define HAVE_KVM_IOCTLS 1

/* WAL Driver integration */
#define HAVE_WAL_DRIVER 1
#ifdef HAVE_WAL_DRIVER
    #define WAL_DRIVER_BUILD_DIR "/home/sunny/dev/kvm_db/build/driver"
    #define WAL_CHAR_DEVICE "/dev/rwal"
    #define WAL_BLOCK_DEVICE "/dev/wal"
    #define WAL_PROC_ENTRY "/proc/wal_driver"
#endif

/* C++23 features */
#define HAVE_CXX23_MODULES 0
#define HAVE_CXX23_RANGES 1
#define HAVE_CXX23_COROUTINES 1
#define HAVE_CXX23_CONCEPTS 1

/* Filesystem support */
#define FILESYSTEM_NO_LINK_NEEDED 1

/* Build configuration */
#define BUILD_DRIVER_BY_DEFAULT 1
#define BUILD_DRIVER_TESTS 1

/* System information */
#define CMAKE_SYSTEM_NAME "Linux"
#define CMAKE_BUILD_TYPE "Release"
#define CMAKE_CXX_COMPILER_ID "GNU"
#define CMAKE_CXX_COMPILER_VERSION "14.2.0"

/* Helpful macros */
#if HAVE_WAL_DRIVER
    #define KVM_DB_HAS_WAL_DRIVER 1
#else
    #define KVM_DB_HAS_WAL_DRIVER 0
#endif

#if HAVE_LINUX_KVM_H && HAVE_KVM_IOCTLS
    #define KVM_DB_HAS_KVM_SUPPORT 1
#else
    #define KVM_DB_HAS_KVM_SUPPORT 0
#endif

/* Debug helpers */
#ifdef NDEBUG
    #define KVM_DB_DEBUG 0
#else
    #define KVM_DB_DEBUG 1
#endif

/* Compiler-specific features */
#ifdef __GNUC__
    #define KVM_DB_LIKELY(x)   __builtin_expect(!!(x), 1)
    #define KVM_DB_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
    #define KVM_DB_LIKELY(x)   (x)
    #define KVM_DB_UNLIKELY(x) (x)
#endif

#endif /* KVM_DB_CONFIG_H */

