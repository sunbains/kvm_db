# cmake/CXX23Features.cmake - C++23 feature detection

include(CheckCXXSourceCompiles)

function(detect_cxx23_features)
    # Test for std::print/std::println availability
    check_cxx_source_compiles("
        #include <print>
        int main() {
            std::println(\"Hello, World!\");
            return 0;
        }
    " HAVE_STD_PRINT)
    
    if(HAVE_STD_PRINT)
        message(STATUS "std::print support detected")
        target_compile_definitions(kvm_db_options INTERFACE HAVE_STD_PRINT=1)
    else()
        message(STATUS "std::print not available, will use fallbacks")
        target_compile_definitions(kvm_db_options INTERFACE HAVE_STD_PRINT=0)
    endif()
    
    # Test for std::expected
    check_cxx_source_compiles("
        #include <expected>
        int main() {
            std::expected<int, int> e = 42;
            return e.value();
        }
    " HAVE_STD_EXPECTED)
    
    if(HAVE_STD_EXPECTED)
        message(STATUS "std::expected support detected")
        target_compile_definitions(kvm_db_options INTERFACE HAVE_STD_EXPECTED=1)
    else()
        message(STATUS "std::expected not available")
        target_compile_definitions(kvm_db_options INTERFACE HAVE_STD_EXPECTED=0)
    endif()
    
    # Test for std::format (should be available in C++20)
    check_cxx_source_compiles("
        #include <format>
        int main() {
            auto s = std::format(\"Hello, {}!\", \"World\");
            return 0;
        }
    " HAVE_STD_FORMAT)
    
    if(HAVE_STD_FORMAT)
        message(STATUS "std::format support detected")
        target_compile_definitions(kvm_db_options INTERFACE HAVE_STD_FORMAT=1)
    else()
        message(STATUS "std::format not available")
        target_compile_definitions(kvm_db_options INTERFACE HAVE_STD_FORMAT=0)
    endif()
    
    # Compiler-specific std::print detection
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND 
       CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "19.37")
        message(STATUS "MSVC with std::print support detected")
        target_compile_definitions(kvm_db_options INTERFACE COMPILER_SUPPORTS_STD_PRINT=1)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND 
           CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "14")
        message(STATUS "GCC 14+ detected, std::print may be available")
        target_compile_definitions(kvm_db_options INTERFACE COMPILER_SUPPORTS_STD_PRINT=1)
    else()
        target_compile_definitions(kvm_db_options INTERFACE COMPILER_SUPPORTS_STD_PRINT=0)
    endif()
endfunction()

# Additional feature test for specific C++23 library features  
function(check_cxx23_feature feature_name test_code)
    set(test_file "${CMAKE_BINARY_DIR}/test_${feature_name}.cpp")
    file(WRITE "${test_file}" "${test_code}")
    
    try_compile(${feature_name}_AVAILABLE
        "${CMAKE_BINARY_DIR}"
        "${test_file}"
        CMAKE_FLAGS "-DCMAKE_CXX_STANDARD=23"
        OUTPUT_VARIABLE compile_output
    )
    
    if(${feature_name}_AVAILABLE)
        message(STATUS "${feature_name} is available")
        target_compile_definitions(kvm_db_options INTERFACE HAVE_${feature_name}=1)
    else()
        message(STATUS "${feature_name} is not available")
        target_compile_definitions(kvm_db_options INTERFACE HAVE_${feature_name}=0)
    endif()
endfunction()

