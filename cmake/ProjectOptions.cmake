# cmake/ProjectOptions.cmake - Project-wide configuration options

function(configure_project_options target_name)
    # Set C++23 standard with strict requirements
    target_compile_features(${target_name} INTERFACE cxx_std_23)
    
    # Disable compiler extensions for portability
    set_target_properties(${target_name} PROPERTIES
        CXX_EXTENSIONS OFF
        CXX_STANDARD_REQUIRED ON
    )
    
    # Debug/Release configuration with generator expressions
    target_compile_options(${target_name} INTERFACE
        # Debug configuration
        $<$<CONFIG:Debug>:-g3 -O0 -fno-omit-frame-pointer>
        $<$<CONFIG:Debug>:-DDEBUG>
        $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU,Clang>>:-fsanitize=address,undefined>
        
        # Release configuration  
        $<$<CONFIG:Release>:-O3 -DNDEBUG>
        $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU>>:-march=native>
        $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:Clang>>:-march=native>
    )
    
    # Link-time options for sanitizers in debug
    target_link_options(${target_name} INTERFACE
        $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU,Clang>>:-fsanitize=address,undefined>
    )
    
    # Linux-specific compile definitions
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(${target_name} INTERFACE
            _GNU_SOURCE=1
            LINUX_SYSTEM=1
        )
    endif()
    
    # Compiler-specific C++23 handling
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "11")
            message(FATAL_ERROR "GCC 11 or later required for C++23 support")
        endif()
        target_compile_options(${target_name} INTERFACE -fconcepts)
        
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "13")
            message(FATAL_ERROR "Clang 13 or later required for C++23 support")
        endif()
        # May need -stdlib=libc++ for some C++23 features
        if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "16")
            target_compile_options(${target_name} INTERFACE -stdlib=libc++)
            target_link_options(${target_name} INTERFACE -stdlib=libc++)
        endif()
        
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "19.30")
            message(FATAL_ERROR "MSVC 19.30 (VS 2022) or later required for C++23")
        endif()
        target_compile_options(${target_name} INTERFACE /permissive- /Zc:__cplusplus)
    endif()
endfunction()
