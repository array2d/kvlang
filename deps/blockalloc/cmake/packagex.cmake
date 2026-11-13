if(NOT BLOCKALLOC_PACKAGEX_INCLUDED)
    set(BLOCKALLOC_PACKAGEX_INCLUDED TRUE)

    install(TARGETS blockalloc
        EXPORT blockallocTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )

    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    include(CMakePackageConfigHelpers)
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/blockallocConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY AnyNewerVersion
    )

    configure_file(
        "${CMAKE_CURRENT_LIST_DIR}/BlockallocConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/blockallocConfig.cmake"
        @ONLY
    )

    install(EXPORT blockallocTargets
        FILE blockallocTargets.cmake
        NAMESPACE blockalloc::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/blockalloc-${PROJECT_VERSION}
    )

    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/blockallocConfig.cmake"
                  "${CMAKE_CURRENT_BINARY_DIR}/blockallocConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/blockalloc-${PROJECT_VERSION})
endif()
