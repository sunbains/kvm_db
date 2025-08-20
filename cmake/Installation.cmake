# cmake/Installation.cmake - Installation and packaging configuration

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

function(configure_installation)
    # Install the executable
    install(TARGETS kvm_db
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT Runtime
    )
    
    # Install documentation files
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        DESTINATION ${CMAKE_INSTALL_DOCDIR}
        COMPONENT Documentation
        OPTIONAL
    )
    
    # CPack configuration for packaging
    include(CPack)
    
    set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
    set(CPACK_PACKAGE_VENDOR "KVM Probe Project")
    set(CPACK_PACKAGE_CONTACT "developer@example.com")
    
    # Platform-specific package generators
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # Prefer native package formats on Linux
        find_program(DPKG_PROGRAM dpkg DOC "dpkg program of Debian-based systems")
        find_program(RPM_PROGRAM rpm DOC "rpm program of RPM-based systems")
        
        if(DPKG_PROGRAM)
            list(APPEND CPACK_GENERATOR "DEB")
            set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
            set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
            set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "${PROJECT_DESCRIPTION}")
        endif()
        
        if(RPM_PROGRAM)
            list(APPEND CPACK_GENERATOR "RPM")
            set(CPACK_RPM_PACKAGE_GROUP "Applications/System")
            set(CPACK_RPM_PACKAGE_LICENSE "MIT")
        endif()
        
        # Always include generic formats
        list(APPEND CPACK_GENERATOR "TGZ")
    else()
        # Generic formats for other platforms
        set(CPACK_GENERATOR "TGZ;ZIP")
    endif()
    
    # Component-based packaging
    set(CPACK_COMPONENTS_ALL Runtime Documentation)
    set(CPACK_COMPONENT_RUNTIME_DISPLAY_NAME "KVM Probe Executable")
    set(CPACK_COMPONENT_RUNTIME_DESCRIPTION "Main KVM probe executable")
    set(CPACK_COMPONENT_DOCUMENTATION_DISPLAY_NAME "Documentation")
    set(CPACK_COMPONENT_DOCUMENTATION_DESCRIPTION "Documentation files")
    
    # DEB-specific configuration
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.17), libstdc++6 (>= 4.9)")
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "linux-headers")
    
    # RPM-specific configuration
    set(CPACK_RPM_PACKAGE_REQUIRES "glibc >= 2.17, libstdc++ >= 4.9")
    set(CPACK_RPM_PACKAGE_SUGGESTS "kernel-headers")
endfunction()

