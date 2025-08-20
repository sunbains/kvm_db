# cmake/CompilerWarnings.cmake - Compiler-specific warning configurations

function(configure_project_warnings target_name)
    # Base warning set for all compilers
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(${target_name} INTERFACE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wcast-align
            -Wunused
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            -Wnull-dereference
            -Wdouble-promotion
            -Wformat=2
            -Wimplicit-fallthrough
        )
        
        # GCC-specific warnings
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            target_compile_options(${target_name} INTERFACE
                -Wmisleading-indentation
                -Wduplicated-cond
                -Wduplicated-branches
                -Wlogical-op
                -Wuseless-cast
            )
        endif()
        
        # Clang-specific warnings
        if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            target_compile_options(${target_name} INTERFACE
                -Wlifetime
                -Wloop-analysis
                -Wthread-safety
            )
        endif()
        
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(${target_name} INTERFACE
            /W4     # High warning level
            /w14640 # Enable warning for thread unsafe static member initialization
            /w14242 # 'identifier': conversion from 'type1' to 'type1', possible loss of data
            /w14254 # 'operator': conversion from 'type1:field_bits' to 'type2:field_bits'
            /w14263 # 'function': member function does not override any base class virtual function
            /w14265 # 'classname': class has virtual functions, but destructor is not virtual
            /w14287 # 'operator': unsigned/negative constant mismatch
            /we4289 # Loop control variable declared in the for-loop and used outside
            /w14296 # 'operator': expression is always 'boolean_value'
            /w14311 # 'variable': pointer truncation from 'type1' to 'type2'
            /w14545 # Expression before comma evaluates to function missing argument list
            /w14546 # Function call before comma missing argument list
            /w14547 # 'operator': operator before comma has no effect
            /w14549 # 'operator': operator before comma has no effect
            /w14555 # Expression has no effect; expected expression with side-effect
            /w14619 # Pragma warning: there is no warning number 'number'
            /w14640 # Enable warning for thread unsafe static member initialization
            /w14826 # Conversion from 'type1' to 'type_2' is sign-extended
            /w14905 # Wide string literal cast to 'LPSTR'
            /w14906 # String literal cast to 'LPWSTR'
            /w14928 # Illegal copy-initialization
        )
    endif()
endfunction()

