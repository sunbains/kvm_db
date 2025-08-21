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
template <typename... Args>
void println(const std::string& fmt, Args&&... args) {
#if USE_STD_PRINT
  std::println(fmt, std::forward<Args>(args)...);
#elif HAVE_STD_FORMAT
  std::cout << std::format(fmt, std::forward<Args>(args)...) << '\\n';
#else
  // Basic fallback - just print the format string
  std::cout << fmt << '\\n';
#endif
}

template <typename... Args>
void print(const std::string& fmt, Args&&... args) {
#if USE_STD_PRINT
  std::print(fmt, std::forward<Args>(args)...);
#elif HAVE_STD_FORMAT
  std::cout << std::format(fmt, std::forward<Args>(args)...);
#else
  std::cout << fmt;
#endif
}

// Simple overloads for basic string output
inline void println(const std::string& msg) { std::cout << msg << '\\n'; }

inline void print(const std::string& msg) { std::cout << msg; }

}  // namespace kvm_db
