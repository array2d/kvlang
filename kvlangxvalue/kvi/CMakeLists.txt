find_program(CARGO_EXECUTABLE cargo)
if(NOT CARGO_EXECUTABLE)
    message(FATAL_ERROR "kvi 用 Rust 实现：找不到 cargo。")
endif()

set(KVI_BINARY "${CMAKE_CURRENT_SOURCE_DIR}/target/release/kvi")
set(KVI_XVALUE_DIRECTORY "$<TARGET_FILE_DIR:kvlang_xvalue>")

add_custom_target(kvi ALL
    COMMAND ${CMAKE_COMMAND} -E env
            "RUSTFLAGS=-L native=${KVI_XVALUE_DIRECTORY} -L native=${KVLANG_KVSPACE_LIB_DIRECTORY} -C link-arg=-Wl,-rpath,${KVI_XVALUE_DIRECTORY} -C link-arg=-Wl,-rpath,${KVLANG_KVSPACE_LIB_DIRECTORY}"
            "${CARGO_EXECUTABLE}" build --release
            --manifest-path "${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml"
    BYPRODUCTS "${KVI_BINARY}"
    DEPENDS kvlang_xvalue
    COMMENT "构建 kvi（Rust）")
