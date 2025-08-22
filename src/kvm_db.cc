#include <fcntl.h>
#include <linux/kvm.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>  // For makedev, major, minor
#include <sys/types.h>
#include <unistd.h>

#include <cstring>  // For strerror, strcmp
#include <filesystem>
#include <memory>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "kvm_db/config.h"
#include "kvm_probe.h"
#include "kvm_wal.h"
#include "kvm_uringblk.h"

using namespace kvm_db;

int main() {
  println("=== KVM Database Probe with WAL Devices ===\n");

  // Check if running as root (required for device creation)
  if (geteuid() != 0) {
    println("Warning: Not running as root. Device creation may fail.");
    println("Try: sudo ./kvm_db\n");
  }

  WAL_device_manager wal_manager;

  println("Creating WAL devices...");
  if (auto result = wal_manager.create_devices(); !result) {
    println("Failed to create WAL devices: {}", result.error().message());
    println("Make sure you're running as root (sudo)");
    return 1;
  }

  if (auto result = wal_manager.test_devices(); !result) {
    println("Warning: WAL device test failed: {}", result.error().message());
  } else {
    println("All WAL devices verified successfully");
  }

  println("");  // Empty line for readability

  KVM_probe probe;

  if (auto result = probe.initialize(); !result) {
    println("Failed to initialize KVM: {}", result.error().message());
    println("Make sure:");
    println("1. KVM is loaded (modprobe kvm kvm-intel/kvm-amd)");
    println("2. /dev/kvm exists and is accessible");
    println("3. You have proper permissions");

    return EXIT_FAILURE;
  }

  probe.print_capabilities();

  println("\n=== Testing WAL Device Interface ===");

  WAL_device_interface wal_interface;

  // Open the devices for I/O operations
  if (auto result = wal_interface.open_devices(); !result) {
    println("Failed to open WAL devices for I/O: {}", result.error().message());
    println("Note: This is expected if no kernel driver is loaded for these devices");
  } else {
    result = wal_interface.test_device_operations();
    if (!result) {
      println("WAL device operation tests failed: {}", result.error().message());
    }
    // Devices will be closed automatically via RAII
  }

#if HAVE_URINGBLK_DRIVER
  println("\n=== Testing uringblk Driver Interface ===");

  UringBlkManager uringblk_manager;
  
  // Check if uringblk driver is loaded
  if (auto driver_loaded = uringblk_manager.is_driver_loaded(); driver_loaded && *driver_loaded) {
    println("uringblk driver is loaded");
    
    if (auto version = uringblk_manager.get_driver_version(); version) {
      println("Driver version: {}", *version);
    }
    
    // Test all available uringblk devices
    if (auto test_result = uringblk_manager.test_all_devices(); !test_result) {
      println("uringblk device testing failed: {}", test_result.error().message());
    }
    
    // Test high-performance I/O operations
    println("\n--- Testing High-Performance I/O ---");
    auto devices = uringblk_manager.enumerate_devices();
    if (devices && !devices->empty()) {
      UringBlkDevice device;
      if (auto open_result = device.open_device(devices->front()); open_result) {
        println("Testing async I/O operations on {}", devices->front());
        
        // Test async read/write operations
        std::vector<uint8_t> test_data(4096, 0x42);  // 4KB test buffer
        std::vector<uint8_t> read_buffer(4096, 0);
        
        // Test write operation
        if (auto write_result = device.write_async(0, test_data.data(), test_data.size()); write_result) {
          println("Async write completed: {} bytes written", *write_result);
          
          // Test read operation
          if (auto read_result = device.read_async(0, read_buffer.data(), read_buffer.size()); read_result) {
            println("Async read completed: {} bytes read", *read_result);
            
            // Verify data integrity
            bool data_matches = std::equal(test_data.begin(), test_data.end(), read_buffer.begin());
            println("Data integrity check: {}", data_matches ? "PASSED" : "FAILED");
          } else {
            println("Async read failed: {}", read_result.error().message());
          }
        } else {
          println("Async write failed: {}", write_result.error().message());
        }
        
        // Test flush operation
        if (auto flush_result = device.flush_async(); flush_result) {
          println("Async flush completed successfully");
        } else {
          println("Async flush failed: {}", flush_result.error().message());
        }
      }
    }
  } else {
    println("uringblk driver is not loaded");
    println("To load the driver, run: sudo make uringblk_driver_load");
    println("Note: This requires the uringblk kernel module to be built first");
  }
#else
  println("\n=== uringblk Driver Support ===");
  println("uringblk driver support is not compiled in");
  println("Rebuild with HAVE_URINGBLK_DRIVER=1 to enable uringblk support");
#endif

  // Device cleanup happens automatically via RAII when wal_manager goes out of scope
  println("\nShutdown: WAL devices will be cleaned up automatically...");

  return EXIT_SUCCESS;
}
