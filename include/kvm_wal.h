#pragma once

#include <expected>
#include <system_error>

#include "kvm_db/config.h"
#include "kvm_output.h"

static constexpr const char* CHAR_DEVICE_PATH = "/dev/rwal";
static constexpr const char* BLOCK_DEVICE_PATH = "/dev/wal";

struct WAL_device_manager {
  WAL_device_manager() = default;

  ~WAL_device_manager() { cleanup_devices(); }

  // Non-copyable, movable
  WAL_device_manager(const WAL_device_manager&) = delete;
  WAL_device_manager& operator=(const WAL_device_manager&) = delete;
  WAL_device_manager& operator=(WAL_device_manager&& other) noexcept;

  [[nodiscard]] std::expected<void, std::error_code> create_devices();

  void cleanup_devices();

  [[nodiscard]] bool are_devices_accessible() const;

  [[nodiscard]] std::expected<void, std::error_code> test_devices() const;

  [[nodiscard]] const char* char_device_path() const noexcept;

  [[nodiscard]] const char* block_device_path() const noexcept;

  [[nodiscard]] bool devices_created() const noexcept;

private:
  [[nodiscard]] std::expected<void, std::error_code> create_single_device(
    const char* device_path, mode_t device_type, dev_t minor_dev);

  [[nodiscard]] bool is_device_accessible(const char* device_path) const;

  [[nodiscard]] std::expected<void, std::error_code> test_single_device(
    const char* device_path, const char* device_type_name) const;

  void cleanup_single_device(const char* device_path, bool& created_flag);

private:
  /** Use a high number to avoid conflicts. */
  static constexpr dev_t DEVICE_MAJOR = 240;
  static constexpr dev_t CHAR_DEVICE_MINOR = 0;
  static constexpr dev_t BLOCK_DEVICE_MINOR = 1;

  bool m_char_device_created{false};
  bool m_block_device_created{false};
};

struct WAL_device_interface {
  WAL_device_interface() = default;

  ~WAL_device_interface() { close_devices(); }

  // Non-copyable, movable
  WAL_device_interface(const WAL_device_interface&) = delete;
  WAL_device_interface& operator=(const WAL_device_interface&) = delete;

  WAL_device_interface(WAL_device_interface&& other) noexcept
    : m_char_device_fd(std::exchange(other.m_char_device_fd, -1)),
      m_block_device_fd(std::exchange(other.m_block_device_fd, -1)) {}

  WAL_device_interface& operator=(WAL_device_interface&& other) noexcept;

  [[nodiscard]] std::expected<void, std::error_code> open_devices();

  void close_devices();

  // Character device operations
  [[nodiscard]] std::expected<std::string, std::error_code> read_char_device(
    size_t max_bytes = 1024);

  [[nodiscard]] std::expected<size_t, std::error_code> write_char_device(const std::string& data);

  // Block device operations
  [[nodiscard]] std::expected<std::vector<uint8_t>, std::error_code> read_block_device(
    size_t block_size = 512);

  [[nodiscard]] std::expected<size_t, std::error_code> write_block_device(
    const std::vector<uint8_t>& data);

  // Convenience method for block device with string data
  [[nodiscard]] std::expected<size_t, std::error_code> write_block_device(const std::string& data);

  // Test both devices
  [[nodiscard]] std::expected<void, std::error_code> test_device_operations();

  [[nodiscard]] bool are_devices_open() const noexcept;

  [[nodiscard]] int char_device_handle() const noexcept;

  [[nodiscard]] int block_device_handle() const noexcept;

private:
  int m_char_device_fd{-1};
  int m_block_device_fd{-1};
  static constexpr const char* WAL_RESPONSE = "Hello from WAL\n";
};
