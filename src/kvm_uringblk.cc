#include "kvm_uringblk.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <liburing.h>

#include <filesystem>

/* Define if not available */
#ifndef IORING_OP_URING_CMD
#define IORING_OP_URING_CMD 26
#endif
#include <fstream>
#include <cstring>
#include <format>

using namespace kvm_db;

#if HAVE_URINGBLK_DRIVER

// UringBlkDevice implementation
std::expected<void, std::error_code> UringBlkDevice::open_device(const std::string& device_path) {
    close_device();  // Close any existing device
    
    m_device_fd = open(device_path.c_str(), O_RDWR);
    if (m_device_fd < 0) {
        return std::unexpected(std::error_code{errno, std::system_category()});
    }
    
    m_device_path = device_path;
    println("Successfully opened uringblk device: {} (fd={})", device_path, m_device_fd);
    return {};
}

void UringBlkDevice::close_device() {
    if (m_device_fd >= 0) {
        close(m_device_fd);
        println("Closed uringblk device: {}", m_device_path);
        m_device_fd = -1;
        m_device_path.clear();
    }
}

std::expected<uringblk_identify, std::error_code> UringBlkDevice::identify() const {
    if (m_device_fd < 0) {
        return std::unexpected(std::error_code{EBADF, std::system_category()});
    }

    uringblk_identify info{};
    auto result = send_uring_cmd(URINGBLK_UCMD_IDENTIFY, nullptr, 0, &info, sizeof(info));
    if (!result) {
        return std::unexpected(result.error());
    }
    
    return info;
}

std::expected<uringblk_limits, std::error_code> UringBlkDevice::get_limits() const {
    if (m_device_fd < 0) {
        return std::unexpected(std::error_code{EBADF, std::system_category()});
    }

    uringblk_limits limits{};
    auto result = send_uring_cmd(URINGBLK_UCMD_GET_LIMITS, nullptr, 0, &limits, sizeof(limits));
    if (!result) {
        return std::unexpected(result.error());
    }
    
    return limits;
}

std::expected<uint64_t, std::error_code> UringBlkDevice::get_features() const {
    if (m_device_fd < 0) {
        return std::unexpected(std::error_code{EBADF, std::system_category()});
    }

    uint64_t features{};
    auto result = send_uring_cmd(URINGBLK_UCMD_GET_FEATURES, nullptr, 0, &features, sizeof(features));
    if (!result) {
        return std::unexpected(result.error());
    }
    
    return features;
}

std::expected<void, std::error_code> UringBlkDevice::set_features(uint64_t features) const {
    if (m_device_fd < 0) {
        return std::unexpected(std::error_code{EBADF, std::system_category()});
    }

    return send_uring_cmd(URINGBLK_UCMD_SET_FEATURES, &features, sizeof(features), nullptr, 0);
}

std::expected<uringblk_geometry, std::error_code> UringBlkDevice::get_geometry() const {
    if (m_device_fd < 0) {
        return std::unexpected(std::error_code{EBADF, std::system_category()});
    }

    uringblk_geometry geo{};
    auto result = send_uring_cmd(URINGBLK_UCMD_GET_GEOMETRY, nullptr, 0, &geo, sizeof(geo));
    if (!result) {
        return std::unexpected(result.error());
    }
    
    return geo;
}

std::expected<uringblk_stats, std::error_code> UringBlkDevice::get_stats() const {
    if (m_device_fd < 0) {
        return std::unexpected(std::error_code{EBADF, std::system_category()});
    }

    uringblk_stats stats{};
    auto result = send_uring_cmd(URINGBLK_UCMD_GET_STATS, nullptr, 0, &stats, sizeof(stats));
    if (!result) {
        return std::unexpected(result.error());
    }
    
    return stats;
}

std::expected<uint64_t, std::error_code> UringBlkDevice::get_capacity_sectors() const {
    auto identify_result = identify();
    if (!identify_result) {
        return std::unexpected(identify_result.error());
    }
    uint64_t capacity = identify_result->capacity_sectors;  // Copy to avoid packed field reference
    return capacity;
}

