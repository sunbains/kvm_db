# cmake/CXX23Features.cmake - C++23 feature detection

include(CheckCXXSourceCompiles)

# Function to detect C++23 features
function(detect_cxx23_features)
    message(STATUS "Detecting C++23 features...")

    # Save current CMAKE_REQUIRED_* settings
    set(CMAKE_REQUIRED_FLAGS_SAVE ${CMAKE_REQUIRED_FLAGS})
    set(CMAKE_REQUIRED_DEFINITIONS_SAVE ${CMAKE_REQUIRED_DEFINITIONS})
    
    # Set C++23 standard for feature tests
    set(CMAKE_REQUIRED_FLAGS "-std=c++23")

    # Test for ranges support
    check_cxx_source_compiles("
        #include <ranges>
        #include <vector>
        int main() {
            std::vector<int> v{1, 2, 3};
            auto r = v | std::views::filter([](int i) { return i > 1; });
            return 0;
        }
    " HAVE_CXX23_RANGES)

    # Test for concepts support
    check_cxx_source_compiles("
        #include <concepts>
        template<typename T>
        concept Integral = std::integral<T>;
        
        template<Integral T>
        T add(T a, T b) { return a + b; }
        
        int main() {
            return add(1, 2);
        }
    " HAVE_CXX23_CONCEPTS)

    # Test for coroutines support
    check_cxx_source_compiles("
        #include <coroutine>
        struct task {
            struct promise_type {
                task get_return_object() { return {}; }
                std::suspend_never initial_suspend() { return {}; }
                std::suspend_never final_suspend() noexcept { return {}; }
                void return_void() {}
                void unhandled_exception() {}
            };
        };
        
        task example() {
            co_return;
        }
        
        int main() {
            auto t = example();
            return 0;
        }
    " HAVE_CXX23_COROUTINES)

    # Test for modules support (basic check)
    # Note: Full module support testing is complex and compiler-dependent
    check_cxx_source_compiles("
        #ifdef __cpp_modules
        #if __cpp_modules >= 201907L
        #define MODULES_SUPPORTED 1
        #endif
        #endif
        
        int main() {
        #ifdef MODULES_SUPPORTED
            return 0;
        #else
            // Force compilation failure if modules not supported
            static_assert(false, \"Modules not supported\");
        #endif
        }
    " HAVE_CXX23_MODULES)

    # Test for std::format (C++20 feature commonly available in C++23 implementations)
    check_cxx_source_compiles("
        #include <format>
        #include <string>
        int main() {
            std::string s = std::format(\"Hello, {}!\", \"world\");
            return 0;
        }
    " HAVE_STD_FORMAT)

    # Test for std::expected (C++23 feature)
    check_cxx_source_compiles("
        #include <expected>
        int main() {
            std::expected<int, std::string> result = 42;
            return result.has_value() ? 0 : 1;
        }
    " HAVE_STD_EXPECTED)

    # Test for std::print (C++23 feature)
    check_cxx_source_compiles("
        #include <print>
        int main() {
            std::print(\"Hello, world!\\n\");
            return 0;
        }
    " HAVE_STD_PRINT)

    # Test for std::generator (C++23 feature)
    check_cxx_source_compiles("
        #include <generator>
        std::generator<int> count() {
            for (int i = 0; i < 3; ++i) {
                co_yield i;
            }
        }
        int main() {
            auto gen = count();
            return 0;
        }
    " HAVE_STD_GENERATOR)

    # Test for if consteval (C++23 feature)
    check_cxx_source_compiles("
        consteval int compile_time_func() { return 42; }
        constexpr int runtime_func() { return 24; }
        
        constexpr int test() {
            if consteval {
                return compile_time_func();
            } else {
                return runtime_func();
            }
        }
        
        int main() {
            constexpr int result = test();
            return 0;
        }
    " HAVE_IF_CONSTEVAL)

    # Restore CMAKE_REQUIRED_* settings
    set(CMAKE_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS_SAVE})
    set(CMAKE_REQUIRED_DEFINITIONS ${CMAKE_REQUIRED_DEFINITIONS_SAVE})

    # Report results
    message(STATUS "C++23 feature detection results:")
    message(STATUS "  Ranges:         ${HAVE_CXX23_RANGES}")
    message(STATUS "  Concepts:       ${HAVE_CXX23_CONCEPTS}")
    message(STATUS "  Coroutines:     ${HAVE_CXX23_COROUTINES}")
    message(STATUS "  Modules:        ${HAVE_CXX23_MODULES}")
    message(STATUS "  std::format:    ${HAVE_STD_FORMAT}")
    message(STATUS "  std::expected:  ${HAVE_STD_EXPECTED}")
    message(STATUS "  std::print:     ${HAVE_STD_PRINT}")
    message(STATUS "  std::generator: ${HAVE_STD_GENERATOR}")
    message(STATUS "  if consteval:   ${HAVE_IF_CONSTEVAL}")

    # Set variables in parent scope
    set(HAVE_CXX23_RANGES ${HAVE_CXX23_RANGES} PARENT_SCOPE)
    set(HAVE_CXX23_CONCEPTS ${HAVE_CXX23_CONCEPTS} PARENT_SCOPE)
    set(HAVE_CXX23_COROUTINES ${HAVE_CXX23_COROUTINES} PARENT_SCOPE)
    set(HAVE_CXX23_MODULES ${HAVE_CXX23_MODULES} PARENT_SCOPE)
    set(HAVE_STD_FORMAT ${HAVE_STD_FORMAT} PARENT_SCOPE)
    set(HAVE_STD_EXPECTED ${HAVE_STD_EXPECTED} PARENT_SCOPE)
    set(HAVE_STD_PRINT ${HAVE_STD_PRINT} PARENT_SCOPE)
    set(HAVE_STD_GENERATOR ${HAVE_STD_GENERATOR} PARENT_SCOPE)
    set(HAVE_IF_CONSTEVAL ${HAVE_IF_CONSTEVAL} PARENT_SCOPE)

    # Provide recommendations if features are missing
    if(NOT HAVE_CXX23_RANGES OR NOT HAVE_CXX23_CONCEPTS)
        message(STATUS "")
        message(STATUS "Some C++23 features are not available.")
        message(STATUS "Consider updating your compiler:")
        message(STATUS "  GCC 12+ or Clang 15+ recommended for full C++23 support")
    endif()
endfunction()

# Function to configure compiler flags based on available features
function(configure_cxx23_flags target)
    # Enable C++23 standard
    target_compile_features(${target} PRIVATE cxx_std_23)
    
    # Add feature-specific definitions
    if(HAVE_CXX23_RANGES)
        target_compile_definitions(${target} PRIVATE HAVE_CXX23_RANGES=1)
    endif()
    
    if(HAVE_CXX23_CONCEPTS)
        target_compile_definitions(${target} PRIVATE HAVE_CXX23_CONCEPTS=1)
    endif()
    
    if(HAVE_CXX23_COROUTINES)
        target_compile_definitions(${target} PRIVATE HAVE_CXX23_COROUTINES=1)
    endif()
    
    if(HAVE_CXX23_MODULES)
        target_compile_definitions(${target} PRIVATE HAVE_CXX23_MODULES=1)
    endif()
    
    if(HAVE_STD_FORMAT)
        target_compile_definitions(${target} PRIVATE HAVE_STD_FORMAT=1)
    endif()
    
    if(HAVE_STD_EXPECTED)
        target_compile_definitions(${target} PRIVATE HAVE_STD_EXPECTED=1)
    endif()
    
    if(HAVE_STD_PRINT)
        target_compile_definitions(${target} PRIVATE HAVE_STD_PRINT=1)
    endif()
    
    if(HAVE_STD_GENERATOR)
        target_compile_definitions(${target} PRIVATE HAVE_STD_GENERATOR=1)
    endif()
    
    if(HAVE_IF_CONSTEVAL)
        target_compile_definitions(${target} PRIVATE HAVE_IF_CONSTEVAL=1)
    endif()
endfunction()

