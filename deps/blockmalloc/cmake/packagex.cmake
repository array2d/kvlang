if(NOT BLOCKMALLOC_PACKAGEX_INCLUDED)
    set(BLOCKMALLOC_PACKAGEX_INCLUDED TRUE)

    install(TARGETS blockmalloc
        EXPORT blockmallocTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )

    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    include(CMakePackageConfigHelpers)
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/blockmallocConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY AnyNewerVersion
    )

    configure_file(
        "${CMAKE_CURRENT_LIST_DIR}/BlockmallocConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/blockmallocConfig.cmake"
        @ONLY
    )

    install(EXPORT blockmallocTargets
        FILE blockmallocTargets.cmake
        NAMESPACE blockmalloc::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/blockmalloc-${PROJECT_VERSION}
    )

    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/blockmallocConfig.cmake"
                  "${CMAKE_CURRENT_BINARY_DIR}/blockmallocConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/blockmalloc-${PROJECT_VERSION})
endif()
