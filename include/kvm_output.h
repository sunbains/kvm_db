#pragma once

#include "kvm_db/config.h"

#if USE_STD_PRINT
#include <print>
#else
#include <iostream>
#if HAVE_STD_FORMAT
#include <format>
#endif
#endif

namespace kvm_db {

// Modern C++23 print function with fallbacks
#if USE_STD_PRINT
template <typename... Args>
void println(const std::string& fmt, Args&&... args) {
  std::println(fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void print(const std::string& fmt, Args&&... args) {
  std::print(fmt, std::forward<Args>(args)...);
}
#elif HAVE_STD_FORMAT
template <typename... Args>
void println(const std::string& fmt, Args&&... args) {
  std::cout << std::format(fmt, std::forward<Args>(args)...) << '\n';
}

template <typename... Args>
void print(const std::string& fmt, Args&&... args) {
  std::cout << std::format(fmt, std::forward<Args>(args)...);
}
#else
// Fallback implementation - suppress unused parameter warnings
template <typename... Args>
void println(const std::string& fmt, [[maybe_unused]] Args&&... args) {
  // Basic fallback - just print the format string
  std::cout << fmt << '\n';
}

template <typename... Args>
void print(const std::string& fmt, [[maybe_unused]] Args&&... args) {
  std::cout << fmt;
}
#endif

// Simple overloads for basic string output (available in all cases)
inline void println(const std::string& msg) { std::cout << msg << '\n'; }
inline void print(const std::string& msg) { std::cout << msg; }

}  // namespace kvm_db
