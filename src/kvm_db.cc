#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/sysmacros.h>  // For makedev, major, minor
#include <unistd.h>
#include <linux/kvm.h>
#include <memory>
#include <vector>
#include <string_view>
#include <system_error>
#include <filesystem>
#include <cstring>  // For strerror, strcmp
#include <utility>

#include "kvm_db/config.h"

using namespace kvm_db;

struct WALDeviceManager {
    WALDeviceManager() = default;
    ~WALDeviceManager() { 
        cleanup_devices(); 
    }

    // Non-copyable, movable
    WALDeviceManager(const WALDeviceManager&) = delete;
    WALDeviceManager& operator=(const WALDeviceManager&) = delete;
    WALDeviceManager(WALDeviceManager&& other) noexcept 
        : char_device_created_(std::exchange(other.char_device_created_, false))
        , block_device_created_(std::exchange(other.block_device_created_, false)) {}
    WALDeviceManager& operator=(WALDeviceManager&& other) noexcept {
        if (this != &other) {
            cleanup_devices();
            char_device_created_ = std::exchange(other.char_device_created_, false);
            block_device_created_ = std::exchange(other.block_device_created_, false);
        }
        return *this;
    }

    [[nodiscard]] expected<void, std::error_code> create_devices() {
        // Create character device (/dev/rwal)
        if (auto result = create_single_device(CHAR_DEVICE_PATH, S_IFCHR, CHAR_DEVICE_MINOR); !result) {
            return result;
        }
        char_device_created_ = true;

        // Create block device (/dev/wal)
        if (auto result = create_single_device(BLOCK_DEVICE_PATH, S_IFBLK, BLOCK_DEVICE_MINOR); !result) {
            // Cleanup char device if block device creation fails
            cleanup_single_device(CHAR_DEVICE_PATH, char_device_created_);
            return result;
        }
        block_device_created_ = true;

        println("Successfully created WAL devices:");
        println("  Character device: {} (major={}, minor={})", CHAR_DEVICE_PATH, DEVICE_MAJOR, CHAR_DEVICE_MINOR);
        println("  Block device:     {} (major={}, minor={})", BLOCK_DEVICE_PATH, DEVICE_MAJOR, BLOCK_DEVICE_MINOR);

        return {};
    }

    void cleanup_devices() {
        cleanup_single_device(CHAR_DEVICE_PATH, char_device_created_);
        cleanup_single_device(BLOCK_DEVICE_PATH, block_device_created_);
    }

    [[nodiscard]] bool are_devices_accessible() const {
        return is_device_accessible(CHAR_DEVICE_PATH) && is_device_accessible(BLOCK_DEVICE_PATH);
    }

