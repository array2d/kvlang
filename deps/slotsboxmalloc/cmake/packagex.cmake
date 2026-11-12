
install(TARGETS slotsboxmalloc
    EXPORT slotsboxmallocTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/../include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/slotsboxmallocConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/SlotsboxmallocConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/slotsboxmallocConfig.cmake"
    @ONLY
)

install(EXPORT slotsboxmallocTargets
    FILE slotsboxmallocTargets.cmake
    NAMESPACE slotsboxmalloc::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/slotsboxmalloc-${PROJECT_VERSION}
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/slotsboxmallocConfig.cmake"
              "${CMAKE_CURRENT_BINARY_DIR}/slotsboxmallocConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/slotsboxmalloc-${PROJECT_VERSION})
