#include <fcntl.h>
#include <linux/kvm.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>  // For makedev, major, minor
#include <sys/types.h>
#include <unistd.h>

#include <cassert>
#include <cstring>  // For strerror, strcmp
#include <filesystem>
#include <memory>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "kvm_wal.h"

using namespace kvm_db;

WAL_device_manager& WAL_device_manager::operator=(WAL_device_manager&& rhs) noexcept {
  if (this != &rhs) {
    cleanup_devices();
    m_char_device_created = std::exchange(rhs.m_char_device_created, false);
    m_block_device_created = std::exchange(rhs.m_block_device_created, false);
  }

  return *this;
}

std::expected<void, std::error_code> WAL_device_manager::create_devices() {
  // Create character device (/dev/rwal)
  if (auto result = create_single_device(CHAR_DEVICE_PATH, S_IFCHR, CHAR_DEVICE_MINOR); !result) {
    return result;
  }

  m_char_device_created = true;

  // Create block device (/dev/wal)
  if (auto result = create_single_device(BLOCK_DEVICE_PATH, S_IFBLK, BLOCK_DEVICE_MINOR); !result) {
    // Cleanup char device if block device creation fails
    cleanup_single_device(CHAR_DEVICE_PATH, m_char_device_created);
    return result;
  }

  m_block_device_created = true;

  println("Successfully created WAL devices:");
  println("  Character device: {} (major={}, minor={})", CHAR_DEVICE_PATH, DEVICE_MAJOR,
          CHAR_DEVICE_MINOR);
  println("  Block device:     {} (major={}, minor={})", BLOCK_DEVICE_PATH, DEVICE_MAJOR,
          BLOCK_DEVICE_MINOR);

  return {};
}

void WAL_device_manager::cleanup_devices() {
  cleanup_single_device(CHAR_DEVICE_PATH, m_char_device_created);
  cleanup_single_device(BLOCK_DEVICE_PATH, m_block_device_created);
}

bool WAL_device_manager::are_devices_accessible() const {
  return is_device_accessible(CHAR_DEVICE_PATH) && is_device_accessible(BLOCK_DEVICE_PATH);
}

std::expected<void, std::error_code> WAL_device_manager::test_devices() const {
  if (!m_char_device_created || !m_block_device_created) {
    return std::unexpected(std::error_code{ENOENT, std::system_category()});
  }

  // Test character device
  if (auto result = test_single_device(CHAR_DEVICE_PATH, "Character"); !result) {
    return result;
  }

  // Test block device
  if (auto result = test_single_device(BLOCK_DEVICE_PATH, "Block"); !result) {
    return result;
  }

  return {};
}

const char* WAL_device_manager::char_device_path() const noexcept { return CHAR_DEVICE_PATH; }

const char* WAL_device_manager::block_device_path() const noexcept { return BLOCK_DEVICE_PATH; }

bool WAL_device_manager::devices_created() const noexcept {
  return m_char_device_created && m_block_device_created;
}

std::expected<void, std::error_code> WAL_device_manager::create_single_device(
  const char* device_path, mode_t device_type, dev_t minor_dev) {
  // Check if device already exists
  if (std::filesystem::exists(device_path)) {
    println("Warning: {} already exists, removing it first", device_path);
    if (std::filesystem::remove(device_path)) {
      println("Removed existing {}", device_path);
    } else {
      return std::unexpected(std::error_code{errno, std::system_category()});
    }
  }

  // Create the device node
  dev_t device_id = makedev(DEVICE_MAJOR, minor(minor_dev));

  // Create device node with appropriate permissions (rw-rw-rw-)
  if (mknod(device_path, device_type | 0666, device_id) != 0) {
    return std::unexpected(std::error_code{errno, std::system_category()});
  }

  return {};
}

bool WAL_device_manager::is_device_accessible(const char* device_path) const {
  if (!std::filesystem::exists(device_path)) {
    return false;
  }

  // Try to open the device for reading to test accessibility
  int fd = open(device_path, O_RDONLY | O_NONBLOCK);

  if (fd >= 0) {
    close(fd);
    return true;
  }

  return false;
}

