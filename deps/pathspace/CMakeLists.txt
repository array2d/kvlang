cmake_minimum_required(VERSION 3.10)
project(pathspace VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

include(GNUInstallDirs)

# Default prefix=/usr; libraries install to <prefix>/lib/pathspace to preserve consumer link paths.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "" FORCE)
endif()

# Unified pathspace library: built-in shm engine + XValue byte layer (head/attr) + storage constants.
# Language-layer constructors, langtype, and coordinate comparison live in <pathlang/xvalue.h>.
add_library(pathspace SHARED
    src/shm/pathspace.c
    src/xvalue_head.c
    src/const.c)
target_include_directories(pathspace PRIVATE src/shm)

# -- Header-only ABI dependencies --------------------------------------------
# src/shm/pathspace.c includes blockalloc and slotboxalloc headers with
# `#define *_IMPLEMENTATION`. Search order:
#   1. <this-dir>/../<name>/include   in-tree deps/ layout (self-contained artifact)
#   2. /usr/local/include             ci/deps.sh target on macOS / local installs
#   3. /usr/include                   ci/deps.sh target on Linux
# Override explicitly with -DPATHSPACE_<name>_INCLUDE_DIRECTORY=<dir>.
# NO_DEFAULT_PATH prevents falling back to unpinned compiler default paths.
foreach(header IN ITEMS blockalloc/blockalloc.h slotboxalloc/slotboxobj.h)
    string(REGEX REPLACE "/[^/]+$" "" header_directory "${header}")
    set(pin_candidates
        "${CMAKE_CURRENT_SOURCE_DIR}/../${header_directory}/include"
        "/usr/local/include"
        "/usr/include")
    find_path(PATHSPACE_${header_directory}_INCLUDE_DIRECTORY
        NAMES ${header}
        PATHS ${pin_candidates}
        NO_DEFAULT_PATH)
    if(NOT PATHSPACE_${header_directory}_INCLUDE_DIRECTORY)
        # List candidates one per line instead of a semicolon-joined CMake list.
        set(candidate_lines "")
        set(candidate_index 1)
        foreach(candidate IN LISTS pin_candidates)
            string(APPEND candidate_lines "    ${candidate_index}. ${candidate}\n")
            math(EXPR candidate_index "${candidate_index} + 1")
        endforeach()
        message(FATAL_ERROR
            "Cannot find <${header}>.\n"
            "  Candidate directories checked (in priority order):\n"
            "${candidate_lines}"
            "  To fix (choose one):\n"
            "    1. Run ./ci/deps.sh (versions pinned in deps.json);\n"
            "    2. Pass -DPATHSPACE_${header_directory}_INCLUDE_DIRECTORY=<dir containing ${header_directory}/>.")
    endif()
    message(STATUS "pathspace: <${header}> -> ${PATHSPACE_${header_directory}_INCLUDE_DIRECTORY}")
    target_include_directories(pathspace PRIVATE
        "${PATHSPACE_${header_directory}_INCLUDE_DIRECTORY}")
endforeach()

# pathspace does not depend on pathlang (the language layer depends on storage, never the reverse).
# Tests construct wire values locally via tests/shm/test_wire.h inline helpers.

include(CTest)
if(BUILD_TESTING)
    # XValue byte-layer and storage contract tests (language-layer codec tests live in pathlang/xvalue/tests/).
    add_executable(test_storage_shm tests/test_storage_shm.c)
    target_include_directories(test_storage_shm PRIVATE include)
    target_link_libraries(test_storage_shm PRIVATE pathspace)
    add_test(NAME storage_shm_contract COMMAND test_storage_shm)
endif()

# shm backend tests (prefixed with `shm_` to avoid target/test name collisions).
if(BUILD_TESTING)
    # Encode pairs as "ctest_name|source_stem" (`|` avoids CMake `;` list splitting).
    set(PATHSPACE_SHM_TEST_PAIRS
        "art_scan|test_art_scan"
        "art_alignment|test_art_alignment"
        "anchor_hook|test_anchor_hook"
        "shm_split|test_shm_split"
        "storage_abi|test_storage_abi"
        )
    foreach(pair IN LISTS PATHSPACE_SHM_TEST_PAIRS)
        string(FIND "${pair}" "|" pair_sep)
        string(SUBSTRING "${pair}" 0 ${pair_sep} shm_name)
        math(EXPR pair_tail "${pair_sep} + 1")
        string(SUBSTRING "${pair}" ${pair_tail} -1 shm_source)
        add_executable(shm_${shm_name} tests/shm/${shm_source}.c)
        target_include_directories(shm_${shm_name} PRIVATE
            src/shm include)
        # Enable internal test hooks (TestAnchorOf / ArtAlignCheck / TestSetFormatVersion)
        # matching the library's P2_TEST_HOOKS build definition.
        target_compile_definitions(shm_${shm_name} PRIVATE P2_TEST_HOOKS)
        target_link_libraries(shm_${shm_name} PRIVATE pathspace)
        add_test(NAME shm_${shm_name} COMMAND shm_${shm_name})
    endforeach()

    # Export-surface link test: references all 16 public symbols so any missing export fails at link time.
    add_executable(test_export_surface tests/test_export_surface.c)
    target_include_directories(test_export_surface PRIVATE include)
    target_link_libraries(test_export_surface PRIVATE pathspace)
    add_test(NAME export_surface COMMAND test_export_surface)
endif()

# Frontend SONAME: libpathspace.so.1
set_target_properties(pathspace PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
)

target_include_directories(pathspace
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Active handle table (atexit flush) uses pthread mutexes.
find_package(Threads REQUIRED)
target_link_libraries(pathspace PRIVATE Threads::Threads)

# Export only public API symbols in non-test builds; keep default visibility when BUILD_TESTING=ON
# so unit tests can link internal pathspaceShm* helpers directly.
if(NOT BUILD_TESTING)
    set_target_properties(pathspace PROPERTIES C_VISIBILITY_PRESET hidden)
    target_compile_options(pathspace PRIVATE -fvisibility=hidden)
endif()

target_compile_definitions(pathspace PRIVATE _GNU_SOURCE)

# Enable internal observation/injection hooks only in test builds.
if(BUILD_TESTING)
    target_compile_definitions(pathspace PRIVATE P2_TEST_HOOKS)
endif()

# Reject implicit function declarations at compile time.
target_compile_options(pathspace PRIVATE -Werror=implicit-function-declaration)

install(TARGETS pathspace
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/pathspace
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

configure_file(pathspace.pc.in ${CMAKE_CURRENT_BINARY_DIR}/pathspace.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/pathspace.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

# -- CLI (standalone Cargo project in cli/) ----------------------------------
# Uses libpathspace C ABI and installs to <prefix>/bin.
# Embed relative RUNPATH entries:
#   $ORIGIN/../lib                      -- release tarball (bin/ + lib/)
#   $ORIGIN/../<libdir>/pathspace         -- cmake --install (<prefix>/<libdir>/pathspace)
# Use @loader_path on macOS; escape $ as $$ for CMake.
# release.yml packages this binary directly without rebuilding.
#
# Storage-only subcommands (del/deltree/cp/cpdir/mkindex/clear/list/tree/head/attr);
# links only libpathspace with no dependency on pathlang.
find_program(CARGO_EXECUTABLE cargo)
if(CARGO_EXECUTABLE)
  set(PATHSPACE_CLI_BIN "${CMAKE_CURRENT_SOURCE_DIR}/cli/target/release/pathspace")
  if(APPLE)
    set(PATHSPACE_CLI_ORIGIN "@loader_path")
  else()
    set(PATHSPACE_CLI_ORIGIN "$$ORIGIN")
  endif()
  add_custom_target(pathspace-cli ALL
    COMMAND ${CMAKE_COMMAND} -E env
            "RUSTFLAGS=-L native=$<TARGET_FILE_DIR:pathspace> -C link-arg=-Wl,-rpath,'${PATHSPACE_CLI_ORIGIN}/../lib' -C link-arg=-Wl,-rpath,'${PATHSPACE_CLI_ORIGIN}/../${CMAKE_INSTALL_LIBDIR}/pathspace'"
            "${CARGO_EXECUTABLE}" build --release
            --manifest-path "${CMAKE_CURRENT_SOURCE_DIR}/cli/Cargo.toml"
    BYPRODUCTS "${PATHSPACE_CLI_BIN}"
    DEPENDS pathspace
    COMMENT "Building pathspace CLI (cli/)")
  install(PROGRAMS "${PATHSPACE_CLI_BIN}" DESTINATION ${CMAKE_INSTALL_BINDIR})
else()
  message(STATUS "cargo not found; skipping pathspace CLI (cli/)")
endif()