std::expected<uint32_t, std::error_code> UringBlkDevice::get_logical_block_size() const {
    auto identify_result = identify();
    if (!identify_result) {
        return std::unexpected(identify_result.error());
    }
    uint32_t block_size = identify_result->logical_block_size;  // Copy to avoid packed field reference
    return block_size;
}

std::expected<bool, std::error_code> UringBlkDevice::supports_feature(uint64_t feature_flag) const {
    auto features_result = get_features();
    if (!features_result) {
        return std::unexpected(features_result.error());
    }
    return (*features_result & feature_flag) != 0;
}

std::expected<void, std::error_code> UringBlkDevice::send_uring_cmd(
    uint16_t opcode, void* payload, uint32_t payload_len, void* response, uint32_t response_len) const {
    
    struct io_uring ring;
    struct io_uring_cqe *cqe;
    struct io_uring_sqe *sqe;
    int ret;
    
    // Initialize io_uring
    ret = io_uring_queue_init(1, &ring, 0);
    if (ret < 0) {
        return std::unexpected(std::error_code{-ret, std::system_category()});
    }
    
    // Create URING_CMD buffer
    struct {
        uringblk_ucmd_hdr header;
        uint8_t data[4096];  // Buffer for payload and response
    } cmd_buffer;
    
    // Prepare header
    cmd_buffer.header.abi_major = URINGBLK_ABI_MAJOR;
    cmd_buffer.header.abi_minor = URINGBLK_ABI_MINOR;
    cmd_buffer.header.opcode = opcode;
    cmd_buffer.header.flags = 0;
    cmd_buffer.header.payload_len = payload_len;
    
    // Copy payload if provided
    if (payload && payload_len > 0) {
        if (payload_len > sizeof(cmd_buffer.data)) {
            io_uring_queue_exit(&ring);
            return std::unexpected(std::error_code{E2BIG, std::system_category()});
        }
        std::memcpy(cmd_buffer.data, payload, payload_len);
    }
    
    // Get SQE and prepare URING_CMD
    sqe = io_uring_get_sqe(&ring);
    if (!sqe) {
        io_uring_queue_exit(&ring);
        return std::unexpected(std::error_code{ENOMEM, std::system_category()});
    }
    
    /* Manually prepare URING_CMD since io_uring_prep_cmd isn't available */
    io_uring_prep_rw(IORING_OP_URING_CMD, sqe, m_device_fd, &cmd_buffer, sizeof(cmd_buffer), 0);
    
    // Submit the command
    ret = io_uring_submit(&ring);
    if (ret < 0) {
        io_uring_queue_exit(&ring);
        return std::unexpected(std::error_code{-ret, std::system_category()});
    }
    
    // Wait for completion
    ret = io_uring_wait_cqe(&ring, &cqe);
    if (ret < 0) {
        io_uring_queue_exit(&ring);
        return std::unexpected(std::error_code{-ret, std::system_category()});
    }
    
    // Check result
    int cmd_result = cqe->res;
    io_uring_cqe_seen(&ring, cqe);
    io_uring_queue_exit(&ring);
    
    if (cmd_result < 0) {
        return std::unexpected(std::error_code{-cmd_result, std::system_category()});
    }
    
    // Copy response if requested
    if (response && response_len > 0) {
        if (response_len > sizeof(cmd_buffer.data)) {
            return std::unexpected(std::error_code{E2BIG, std::system_category()});
        }
        std::memcpy(response, cmd_buffer.data, std::min(response_len, (uint32_t)cmd_result));
    }
    
    return {};
}

