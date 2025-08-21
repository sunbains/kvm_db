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

  // Device cleanup happens automatically via RAII when wal_manager goes out of scope
  println("Shutdown: WAL devices will be cleaned up automatically...");

  return EXIT_SUCCESS;
}
