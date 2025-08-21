#pragma once

#include <expected>
#include <system_error>

#include "kvm_db/config.h"

struct KVM_probe {
  KVM_probe() = default;

  ~KVM_probe() {
    if (m_kvm_fd >= 0) {
      close(m_kvm_fd);
    }
  }

  // Non-copyable, movable
  KVM_probe(const KVM_probe&) = delete;
  KVM_probe& operator=(const KVM_probe&) = delete;
  KVM_probe(KVM_probe&& other) noexcept : m_kvm_fd(std::exchange(other.m_kvm_fd, -1)) {}

  KVM_probe& operator=(KVM_probe&& other) noexcept;

  [[nodiscard]] std::expected<void, std::error_code> initialize();

  [[nodiscard]] std::expected<int, std::error_code> get_api_version() const;

  [[nodiscard]] bool check_extension(int extension) const;

  [[nodiscard]] std::expected<size_t, std::error_code> get_vcpu_mmap_size() const;

  void print_capabilities() const;

private:
  int m_kvm_fd{-1};
};