std::expected<size_t, std::error_code> UringBlkDevice::read_async(uint64_t offset, void* buffer, size_t length) const {
    if (m_device_fd < 0) {
        return std::unexpected(std::error_code{EBADF, std::system_category()});
    }

    struct io_uring ring;
    struct io_uring_cqe *cqe;
    struct io_uring_sqe *sqe;
    int ret;
    
    // Initialize io_uring with polling support
    ret = io_uring_queue_init(1, &ring, IORING_SETUP_IOPOLL);
    if (ret < 0) {
        // Fallback to regular ring if polling not supported
        ret = io_uring_queue_init(1, &ring, 0);
        if (ret < 0) {
            return std::unexpected(std::error_code{-ret, std::system_category()});
        }
    }
    
    // Get SQE and prepare read operation
    sqe = io_uring_get_sqe(&ring);
    if (!sqe) {
        io_uring_queue_exit(&ring);
        return std::unexpected(std::error_code{ENOMEM, std::system_category()});
    }
    
    io_uring_prep_read(sqe, m_device_fd, buffer, static_cast<unsigned int>(length), offset);
    sqe->flags |= IOSQE_ASYNC;  // Force async execution
    
    // Submit the operation
    ret = io_uring_submit(&ring);
    if (ret < 0) {
        io_uring_queue_exit(&ring);
        return std::unexpected(std::error_code{-ret, std::system_category()});
    }
    
    // Wait for completion
    ret = io_uring_wait_cqe(&ring, &cqe);
    if (ret < 0) {
        io_uring_queue_exit(&ring);
        return std::unexpected(std::error_code{-ret, std::system_category()});
    }
    
    // Get result
    int bytes_read = cqe->res;
    io_uring_cqe_seen(&ring, cqe);
    io_uring_queue_exit(&ring);
    
    if (bytes_read < 0) {
        return std::unexpected(std::error_code{-bytes_read, std::system_category()});
    }
    
    return static_cast<size_t>(bytes_read);
}

std::expected<size_t, std::error_code> UringBlkDevice::write_async(uint64_t offset, const void* buffer, size_t length) const {
    if (m_device_fd < 0) {
        return std::unexpected(std::error_code{EBADF, std::system_category()});
    }

    struct io_uring ring;
    struct io_uring_cqe *cqe;
    struct io_uring_sqe *sqe;
    int ret;
    
    // Initialize io_uring with polling support
    ret = io_uring_queue_init(1, &ring, IORING_SETUP_IOPOLL);
    if (ret < 0) {
        // Fallback to regular ring if polling not supported
        ret = io_uring_queue_init(1, &ring, 0);
        if (ret < 0) {
            return std::unexpected(std::error_code{-ret, std::system_category()});
        }
    }
    
    // Get SQE and prepare write operation
    sqe = io_uring_get_sqe(&ring);
    if (!sqe) {
        io_uring_queue_exit(&ring);
        return std::unexpected(std::error_code{ENOMEM, std::system_category()});
    }
    
    io_uring_prep_write(sqe, m_device_fd, buffer, static_cast<unsigned int>(length), offset);
    sqe->flags |= IOSQE_ASYNC;  // Force async execution
    
    // Submit the operation
    ret = io_uring_submit(&ring);
    if (ret < 0) {
        io_uring_queue_exit(&ring);
        return std::unexpected(std::error_code{-ret, std::system_category()});
    }
    
    // Wait for completion
    ret = io_uring_wait_cqe(&ring, &cqe);
    if (ret < 0) {
        io_uring_queue_exit(&ring);
        return std::unexpected(std::error_code{-ret, std::system_category()});
    }
    
    // Get result
    int bytes_written = cqe->res;
    io_uring_cqe_seen(&ring, cqe);
    io_uring_queue_exit(&ring);
    
    if (bytes_written < 0) {
        return std::unexpected(std::error_code{-bytes_written, std::system_category()});
    }
    
    return static_cast<size_t>(bytes_written);
}