    [[nodiscard]] expected<void, std::error_code> test_devices() const {
        if (!char_device_created_ || !block_device_created_) {
            return unexpected(std::error_code{ENOENT, std::system_category()});
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

    [[nodiscard]] const char* char_device_path() const noexcept {
        return CHAR_DEVICE_PATH;
    }

    [[nodiscard]] const char* block_device_path() const noexcept {
        return BLOCK_DEVICE_PATH;
    }

    [[nodiscard]] bool devices_created() const noexcept {
        return char_device_created_ && block_device_created_;
    }

private:
    [[nodiscard]] expected<void, std::error_code> create_single_device(const char* device_path, mode_t device_type, dev_t minor_dev) {
        // Check if device already exists
        if (std::filesystem::exists(device_path)) {
            println("Warning: {} already exists, removing it first", device_path);
            if (std::filesystem::remove(device_path)) {
                println("Removed existing {}", device_path);
            } else {
                return unexpected(std::error_code{errno, std::system_category()});
            }
        }

        // Create the device node
        dev_t device_id = makedev(DEVICE_MAJOR, minor(minor_dev));

        // Create device node with appropriate permissions (rw-rw-rw-)
        if (mknod(device_path, device_type | 0666, device_id) != 0) {
            return unexpected(std::error_code{errno, std::system_category()});
        }

        return {};
    }

    [[nodiscard]] bool is_device_accessible(const char* device_path) const {
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

    [[nodiscard]] expected<void, std::error_code> test_single_device(const char* device_path, const char* device_type_name) const {
        struct stat device_stat;
        if (stat(device_path, &device_stat) != 0) {
            return unexpected(std::error_code{errno, std::system_category()});
        }

        bool is_expected_type = false;
        if (strcmp(device_type_name, "Character") == 0) {
            is_expected_type = S_ISCHR(device_stat.st_mode);
        } else if (strcmp(device_type_name, "Block") == 0) {
            is_expected_type = S_ISBLK(device_stat.st_mode);
        }

        if (!is_expected_type) {
            return unexpected(std::error_code{ENOTTY, std::system_category()});
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

    void cleanup_single_device(const char* device_path, bool& created_flag) {
        if (created_flag && std::filesystem::exists(device_path)) {
            if (unlink(device_path) == 0) {
                println("Successfully removed device: {}", device_path);
            } else {
                println("Failed to remove device {}: {}", device_path, strerror(errno));
            }
            created_flag = false;
        }
    }

public:
    static constexpr const char* CHAR_DEVICE_PATH = "/dev/rwal";
    static constexpr const char* BLOCK_DEVICE_PATH = "/dev/wal";
    static constexpr dev_t DEVICE_MAJOR = 240;  // Use a high number to avoid conflicts
    static constexpr dev_t CHAR_DEVICE_MINOR = 0;
    static constexpr dev_t BLOCK_DEVICE_MINOR = 1;
    bool char_device_created_{false};
    bool block_device_created_{false};
};

struct KVMProbe {
    KVMProbe() = default;
    ~KVMProbe() { 
        if (kvm_fd >= 0) close(kvm_fd); 
    }

    // Non-copyable, movable
    KVMProbe(const KVMProbe&) = delete;
    KVMProbe& operator=(const KVMProbe&) = delete;
    KVMProbe(KVMProbe&& other) noexcept : kvm_fd(std::exchange(other.kvm_fd, -1)) {}
    KVMProbe& operator=(KVMProbe&& other) noexcept {
        if (this != &other) {
            if (kvm_fd >= 0) close(kvm_fd);
            kvm_fd = std::exchange(other.kvm_fd, -1);
        }
        return *this;
    }

    [[nodiscard]] expected<void, std::error_code> initialize() {
        kvm_fd = open("/dev/kvm", O_RDWR | O_CLOEXEC);
        if (kvm_fd < 0) {
            return unexpected(std::error_code{errno, std::system_category()});
        }
        return {};
    }

    [[nodiscard]] expected<int, std::error_code> get_api_version() const {
        int version = ioctl(kvm_fd, KVM_GET_API_VERSION, 0);
        if (version < 0) {
            return unexpected(std::error_code{errno, std::system_category()});
        }
        return version;
    }

    [[nodiscard]] bool check_extension(int extension) const {
        return ioctl(kvm_fd, KVM_CHECK_EXTENSION, extension) > 0;
    }

    [[nodiscard]] expected<size_t, std::error_code> get_vcpu_mmap_size() const {
        int size = ioctl(kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
        if (size < 0) {
            return unexpected(std::error_code{errno, std::system_category()});
        }
        return static_cast<size_t>(size);
    }

    void print_capabilities() const {
        println("=== KVM Capabilities ===\n");

        // API Version
        if (auto version = get_api_version()) {
            println("API Version: {}", *version);
        } else {
            println("Failed to get API version: {}", version.error().message());
            return;
        }

        // VCPU mmap size
        if (auto mmap_size = get_vcpu_mmap_size()) {
            println("VCPU mmap size: {} bytes\n", *mmap_size);
        }

        // Common extensions
        struct Extension {
            int id;
            std::string_view name;
            std::string_view description;
        };

        std::vector<Extension> extensions = {
            {KVM_CAP_IRQCHIP, "KVM_CAP_IRQCHIP", "In-kernel interrupt controller"},
            {KVM_CAP_HLT, "KVM_CAP_HLT", "HLT instruction support"},
            {KVM_CAP_MMU_SHADOW_CACHE_CONTROL, "KVM_CAP_MMU_SHADOW_CACHE_CONTROL", "MMU shadow cache control"},
            {KVM_CAP_USER_MEMORY, "KVM_CAP_USER_MEMORY", "User memory regions"},
            {KVM_CAP_SET_TSS_ADDR, "KVM_CAP_SET_TSS_ADDR", "TSS address setting"},
            {KVM_CAP_VAPIC, "KVM_CAP_VAPIC", "Virtual APIC"},
            {KVM_CAP_EXT_CPUID, "KVM_CAP_EXT_CPUID", "Extended CPUID"},
            {KVM_CAP_CLOCKSOURCE, "KVM_CAP_CLOCKSOURCE", "Clock source"},
            {KVM_CAP_NR_VCPUS, "KVM_CAP_NR_VCPUS", "Maximum VCPUs"},
            {KVM_CAP_NR_MEMSLOTS, "KVM_CAP_NR_MEMSLOTS", "Maximum memory slots"},
            {KVM_CAP_PIT, "KVM_CAP_PIT", "Programmable Interval Timer"},
            {KVM_CAP_NOP_IO_DELAY, "KVM_CAP_NOP_IO_DELAY", "NOP IO delay"},
            {KVM_CAP_PV_MMU, "KVM_CAP_PV_MMU", "Paravirtual MMU"},
            {KVM_CAP_MP_STATE, "KVM_CAP_MP_STATE", "MP state control"},
            {KVM_CAP_COALESCED_MMIO, "KVM_CAP_COALESCED_MMIO", "Coalesced MMIO"},
            {KVM_CAP_SYNC_MMU, "KVM_CAP_SYNC_MMU", "Synchronous MMU"},
            {KVM_CAP_IOMMU, "KVM_CAP_IOMMU", "IOMMU support"},
            {KVM_CAP_DESTROY_MEMORY_REGION_WORKS, "KVM_CAP_DESTROY_MEMORY_REGION_WORKS", "Memory region destruction"},
            {KVM_CAP_USER_NMI, "KVM_CAP_USER_NMI", "User NMI injection"},
            {KVM_CAP_SET_GUEST_DEBUG, "KVM_CAP_SET_GUEST_DEBUG", "Guest debugging"},
            {KVM_CAP_REINJECT_CONTROL, "KVM_CAP_REINJECT_CONTROL", "Interrupt reinjection control"},
            {KVM_CAP_IRQ_ROUTING, "KVM_CAP_IRQ_ROUTING", "IRQ routing"},
            {KVM_CAP_IRQ_INJECT_STATUS, "KVM_CAP_IRQ_INJECT_STATUS", "IRQ injection status"},
            {KVM_CAP_ASSIGN_DEV_IRQ, "KVM_CAP_ASSIGN_DEV_IRQ", "Device IRQ assignment"},
            {KVM_CAP_JOIN_MEMORY_REGIONS_WORKS, "KVM_CAP_JOIN_MEMORY_REGIONS_WORKS", "Memory region joining"},
            {KVM_CAP_MCE, "KVM_CAP_MCE", "Machine Check Exception"},
            {KVM_CAP_IRQFD, "KVM_CAP_IRQFD", "IRQ file descriptor"},
            {KVM_CAP_PIT2, "KVM_CAP_PIT2", "PIT2 support"},
            {KVM_CAP_SET_BOOT_CPU_ID, "KVM_CAP_SET_BOOT_CPU_ID", "Boot CPU ID setting"},
            {KVM_CAP_PIT_STATE2, "KVM_CAP_PIT_STATE2", "PIT state 2"},
            {KVM_CAP_IOEVENTFD, "KVM_CAP_IOEVENTFD", "IO event file descriptor"},
            {KVM_CAP_SET_IDENTITY_MAP_ADDR, "KVM_CAP_SET_IDENTITY_MAP_ADDR", "Identity map address"},
            {KVM_CAP_XEN_HVM, "KVM_CAP_XEN_HVM", "Xen HVM support"},
            {KVM_CAP_ADJUST_CLOCK, "KVM_CAP_ADJUST_CLOCK", "Clock adjustment"},
            {KVM_CAP_INTERNAL_ERROR_DATA, "KVM_CAP_INTERNAL_ERROR_DATA", "Internal error data"},
            {KVM_CAP_VCPU_EVENTS, "KVM_CAP_VCPU_EVENTS", "VCPU events"},
            {KVM_CAP_S390_PSW, "KVM_CAP_S390_PSW", "S390 PSW"},
            {KVM_CAP_PPC_SEGSTATE, "KVM_CAP_PPC_SEGSTATE", "PowerPC segment state"},
            {KVM_CAP_HYPERV, "KVM_CAP_HYPERV", "Hyper-V support"},
            {KVM_CAP_HYPERV_VAPIC, "KVM_CAP_HYPERV_VAPIC", "Hyper-V VAPIC"},
            {KVM_CAP_HYPERV_SPIN, "KVM_CAP_HYPERV_SPIN", "Hyper-V spinlocks"},
            {KVM_CAP_PCI_SEGMENT, "KVM_CAP_PCI_SEGMENT", "PCI segments"},
            {KVM_CAP_PPC_PAIRED_SINGLES, "KVM_CAP_PPC_PAIRED_SINGLES", "PowerPC paired singles"},
            {KVM_CAP_INTR_SHADOW, "KVM_CAP_INTR_SHADOW", "Interrupt shadow"},
            {KVM_CAP_DEBUGREGS, "KVM_CAP_DEBUGREGS", "Debug registers"},
            {KVM_CAP_X86_ROBUST_SINGLESTEP, "KVM_CAP_X86_ROBUST_SINGLESTEP", "Robust single-step"},
            {KVM_CAP_PPC_OSI, "KVM_CAP_PPC_OSI", "PowerPC OSI"},
            {KVM_CAP_PPC_UNSET_IRQ, "KVM_CAP_PPC_UNSET_IRQ", "PowerPC IRQ unsetting"},
            {KVM_CAP_ENABLE_CAP, "KVM_CAP_ENABLE_CAP", "Enable capability"},
            {KVM_CAP_XSAVE, "KVM_CAP_XSAVE", "XSAVE support"},
            {KVM_CAP_XCRS, "KVM_CAP_XCRS", "Extended control registers"},
            {KVM_CAP_PPC_GET_PVINFO, "KVM_CAP_PPC_GET_PVINFO", "PowerPC PV info"},
            {KVM_CAP_PPC_IRQ_LEVEL, "KVM_CAP_PPC_IRQ_LEVEL", "PowerPC IRQ level"},
            {KVM_CAP_ASYNC_PF, "KVM_CAP_ASYNC_PF", "Async page fault"},
            {KVM_CAP_TSC_CONTROL, "KVM_CAP_TSC_CONTROL", "TSC control"},
            {KVM_CAP_GET_TSC_KHZ, "KVM_CAP_GET_TSC_KHZ", "Get TSC frequency"},
            {KVM_CAP_PPC_BOOKE_SREGS, "KVM_CAP_PPC_BOOKE_SREGS", "PowerPC BookE special registers"},
            {KVM_CAP_SPAPR_TCE, "KVM_CAP_SPAPR_TCE", "SPAPR TCE"},
            {KVM_CAP_PPC_SMT, "KVM_CAP_PPC_SMT", "PowerPC SMT"},
            {KVM_CAP_PPC_RMA, "KVM_CAP_PPC_RMA", "PowerPC RMA"},
            {KVM_CAP_MAX_VCPUS, "KVM_CAP_MAX_VCPUS", "Maximum VCPUs (hard limit)"},
            {KVM_CAP_PPC_HIOR, "KVM_CAP_PPC_HIOR", "PowerPC HIOR"},
            {KVM_CAP_PPC_PAPR, "KVM_CAP_PPC_PAPR", "PowerPC PAPR"},
            {KVM_CAP_SW_TLB, "KVM_CAP_SW_TLB", "Software TLB"},
            {KVM_CAP_ONE_REG, "KVM_CAP_ONE_REG", "One register interface"},
            {KVM_CAP_S390_GMAP, "KVM_CAP_S390_GMAP", "S390 guest mapping"},
            {KVM_CAP_TSC_DEADLINE_TIMER, "KVM_CAP_TSC_DEADLINE_TIMER", "TSC deadline timer"},
            {KVM_CAP_S390_UCONTROL, "KVM_CAP_S390_UCONTROL", "S390 user control"},
            {KVM_CAP_SYNC_REGS, "KVM_CAP_SYNC_REGS", "Sync registers"},
            {KVM_CAP_PCI_2_3, "KVM_CAP_PCI_2_3", "PCI 2.3 features"},
            {KVM_CAP_KVMCLOCK_CTRL, "KVM_CAP_KVMCLOCK_CTRL", "KVM clock control"},
            {KVM_CAP_SIGNAL_MSI, "KVM_CAP_SIGNAL_MSI", "Signal MSI"},
            {KVM_CAP_PPC_GET_SMMU_INFO, "KVM_CAP_PPC_GET_SMMU_INFO", "PowerPC SMMU info"},
            {KVM_CAP_S390_COW, "KVM_CAP_S390_COW", "S390 copy-on-write"},
            {KVM_CAP_PPC_ALLOC_HTAB, "KVM_CAP_PPC_ALLOC_HTAB", "PowerPC HTAB allocation"},
            {KVM_CAP_READONLY_MEM, "KVM_CAP_READONLY_MEM", "Read-only memory"},
            {KVM_CAP_IRQFD_RESAMPLE, "KVM_CAP_IRQFD_RESAMPLE", "IRQ file descriptor resampling"},
            {KVM_CAP_PPC_BOOKE_WATCHDOG, "KVM_CAP_PPC_BOOKE_WATCHDOG", "PowerPC BookE watchdog"},
            {KVM_CAP_PPC_HTAB_FD, "KVM_CAP_PPC_HTAB_FD", "PowerPC HTAB file descriptor"},
            {KVM_CAP_S390_CSS_SUPPORT, "KVM_CAP_S390_CSS_SUPPORT", "S390 channel subsystem"},
            {KVM_CAP_PPC_EPR, "KVM_CAP_PPC_EPR", "PowerPC external proxy"},
            {KVM_CAP_ARM_PSCI, "KVM_CAP_ARM_PSCI", "ARM PSCI"},
            {KVM_CAP_ARM_SET_DEVICE_ADDR, "KVM_CAP_ARM_SET_DEVICE_ADDR", "ARM device address"},
            {KVM_CAP_DEVICE_CTRL, "KVM_CAP_DEVICE_CTRL", "Device control"},
            {KVM_CAP_IRQ_MPIC, "KVM_CAP_IRQ_MPIC", "MPIC interrupt controller"},
            {KVM_CAP_PPC_RTAS, "KVM_CAP_PPC_RTAS", "PowerPC RTAS"},
            {KVM_CAP_IRQ_XICS, "KVM_CAP_IRQ_XICS", "XICS interrupt controller"},
            {KVM_CAP_ARM_EL1_32BIT, "KVM_CAP_ARM_EL1_32BIT", "ARM EL1 32-bit"},
            {KVM_CAP_SPAPR_MULTITCE, "KVM_CAP_SPAPR_MULTITCE", "SPAPR multiple TCE"},
            {KVM_CAP_EXT_EMUL_CPUID, "KVM_CAP_EXT_EMUL_CPUID", "Extended emulated CPUID"},
            {KVM_CAP_HYPERV_TIME, "KVM_CAP_HYPERV_TIME", "Hyper-V time"},
            {KVM_CAP_IOAPIC_POLARITY_IGNORED, "KVM_CAP_IOAPIC_POLARITY_IGNORED", "IOAPIC polarity ignored"},
            {KVM_CAP_ENABLE_CAP_VM, "KVM_CAP_ENABLE_CAP_VM", "Enable VM capability"},
            {KVM_CAP_S390_IRQCHIP, "KVM_CAP_S390_IRQCHIP", "S390 interrupt chip"},
            {KVM_CAP_IOEVENTFD_NO_LENGTH, "KVM_CAP_IOEVENTFD_NO_LENGTH", "IO event FD no length"},
            {KVM_CAP_VM_ATTRIBUTES, "KVM_CAP_VM_ATTRIBUTES", "VM attributes"},
            {KVM_CAP_ARM_PSCI_0_2, "KVM_CAP_ARM_PSCI_0_2", "ARM PSCI 0.2"},
            {KVM_CAP_PPC_FIXUP_HCALL, "KVM_CAP_PPC_FIXUP_HCALL", "PowerPC hypercall fixup"},
            {KVM_CAP_PPC_ENABLE_HCALL, "KVM_CAP_PPC_ENABLE_HCALL", "PowerPC enable hypercall"},
            {KVM_CAP_CHECK_EXTENSION_VM, "KVM_CAP_CHECK_EXTENSION_VM", "Check VM extension"}
        };

        println("Supported Extensions:");
        println("---------------------");

        for (const auto& ext : extensions) {
            bool supported = check_extension(ext.id);
            println("{:<35} {:<8} {}", 
                        ext.name, 
                        supported ? "[YES]" : "[NO]", 
                        ext.description);
        }

        // Get numeric capability values for some extensions
        println("\nNumeric Capabilities:");
        println("--------------------");

        if (int max_vcpus = ioctl(kvm_fd, KVM_CHECK_EXTENSION, KVM_CAP_NR_VCPUS); max_vcpus > 0) {
            println("Recommended max VCPUs: {}", max_vcpus);
        }

        if (int hard_max_vcpus = ioctl(kvm_fd, KVM_CHECK_EXTENSION, KVM_CAP_MAX_VCPUS); hard_max_vcpus > 0) {
            println("Hard limit max VCPUs: {}", hard_max_vcpus);
        }

        if (int max_memslots = ioctl(kvm_fd, KVM_CHECK_EXTENSION, KVM_CAP_NR_MEMSLOTS); max_memslots > 0) {
            println("Max memory slots: {}", max_memslots);
        }

        if (int tsc_khz = ioctl(kvm_fd, KVM_CHECK_EXTENSION, KVM_CAP_GET_TSC_KHZ); tsc_khz > 0) {
            println("TSC frequency: {} kHz", tsc_khz);
        }
    }

private:
    int kvm_fd{-1};
};

int main() {
    println("=== KVM Database Probe with WAL Devices ===\n");

    // Check if running as root (required for device creation)
    if (geteuid() != 0) {
        println("Warning: Not running as root. Device creation may fail.");
        println("Try: sudo ./kvm_db\n");
    }

    // Initialize WAL device manager
    WALDeviceManager wal_manager;

    // Create the WAL devices
    println("Creating WAL devices...");
    if (auto result = wal_manager.create_devices(); !result) {
        println("Failed to create WAL devices: {}", result.error().message());
        println("Make sure you're running as root (sudo)");
        return EXIT_FAILURE;
    }

    // Test the devices
    if (auto result = wal_manager.test_devices(); !result) {
        println("Warning: WAL device test failed: {}", result.error().message());
    } else {
        println("All WAL devices verified successfully");
    }

    println(""); // Empty line for readability

    // Initialize KVM probe
    KVMProbe probe;

    if (auto result = probe.initialize(); !result) {
        println("Failed to initialize KVM: {}", result.error().message());
        println("Make sure:");
        println("1. KVM is loaded (modprobe kvm kvm-intel/kvm-amd)");
        println("2. /dev/kvm exists and is accessible");
        println("3. You have proper permissions");
        return 1;
    }

    // Run the probe
    probe.print_capabilities();

    // Device cleanup happens automatically via RAII when wal_manager goes out of scope
    println("\nShutdown: WAL devices will be cleaned up automatically...");

    return EXIT_SUCCESS;
}

