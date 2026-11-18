
set(KVI_BINARY "${CMAKE_SOURCE_DIR}/kvi/target/release/kvi")

add_test(NAME cli
         COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/cli_test.sh"
                 "${KVI_BINARY}"
                 "${CMAKE_CURRENT_BINARY_DIR}/cli_test.shm")