std::expected<void, std::error_code> UringBlkDevice::flush_async() const {
    if (m_device_fd < 0) {
        return std::unexpected(std::error_code{EBADF, std::system_category()});
    }

    struct io_uring ring;
    struct io_uring_cqe *cqe;
    struct io_uring_sqe *sqe;
    int ret;
    
    // Initialize io_uring
    ret = io_uring_queue_init(1, &ring, 0);
    if (ret < 0) {
        return std::unexpected(std::error_code{-ret, std::system_category()});
    }
    
    // Get SQE and prepare fsync operation
    sqe = io_uring_get_sqe(&ring);
    if (!sqe) {
        io_uring_queue_exit(&ring);
        return std::unexpected(std::error_code{ENOMEM, std::system_category()});
    }
    
    io_uring_prep_fsync(sqe, m_device_fd, IORING_FSYNC_DATASYNC);
    
    // Submit the operation
    ret = io_uring_submit(&ring);
    if (ret < 0) {
        io_uring_queue_exit(&ring);
        return std::unexpected(std::error_code{-ret, std::system_category()});
    }
    
    // Wait for completion
    ret = io_uring_wait_cqe(&ring, &cqe);
    if (ret < 0) {
        io_uring_queue_exit(&ring);
        return std::unexpected(std::error_code{-ret, std::system_category()});
    }
    
    // Get result
    int flush_result = cqe->res;
    io_uring_cqe_seen(&ring, cqe);
    io_uring_queue_exit(&ring);
    
    if (flush_result < 0) {
        return std::unexpected(std::error_code{-flush_result, std::system_category()});
    }
    
    return {};
}

// UringBlkManager implementation
std::expected<std::vector<std::string>, std::error_code> UringBlkManager::enumerate_devices() const {
    std::vector<std::string> devices;
    
    // Check for standard uringblk devices
    for (int i = 0; i < 16; ++i) {
        std::string device_path = std::format("/dev/uringblk{}", i);
        if (std::filesystem::exists(device_path)) {
            devices.push_back(device_path);
        }
    }
    
    return devices;
}

std::expected<bool, std::error_code> UringBlkManager::is_device_available(const std::string& device_path) const {
    if (!std::filesystem::exists(device_path)) {
        return false;
    }
    
    // Try to open the device to verify it's functional
    int fd = open(device_path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd >= 0) {
        close(fd);
        return true;
    }
    
    return false;
}

std::expected<bool, std::error_code> UringBlkManager::is_driver_loaded() const {
    // Check if the driver module is loaded by looking for proc entries
    return std::filesystem::exists("/proc/modules") && 
           std::filesystem::exists("/sys/module/uringblk_driver");
}

std::expected<std::string, std::error_code> UringBlkManager::get_driver_version() const {
    try {
        std::string version_path = "/sys/module/uringblk_driver/version";
        if (!std::filesystem::exists(version_path)) {
            return std::unexpected(std::error_code{ENOENT, std::system_category()});
        }
        
        std::ifstream version_file(version_path);
        if (!version_file) {
            return std::unexpected(std::error_code{errno, std::system_category()});
        }
        
        std::string version;
        std::getline(version_file, version);
        return version;
    } catch (const std::exception&) {
        return std::unexpected(std::error_code{EIO, std::system_category()});
    }
}

std::expected<void, std::error_code> UringBlkManager::test_device(const std::string& device_path) const {
    UringBlkDevice device;
    
    // Try to open the device
    auto open_result = device.open_device(device_path);
    if (!open_result) {
        return open_result;
    }
    
    println("\n=== Testing uringblk device: {} ===", device_path);
    
    // Test identify command
    if (auto identify_result = device.identify(); identify_result) {
        println("Device Identification:");
        println("{}", format_uringblk_identify(*identify_result));
    } else {
        println("Failed to get device identification: {}", identify_result.error().message());
        return std::unexpected(identify_result.error());
    }
    
    // Test limits command
    if (auto limits_result = device.get_limits(); limits_result) {
        println("Device Limits:");
        println("{}", format_uringblk_limits(*limits_result));
    } else {
        println("Failed to get device limits: {}", limits_result.error().message());
    }
    
    // Test geometry command
    if (auto geometry_result = device.get_geometry(); geometry_result) {
        println("Device Geometry:");
        println("{}", format_uringblk_geometry(*geometry_result));
    } else {
        println("Failed to get device geometry: {}", geometry_result.error().message());
    }
    
    // Test features command
    if (auto features_result = device.get_features(); features_result) {
        println("Device Features:");
        println("{}", format_features_bitmap(*features_result));
    } else {
        println("Failed to get device features: {}", features_result.error().message());
    }
    
    // Test statistics command
    if (auto stats_result = device.get_stats(); stats_result) {
        println("Device Statistics:");
        println("{}", format_uringblk_stats(*stats_result));
    } else {
        println("Failed to get device statistics: {}", stats_result.error().message());
    }
    
    println("=== Device test completed ===\n");
    return {};
}

