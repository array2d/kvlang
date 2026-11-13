# Component: runtimec top-level build
# Role: C11-only (no libstdc++); builds independent targets per component and
#       generates export scripts for three platforms from interface/exports.txt.
#       Produces libpathlang_runtime (not for benchmarks, tutorials, or releases).
# Links: Dependencies follow only the one-way graph declared here; JIT is internal to Compiler.
cmake_minimum_required(VERSION 3.20)
project(pathlang_runtimec_scaffold VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

option(RUNTIMEC_ENABLE_JIT "Enable BinRWIR JIT compilation capability" ON)
# Temporary: in-process output operators (print/cerr); the production implementation
# belongs to the pathseek extension runtime (issue #622 decision D).
# Default OFF; **must be OFF for .so artifacts**.
option(RUNTIMEC_TEMP_OUTPUT "TEMPORARY: in-process output operators for tutorial tests" OFF)
if(RUNTIMEC_TEMP_OUTPUT)
    message(STATUS "runtimec: TEMPORARY output operators enabled (must be OFF for .so)")
endif()

set(RUNTIMEC_INCLUDE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include")
# The **byte-layout** header for XValue lives in pathspace; the **language-layer**
# header lives in this repo (moved from pathspace in TASK-082: langtype, constructors,
# and coordinate comparators are language concepts and do not belong in storage).
#
# **Required, with no default**: a sibling directory layout is a local developer
# coincidence, not a contract. Defaults previously caused three failure modes: CMake
# silently picking up an unpinned sibling checkout, CI freezing directory layout after
# side-by-side clones, and pod rebuilds breaking paths with misleading errors. With a
# default, the explicit path goes untested.
set(PATHSPACE_INCLUDE_DIRECTORY "" CACHE PATH
    "pathspace repo include/ directory (providing <pathspace/pathspace_xvalue.h>); required")
if(NOT PATHSPACE_INCLUDE_DIRECTORY)
    message(FATAL_ERROR
        "Missing -DPATHSPACE_INCLUDE_DIRECTORY=<pathspace include/>.\n"
        "Example: cmake -S runtimec -B build/runtimec \\\n"
        "           -DPATHSPACE_INCLUDE_DIRECTORY=/path/to/pathspace/include")
endif()
# The pathspace headers have a single definition site: include/ in the `pathspace` repo.
# Reject stale copies inside pathlang so an 8-byte copy cannot shadow the 16-byte header.
set(PATHLANG_STALE_INCLUDE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../include/pathspace")
if(EXISTS "${PATHLANG_STALE_INCLUDE_DIRECTORY}")
    message(FATAL_ERROR "pathlang/include/pathspace/ must not exist (stale copy of the "
        "pathspace headers). Delete it; the single definition is "
        "${PATHSPACE_INCLUDE_DIRECTORY}/pathspace.")
endif()
if(NOT EXISTS "${PATHSPACE_INCLUDE_DIRECTORY}/pathspace/pathspace_xvalue.h")
    message(FATAL_ERROR
        "PATHSPACE_INCLUDE_DIRECTORY=${PATHSPACE_INCLUDE_DIRECTORY} does not contain "
        "pathspace/pathspace_xvalue.h; it must point to include/ in the pathspace repo.")
endif()
get_filename_component(PATHLANG_INCLUDE_DIRECTORY
    "${CMAKE_CURRENT_SOURCE_DIR}/../include" ABSOLUTE)
if(NOT EXISTS "${PATHLANG_INCLUDE_DIRECTORY}/pathlang/xvalue.h")
    message(FATAL_ERROR "runtimec requires pathlang/include/pathlang/xvalue.h "
        "(looked in ${PATHLANG_INCLUDE_DIRECTORY}/pathlang).")
endif()

# Language-layer static library built from pathlang/xvalue. Constructors
# (pathlangXvalueHeadNew*) and coordinate comparators live here rather than in libpathspace.
# Do not call `set(... "" CACHE FILEPATH)` beforehand: an empty cache entry causes
# find_library to **skip searching**, breaking the default search path unless -D is passed.
# Let find_library create the cache entry itself; explicit -D still takes precedence.
find_library(PATHLANG_XVALUE_LIBRARY_PATH NAMES pathlang_xvalue
    PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../lib" NO_DEFAULT_PATH
    DOC "libpathlang_xvalue.a (built from pathlang/xvalue)")
if(NOT PATHLANG_XVALUE_LIBRARY_PATH)
    message(FATAL_ERROR "cannot find libpathlang_xvalue.a (searched pathlang/lib). "
        "Fix: cmake -S pathlang/xvalue -B <dir> && cmake --build <dir>, then copy the "
        ".a into pathlang/lib or pass -DPATHLANG_XVALUE_LIBRARY_PATH=<path>.")
endif()

set(RUNTIMEC_GENERATED_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${RUNTIMEC_GENERATED_DIRECTORY}")

# Real library path: storage access links against it directly, not a test double.
set(RUNTIMEC_PATHSPACE_LIBRARY_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../lib"
    CACHE PATH "directory holding libpathspace")
find_library(RUNTIMEC_PATHSPACE_LIBRARY_PATH NAMES pathspace
    PATHS "${RUNTIMEC_PATHSPACE_LIBRARY_DIRECTORY}" NO_DEFAULT_PATH)
if(NOT RUNTIMEC_PATHSPACE_LIBRARY_PATH)
    message(FATAL_ERROR "cannot find libpathspace (searched "
        "${RUNTIMEC_PATHSPACE_LIBRARY_DIRECTORY}). Fix: build the pathspace include dir there "
        "(pathlang and pathspace must be siblings), or configure with "
        "-DRUNTIMEC_PATHSPACE_LIBRARY_DIRECTORY=<dir holding libpathspace>.")
endif()

# Three-platform export scripts: interface/exports.txt is the sole symbol manifest.
set(RUNTIMEC_EXPORT_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/interface/exports.txt")
file(STRINGS "${RUNTIMEC_EXPORT_MANIFEST}" RUNTIMEC_PUBLIC_SYMBOLS)
set(elf "{\n  global:\n")
set(macho "")
set(windows "LIBRARY pathlang_runtime\nEXPORTS\n")
foreach(symbol IN LISTS RUNTIMEC_PUBLIC_SYMBOLS)
    string(APPEND elf "    ${symbol};\n")
    string(APPEND macho "_${symbol}\n")
    string(APPEND windows "    ${symbol}\n")
endforeach()
string(APPEND elf "  local: *;\n};\n")
file(WRITE "${RUNTIMEC_GENERATED_DIRECTORY}/exports.map" "${elf}")
file(WRITE "${RUNTIMEC_GENERATED_DIRECTORY}/exports.macos" "${macho}")
file(WRITE "${RUNTIMEC_GENERATED_DIRECTORY}/exports.def" "${windows}")

function(runtimec_configure_target target_name)
    target_compile_features(${target_name} PRIVATE c_std_11)
    target_include_directories(${target_name} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}" "${RUNTIMEC_INCLUDE_DIRECTORY}"
        "${PATHSPACE_INCLUDE_DIRECTORY}" "${PATHLANG_INCLUDE_DIRECTORY}")
    set_target_properties(${target_name} PROPERTIES
        C_VISIBILITY_PRESET hidden POSITION_INDEPENDENT_CODE ON)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(${target_name} PRIVATE
            -Wall -Wextra -Wpedantic -Werror
            -Wstrict-prototypes -Wmissing-prototypes)
    elseif(MSVC)
        target_compile_options(${target_name} PRIVATE /W4)
    endif()
endfunction()

# Every component must own real source files: missing components fail immediately at
# configure time rather than masking missing capabilities behind empty translation units.
function(runtimec_add_component target_name component_path)
    file(GLOB_RECURSE sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/${component_path}/*.c")
    if(NOT sources)
        message(FATAL_ERROR "runtimec component ${target_name} has no source files")
    endif()
    add_library(${target_name} STATIC ${sources})
    runtimec_configure_target(${target_name})
endfunction()

# Six explicit calls (**do not use foreach**): `check_layers.py` verifies the one-way
# dependency graph (`EXPECTED_COMPONENT_ORDER`) from the literal text of these six lines.
# Replacing them with a loop hides the component list (`found ()`) and disables that gate.
runtimec_add_component(runtimec_contract contract)
runtimec_add_component(runtimec_binrwir binrwir)
runtimec_add_component(runtimec_vmkv vmkv)
runtimec_add_component(runtimec_binrwir_compiler binrwir_compiler)
runtimec_add_component(runtimec_vmbin vmbin)
runtimec_add_component(runtimec_engine engine)

# `vmkv/rwir_manifest.c` pulls in `specification/*.def` via `#include`, whereas CMake
# only tracks `.c/.h` in the source list. Without explicit registration, `.o` stays up
# to date after generator rewrites and new operators silently become FOREIGN at runtime.
file(GLOB rwir_specification_defs CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/specification/*.def")
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/vmkv/rwir_manifest.c"
    PROPERTIES OBJECT_DEPENDS "${rwir_specification_defs}")

# Handlers in contract and vmbin call the pathspace C binary interface directly (no
# wrapper layer; handles are `void *`), so they link the real library. **Link only the
# frontend `libpathspace`** — pathspace-c / pathspace-durable are selected at runtime
# by DSN scheme. `m`: sqrt/exp/log for scalar.
target_include_directories(runtimec_contract PUBLIC
    "$<BUILD_INTERFACE:${RUNTIMEC_INCLUDE_DIRECTORY}>"
    "$<BUILD_INTERFACE:${PATHSPACE_INCLUDE_DIRECTORY}>")
# contract also links pathlang_xvalue.a for language-layer value constructors
# (pathlangXvalueHeadNew*). Dependency direction remains "language <- runtime",
# without involving pathspace.
target_link_libraries(runtimec_contract PRIVATE
    "${RUNTIMEC_PATHSPACE_LIBRARY_PATH}" "${PATHLANG_XVALUE_LIBRARY_PATH}")
target_link_libraries(runtimec_binrwir PRIVATE runtimec_contract)
target_link_libraries(runtimec_vmkv PRIVATE runtimec_contract runtimec_binrwir)
target_link_libraries(runtimec_binrwir_compiler
    PRIVATE runtimec_contract runtimec_binrwir)
target_link_libraries(runtimec_vmbin PRIVATE runtimec_contract runtimec_binrwir
    "${RUNTIMEC_PATHSPACE_LIBRARY_PATH}" "${PATHLANG_XVALUE_LIBRARY_PATH}" m)
# engine is the composition root: connects to pathspace and lends the handle to contract/pathspace_access.
target_link_libraries(runtimec_engine PRIVATE
    runtimec_contract runtimec_binrwir runtimec_vmkv
    runtimec_binrwir_compiler runtimec_vmbin "${RUNTIMEC_PATHSPACE_LIBRARY_PATH}")
foreach(target_name runtimec_binrwir_compiler runtimec_engine)
    target_compile_definitions(${target_name}
        PRIVATE RUNTIMEC_ENABLE_JIT=$<BOOL:${RUNTIMEC_ENABLE_JIT}>)
endforeach()

# Compile the public binary interface once and share it between static components and the
# scaffold shared library. `extension_interface.c` must be in this OBJECT set: its sole
# callers are outside the `.so`, so an archive link would **drop the whole object**.
add_library(runtimec_interface_objects OBJECT
    interface/public_interface.c engine/extension_interface.c)
runtimec_configure_target(runtimec_interface_objects)
target_compile_definitions(runtimec_interface_objects
    PRIVATE PATHLANG_RUNTIMEC_BUILDING_LIBRARY=1)

add_library(runtimec_interface STATIC $<TARGET_OBJECTS:runtimec_interface_objects>)
runtimec_configure_target(runtimec_interface)
target_link_libraries(runtimec_interface PUBLIC runtimec_contract PRIVATE runtimec_engine)

# Drivers no longer duplicate sources or rely on `RUNTIMEC_TEMP_OUTPUT`: output operators
# register through the extension table via `tools/driver_output.c`, which is observable and
# testable at runtime. The only difference from `.so` is linking `tools/driver_output.c`.
add_executable(kv_runner cli/kv_runner.c tools/driver_output.c)
runtimec_configure_target(kv_runner)
target_link_libraries(kv_runner PRIVATE runtimec_contract runtimec_binrwir
    runtimec_vmbin runtimec_vmkv runtimec_binrwir_compiler runtimec_engine
    "${RUNTIMEC_PATHSPACE_LIBRARY_PATH}" "${CMAKE_DL_LIBS}" m)
set_target_properties(kv_runner PROPERTIES
    BUILD_RPATH "${RUNTIMEC_PATHSPACE_LIBRARY_DIRECTORY}")

# `pathlangrun`: two-phase benchmark driver (`create <src>` creates a vthread, `run <vid>`
# advances it). Shares `tools/driver_output.c` and link flags with `kv_runner`; loads
# layout via dlopen rather than linking it.
add_executable(pathlangrun cli/pathlangrun.c tools/driver_output.c)
runtimec_configure_target(pathlangrun)
target_link_libraries(pathlangrun PRIVATE runtimec_contract runtimec_binrwir
    runtimec_vmbin runtimec_vmkv runtimec_binrwir_compiler runtimec_engine
    "${RUNTIMEC_PATHSPACE_LIBRARY_PATH}" "${CMAKE_DL_LIBS}" m)
set_target_properties(pathlangrun PROPERTIES
    BUILD_RPATH "${RUNTIMEC_PATHSPACE_LIBRARY_DIRECTORY}")

add_library(pathlang_runtimec_scaffold SHARED
    $<TARGET_OBJECTS:runtimec_interface_objects>)
target_include_directories(pathlang_runtimec_scaffold
    PUBLIC "$<BUILD_INTERFACE:${RUNTIMEC_INCLUDE_DIRECTORY}>")
target_link_libraries(pathlang_runtimec_scaffold
    PRIVATE runtimec_interface runtimec_engine)
# **Output artifact name is `libpathlang_runtime.so`** (decision 2026-11-10, TASK-059).
# Keep the internal CMake target name `pathlang_runtimec_scaffold` to minimize churn;
# `OUTPUT_NAME` controls the user-visible artifact name.
set_target_properties(pathlang_runtimec_scaffold PROPERTIES
    C_VISIBILITY_PRESET hidden OUTPUT_NAME pathlang_runtime
    POSITION_INDEPENDENT_CODE ON)

if(APPLE)
    target_link_options(pathlang_runtimec_scaffold PRIVATE
        "LINKER:-exported_symbols_list,${RUNTIMEC_GENERATED_DIRECTORY}/exports.macos")
    set_property(TARGET pathlang_runtimec_scaffold APPEND PROPERTY
        LINK_DEPENDS "${RUNTIMEC_GENERATED_DIRECTORY}/exports.macos")
elseif(WIN32)
    target_sources(pathlang_runtimec_scaffold PRIVATE
        "${RUNTIMEC_GENERATED_DIRECTORY}/exports.def")
elseif(UNIX)
    target_link_options(pathlang_runtimec_scaffold PRIVATE
        "LINKER:--version-script=${RUNTIMEC_GENERATED_DIRECTORY}/exports.map")
    set_property(TARGET pathlang_runtimec_scaffold APPEND PROPERTY
        LINK_DEPENDS "${RUNTIMEC_GENERATED_DIRECTORY}/exports.map")
endif()

foreach(alias_contract "runtimec_contract" "runtimec_binrwir" "runtimec_vmkv"
                       "runtimec_binrwir_compiler" "runtimec_vmbin"
                       "runtimec_engine" "runtimec_interface")
    add_library(pathlang::${alias_contract} ALIAS ${alias_contract})
endforeach()
add_library(pathlang::runtimec_scaffold ALIAS pathlang_runtimec_scaffold)

# **Test files and their registrations belong together**: leaving a `.c` file without
# `add_test` causes `ctest` to report `No tests were found!!!` and **exit 0**, producing
# a false green in CI (TASK-086). `enable_testing()` must be called here so subdirectory
# `add_test` calls are visible to ctest.
enable_testing()
add_subdirectory(tests)
