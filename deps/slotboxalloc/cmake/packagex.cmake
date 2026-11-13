
install(TARGETS slotboxalloc
    EXPORT slotboxallocTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/../include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/slotboxallocConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/SlotboxallocConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/slotboxallocConfig.cmake"
    @ONLY
)

install(EXPORT slotboxallocTargets
    FILE slotboxallocTargets.cmake
    NAMESPACE slotboxalloc::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/slotboxalloc-${PROJECT_VERSION}
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/slotboxallocConfig.cmake"
              "${CMAKE_CURRENT_BINARY_DIR}/slotboxallocConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/slotboxalloc-${PROJECT_VERSION})