std::expected<void, std::error_code> UringBlkManager::test_all_devices() const {
    auto devices_result = enumerate_devices();
    if (!devices_result) {
        return std::unexpected(devices_result.error());
    }
    
    if (devices_result->empty()) {
        println("No uringblk devices found");
        return {};
    }
    
    println("Found {} uringblk device(s)", devices_result->size());
    
    for (const auto& device_path : *devices_result) {
        auto test_result = test_device(device_path);
        if (!test_result) {
            println("Test failed for device {}: {}", device_path, test_result.error().message());
            // Continue testing other devices
        }
    }
    
    return {};
}

bool UringBlkManager::check_sysfs_path(const std::string& device_name) const {
    std::string sysfs_path = std::format("/sys/block/{}/uringblk", device_name);
    return std::filesystem::exists(sysfs_path);
}

// Helper functions
std::string kvm_db::format_uringblk_identify(const uringblk_identify& info) {
    return std::format(
        "  Model: {:.40s}\n"
        "  Firmware: {:.16s}\n"
        "  Logical Block Size: {} bytes\n"
        "  Physical Block Size: {} bytes\n"
        "  Capacity: {} sectors ({:.2f} GB)\n"
        "  Features: 0x{:016x}\n"
        "  Queue Count: {}\n"
        "  Queue Depth: {}\n"
        "  Max Segments: {}\n"
        "  Max Segment Size: {} bytes\n"
        "  DMA Alignment: {}\n"
        "  IO Min: {}\n"
        "  IO Opt: {}\n"
        "  Discard Granularity: {}\n"
        "  Discard Max: {} bytes",
        reinterpret_cast<const char*>(info.model),
        reinterpret_cast<const char*>(info.firmware),
        info.logical_block_size,
        info.physical_block_size,
        info.capacity_sectors,
        static_cast<double>(info.capacity_sectors * info.logical_block_size) / (1024.0 * 1024.0 * 1024.0),
        info.features_bitmap,
        info.queue_count,
        info.queue_depth,
        info.max_segments,
        info.max_segment_size,
        info.dma_alignment,
        info.io_min,
        info.io_opt,
        info.discard_granularity,
        info.discard_max_bytes
    );
}

std::string kvm_db::format_uringblk_limits(const uringblk_limits& limits) {
    return std::format(
        "  Max HW Sectors: {} KB\n"
        "  Max Sectors: {} KB\n"
        "  HW Queues: {}\n"
        "  Queue Depth: {}\n"
        "  Max Segments: {}\n"
        "  Max Segment Size: {} bytes\n"
        "  DMA Alignment: {}\n"
        "  IO Min: {}\n"
        "  IO Opt: {}\n"
        "  Discard Granularity: {}\n"
        "  Discard Max: {} bytes",
        limits.max_hw_sectors_kb,
        limits.max_sectors_kb,
        limits.nr_hw_queues,
        limits.queue_depth,
        limits.max_segments,
        limits.max_segment_size,
        limits.dma_alignment,
        limits.io_min,
        limits.io_opt,
        limits.discard_granularity,
        limits.discard_max_bytes
    );
}