std::expected<void, std::error_code> WAL_device_manager::test_single_device(
  const char* device_path, const char* device_type_name) const {
  struct stat device_stat;

  if (stat(device_path, &device_stat) != 0) {
    return std::unexpected(std::error_code{errno, std::system_category()});
  }

  bool is_expected_type = false;

  if (strcmp(device_type_name, "Character") == 0) {
    is_expected_type = S_ISCHR(device_stat.st_mode);
  } else if (strcmp(device_type_name, "Block") == 0) {
    is_expected_type = S_ISBLK(device_stat.st_mode);
  }

  if (!is_expected_type) {
    return std::unexpected(std::error_code{ENOTTY, std::system_category()});
  }

  dev_t device_id = device_stat.st_rdev;
  unsigned int major_num = major(device_id);
  unsigned int minor_num = minor(device_id);

  println("Device {} verified:", device_path);
  println("  Type: {} device", device_type_name);
  println("  Major: {}, Minor: {}", major_num, minor_num);
  println("  Permissions: {:o}", device_stat.st_mode & 0777);

  if (major_num != DEVICE_MAJOR) {
    println("Warning: Major number {} doesn't match expected {}", major_num, DEVICE_MAJOR);
  }

  return {};
}

void WAL_device_manager::cleanup_single_device(const char* device_path, bool& created_flag) {
  if (created_flag && std::filesystem::exists(device_path)) {
    if (unlink(device_path) == 0) {
      println("Successfully removed device: {}", device_path);
    } else {
      println("Failed to remove device {}: {}", device_path, strerror(errno));
    }
    created_flag = false;
  }
}

WAL_device_interface& WAL_device_interface::operator=(WAL_device_interface&& other) noexcept {
  if (this != &other) {
    close_devices();
    m_char_device_fd = std::exchange(other.m_char_device_fd, -1);
    m_block_device_fd = std::exchange(other.m_block_device_fd, -1);
  }
  return *this;
}

std::expected<void, std::error_code> WAL_device_interface::open_devices() {
  // Open character device (/dev/rwal)
  m_char_device_fd = open(CHAR_DEVICE_PATH, O_RDWR);

  if (m_char_device_fd < 0) {
    return std::unexpected(std::error_code{errno, std::system_category()});
  }

  // Open block device (/dev/wal)
  m_block_device_fd = open(BLOCK_DEVICE_PATH, O_RDWR);
  if (m_block_device_fd < 0) {
    close(m_char_device_fd);
    m_char_device_fd = -1;
    return std::unexpected(std::error_code{errno, std::system_category()});
  }

  println("Successfully opened WAL devices:");
  println("  Character device: {} (fd={})", CHAR_DEVICE_PATH, m_char_device_fd);
  println("  Block device:     {} (fd={})", BLOCK_DEVICE_PATH, m_block_device_fd);

  return {};
}

void WAL_device_interface::close_devices() {
  if (m_char_device_fd >= 0) {
    close(m_char_device_fd);
    println("Closed character device: {}", CHAR_DEVICE_PATH);
    m_char_device_fd = -1;
  }
  if (m_block_device_fd >= 0) {
    close(m_block_device_fd);
    println("Closed block device: {}", BLOCK_DEVICE_PATH);
    m_block_device_fd = -1;
  }
}

std::expected<std::string, std::error_code> WAL_device_interface::read_char_device(
  size_t max_bytes) {
  if (m_char_device_fd < 0) {
    return std::unexpected(std::error_code{EBADF, std::system_category()});
  }

  std::vector<char> buffer(max_bytes);
  ssize_t bytes_read = read(m_char_device_fd, buffer.data(), max_bytes);

  if (bytes_read < 0) {
    return std::unexpected(std::error_code{errno, std::system_category()});
  }

  std::string result(buffer.data(), static_cast<size_t>(bytes_read));
  println("Read from {}: \"{}\" ({} bytes)", CHAR_DEVICE_PATH, result, bytes_read);

  // Simulate returning fixed text since we don't have a real kernel driver
  if (result.empty()) {
    result = WAL_RESPONSE;
    println("Simulated response: \"{}\"", result);
  }

  return result;
}

std::expected<size_t, std::error_code> WAL_device_interface::write_char_device(
  const std::string& data) {
  if (m_char_device_fd < 0) {
    return std::unexpected(std::error_code{EBADF, std::system_category()});
  }

  println("Writing to {}: \"{}\" ({} bytes)", CHAR_DEVICE_PATH, data, data.size());

  // Print captured write data
  println("Captured write data:");
  println("  Raw data: \"{}\"", data);
  print("  Hex dump: ");
  for (size_t i = 0; i < data.size() && i < 64; ++i) {
    print("{:02x} ", static_cast<unsigned char>(data[i]));
  }
  if (data.size() > 64) {
    print("... (truncated)");
  }
  println("");

  ssize_t bytes_written = write(m_char_device_fd, data.c_str(), data.size());

  if (bytes_written < 0) {
    return std::unexpected(std::error_code{errno, std::system_category()});
  }

  println("Successfully wrote {} bytes to character device", bytes_written);
  return static_cast<size_t>(bytes_written);
}

