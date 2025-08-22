#pragma once

#include <expected>
#include <system_error>
#include <vector>
#include <string>
#include <utility>

#include "kvm_db/config.h"
#include "kvm_output.h"

#if HAVE_URINGBLK_DRIVER
#include "uringblk_uapi.h"
#else
// Forward declarations for when driver is not available
struct uringblk_identify;
struct uringblk_limits;
struct uringblk_geometry;
struct uringblk_stats;
#endif

namespace kvm_db {

#if HAVE_URINGBLK_DRIVER

class UringBlkDevice {
public:
    UringBlkDevice() = default;
    ~UringBlkDevice() { close_device(); }

    // Non-copyable, movable
    UringBlkDevice(const UringBlkDevice&) = delete;
    UringBlkDevice& operator=(const UringBlkDevice&) = delete;
    
    UringBlkDevice(UringBlkDevice&& other) noexcept
        : m_device_fd(std::exchange(other.m_device_fd, -1)),
          m_device_path(std::move(other.m_device_path)) {}

    UringBlkDevice& operator=(UringBlkDevice&& other) noexcept {
        if (this != &other) {
            close_device();
            m_device_fd = std::exchange(other.m_device_fd, -1);
            m_device_path = std::move(other.m_device_path);
        }
        return *this;
    }

    // Device management
    [[nodiscard]] std::expected<void, std::error_code> open_device(const std::string& device_path = "/dev/uringblk0");
    void close_device();
    [[nodiscard]] bool is_device_open() const noexcept { return m_device_fd >= 0; }

    // Device identification and capabilities
    [[nodiscard]] std::expected<uringblk_identify, std::error_code> identify() const;
    [[nodiscard]] std::expected<uringblk_limits, std::error_code> get_limits() const;
    [[nodiscard]] std::expected<uint64_t, std::error_code> get_features() const;
    [[nodiscard]] std::expected<void, std::error_code> set_features(uint64_t features) const;
    [[nodiscard]] std::expected<uringblk_geometry, std::error_code> get_geometry() const;
    [[nodiscard]] std::expected<uringblk_stats, std::error_code> get_stats() const;

    // Convenience methods
    [[nodiscard]] std::expected<uint64_t, std::error_code> get_capacity_sectors() const;
    [[nodiscard]] std::expected<uint32_t, std::error_code> get_logical_block_size() const;
    [[nodiscard]] std::expected<bool, std::error_code> supports_feature(uint64_t feature_flag) const;

    // Device path accessor
    [[nodiscard]] const std::string& device_path() const noexcept { return m_device_path; }
    [[nodiscard]] int device_handle() const noexcept { return m_device_fd; }

private:
    [[nodiscard]] std::expected<void, std::error_code> send_uring_cmd(
        uint16_t opcode, void* payload, uint32_t payload_len, void* response, uint32_t response_len) const;

private:
    int m_device_fd{-1};
    std::string m_device_path;
};

class UringBlkManager {
public:
    UringBlkManager() = default;
    ~UringBlkManager() = default;

    // Non-copyable, movable
    UringBlkManager(const UringBlkManager&) = delete;
    UringBlkManager& operator=(const UringBlkManager&) = delete;
    UringBlkManager(UringBlkManager&&) = default;
    UringBlkManager& operator=(UringBlkManager&&) = default;

    // Device enumeration and management
    [[nodiscard]] std::expected<std::vector<std::string>, std::error_code> enumerate_devices() const;
    [[nodiscard]] std::expected<bool, std::error_code> is_device_available(const std::string& device_path) const;
    
    // Driver status
    [[nodiscard]] std::expected<bool, std::error_code> is_driver_loaded() const;
    [[nodiscard]] std::expected<std::string, std::error_code> get_driver_version() const;

    // Test functionality
    [[nodiscard]] std::expected<void, std::error_code> test_device(const std::string& device_path) const;
    [[nodiscard]] std::expected<void, std::error_code> test_all_devices() const;

private:
    [[nodiscard]] bool check_sysfs_path(const std::string& device_name) const;
};

#else // !HAVE_URINGBLK_DRIVER

// Stub implementations when uringblk driver is not available
class UringBlkDevice {
public:
    UringBlkDevice() = default;
    ~UringBlkDevice() = default;
    UringBlkDevice(const UringBlkDevice&) = delete;
    UringBlkDevice& operator=(const UringBlkDevice&) = delete;
    UringBlkDevice(UringBlkDevice&&) = default;
    UringBlkDevice& operator=(UringBlkDevice&&) = default;

    [[nodiscard]] std::expected<void, std::error_code> open_device(const std::string&) {
        return std::unexpected(std::error_code{ENOSYS, std::system_category()});
    }
    void close_device() {}
    [[nodiscard]] bool is_device_open() const noexcept { return false; }
};

class UringBlkManager {
public:
    UringBlkManager() = default;
    ~UringBlkManager() = default;
    UringBlkManager(const UringBlkManager&) = delete;
    UringBlkManager& operator=(const UringBlkManager&) = delete;
    UringBlkManager(UringBlkManager&&) = default;
    UringBlkManager& operator=(UringBlkManager&&) = default;

    [[nodiscard]] std::expected<bool, std::error_code> is_driver_loaded() const {
        return false;  // Driver not available
    }
};

#endif // HAVE_URINGBLK_DRIVER

// Helper functions (always available)
[[nodiscard]] std::string format_uringblk_identify(const uringblk_identify& info);
[[nodiscard]] std::string format_uringblk_limits(const uringblk_limits& limits);
[[nodiscard]] std::string format_uringblk_geometry(const uringblk_geometry& geo);
[[nodiscard]] std::string format_uringblk_stats(const uringblk_stats& stats);
[[nodiscard]] std::string format_features_bitmap(uint64_t features);

} // namespace kvm_db