std::string kvm_db::format_uringblk_geometry(const uringblk_geometry& geo) {
    return std::format(
        "  Capacity: {} sectors ({:.2f} GB)\n"
        "  Logical Block Size: {} bytes\n"
        "  Physical Block Size: {} bytes\n"
        "  Cylinders: {}\n"
        "  Heads: {}\n"
        "  Sectors per Track: {}",
        geo.capacity_sectors,
        static_cast<double>(geo.capacity_sectors * geo.logical_block_size) / (1024.0 * 1024.0 * 1024.0),
        geo.logical_block_size,
        geo.physical_block_size,
        geo.cylinders,
        geo.heads,
        geo.sectors_per_track
    );
}

std::string kvm_db::format_uringblk_stats(const uringblk_stats& stats) {
    return std::format(
        "  Operations:\n"
        "    Read Ops: {}\n"
        "    Write Ops: {}\n"
        "    Flush Ops: {}\n"
        "    Discard Ops: {}\n"
        "  Data Transfer:\n"
        "    Read Sectors: {} ({:.2f} MB)\n"
        "    Write Sectors: {} ({:.2f} MB)\n"
        "    Read Bytes: {} ({:.2f} MB)\n"
        "    Write Bytes: {} ({:.2f} MB)\n"
        "  Performance:\n"
        "    Queue Full Events: {}\n"
        "    Media Errors: {}\n"
        "    Retries: {}\n"
        "    P50 Read Latency: {} μs\n"
        "    P99 Read Latency: {} μs\n"
        "    P50 Write Latency: {} μs\n"
        "    P99 Write Latency: {} μs",
        stats.read_ops,
        stats.write_ops,
        stats.flush_ops,
        stats.discard_ops,
        stats.read_sectors,
        static_cast<double>(stats.read_sectors) * 512.0 / (1024.0 * 1024.0),
        stats.write_sectors,
        static_cast<double>(stats.write_sectors) * 512.0 / (1024.0 * 1024.0),
        stats.read_bytes,
        static_cast<double>(stats.read_bytes) / (1024.0 * 1024.0),
        stats.write_bytes,
        static_cast<double>(stats.write_bytes) / (1024.0 * 1024.0),
        stats.queue_full_events,
        stats.media_errors,
        stats.retries,
        stats.p50_read_latency_us,
        stats.p99_read_latency_us,
        stats.p50_write_latency_us,
        stats.p99_write_latency_us
    );
}

std::string kvm_db::format_features_bitmap(uint64_t features) {
    std::string result = std::format("0x{:016x} (", features);
    std::vector<std::string> feature_names;
    
    if (features & URINGBLK_FEAT_WRITE_CACHE) feature_names.push_back("WRITE_CACHE");
    if (features & URINGBLK_FEAT_FUA) feature_names.push_back("FUA");
    if (features & URINGBLK_FEAT_FLUSH) feature_names.push_back("FLUSH");
    if (features & URINGBLK_FEAT_DISCARD) feature_names.push_back("DISCARD");
    if (features & URINGBLK_FEAT_WRITE_ZEROES) feature_names.push_back("WRITE_ZEROES");
    if (features & URINGBLK_FEAT_ZONED) feature_names.push_back("ZONED");
    if (features & URINGBLK_FEAT_POLLING) feature_names.push_back("POLLING");
    
    if (feature_names.empty()) {
        result += "none)";
    } else {
        for (size_t i = 0; i < feature_names.size(); ++i) {
            if (i > 0) result += ", ";
            result += feature_names[i];
        }
        result += ")";
    }
    
    return result;
}

#else // !HAVE_URINGBLK_DRIVER

// Stub implementations for helper functions when driver is not available
std::string kvm_db::format_uringblk_identify(const uringblk_identify&) {
    return "uringblk driver not available";
}

std::string kvm_db::format_uringblk_limits(const uringblk_limits&) {
    return "uringblk driver not available";
}

std::string kvm_db::format_uringblk_geometry(const uringblk_geometry&) {
    return "uringblk driver not available";
}

std::string kvm_db::format_uringblk_stats(const uringblk_stats&) {
    return "uringblk driver not available";
}

std::string kvm_db::format_features_bitmap(uint64_t) {
    return "uringblk driver not available";
}

#endif // HAVE_URINGBLK_DRIVER