std::expected<std::vector<uint8_t>, std::error_code> WAL_device_interface::read_block_device(
  size_t block_size) {
  if (m_block_device_fd < 0) {
    return std::unexpected(std::error_code{EBADF, std::system_category()});
  }

  std::vector<uint8_t> buffer(block_size);
  ssize_t bytes_read = read(m_block_device_fd, buffer.data(), block_size);

  if (bytes_read < 0) {
    return std::unexpected(std::error_code{errno, std::system_category()});
  }

  buffer.resize(static_cast<size_t>(bytes_read));
  println("Read from {}: {} bytes", BLOCK_DEVICE_PATH, bytes_read);

  // Simulate returning fixed text pattern since we don't have a real kernel driver
  if (buffer.empty() || bytes_read == 0) {
    const std::string response = WAL_RESPONSE;

    buffer.clear();
    buffer.reserve(response.size());

    for (char c : response) {
      buffer.push_back(static_cast<uint8_t>(c));
    }
    println("Simulated block response: \"{}\"", response);
  }

  return buffer;
}

std::expected<size_t, std::error_code> WAL_device_interface::write_block_device(
  const std::vector<uint8_t>& data) {
  if (m_block_device_fd < 0) {
    return std::unexpected(std::error_code{EBADF, std::system_category()});
  }

  println("Writing to {}: {} bytes", BLOCK_DEVICE_PATH, data.size());

  // Print captured write data
  println("Captured block write data:");
  print("  Hex dump: ");
  for (size_t i = 0; i < data.size() && i < 128; ++i) {
    if (i % 16 == 0 && i > 0) {
      println("");
      print("            ");
    }
    print("{:02x} ", data[i]);
  }

  if (data.size() > 128) {
    print("... (truncated)");
  }

  println("");

  // Try to print as text if it contains printable characters
  std::string text_view;
  bool is_printable = true;

  for (uint8_t byte : data) {
    if (std::isprint(byte) || std::isspace(byte)) {
      text_view += static_cast<char>(byte);
    } else {
      is_printable = false;
      break;
    }
  }

  if (is_printable && !text_view.empty()) {
    println("  As text: \"{}\"", text_view);
  }

  ssize_t bytes_written = write(m_block_device_fd, data.data(), data.size());

  if (bytes_written < 0) {
    return std::unexpected(std::error_code{errno, std::system_category()});
  }

  println("Successfully wrote {} bytes to block device", bytes_written);
  return static_cast<size_t>(bytes_written);
}

std::expected<size_t, std::error_code> WAL_device_interface::write_block_device(
  const std::string& data) {
  std::vector<uint8_t> bytes;
  bytes.reserve(data.size());
  for (char c : data) {
    bytes.push_back(static_cast<uint8_t>(c));
  }
  return write_block_device(bytes);
}

std::expected<void, std::error_code> WAL_device_interface::test_device_operations() {
  if (m_char_device_fd < 0 || m_block_device_fd < 0) {
    return std::unexpected(std::error_code{EBADF, std::system_category()});
  }

  println("\n=== Testing WAL Device Operations ===");

  // Test character device
  println("\n--- Character Device Tests ---");

  // Test write to character device
  const std::string test_char_data = "Hello, character device!";
  if (auto result = write_char_device(test_char_data); !result) {
    println("Character device write failed: {}", result.error().message());
    return std::unexpected(result.error());
  }

  // Test read from character device
  if (auto result = read_char_device(); !result) {
    println("Character device read failed: {}", result.error().message());
    return std::unexpected(result.error());
  } else {
    println("Character device read result: \"{}\"", *result);
  }

  // Test block device
  println("\n--- Block Device Tests ---");

  // Test write to block device
  const std::string test_block_data = "Hello, block device! This is a longer message.";

  if (auto result = write_block_device(test_block_data); !result) {
    println("Block device write failed: {}", result.error().message());
    return std::unexpected(result.error());
  }

  // Test read from block device
  if (auto result = read_block_device(); !result) {
    println("Block device read failed: {}", result.error().message());
    return std::unexpected(result.error());
  } else {
    println("Block device read result: {} bytes", result->size());
  }

  println("\n=== Device Operation Tests Complete ===\n");
  return {};
}

bool WAL_device_interface::are_devices_open() const noexcept {
  return m_char_device_fd >= 0 && m_block_device_fd >= 0;
}

int WAL_device_interface::char_device_handle() const noexcept { return m_char_device_fd; }

int WAL_device_interface::block_device_handle() const noexcept { return m_block_device_fd; }
