# 组件：test —— 语言层的契约测试。
#
# 本目录只放 .sh：测试通过 kvlangxvalue-cli 走真实命令行，不写 C。C 代码是产品，
# 测试脚本是契约——两者放同一目录就会有人开始「为了让测试通过而写 C」。

# CLI 是语言层的对外窗口：它读写的必须是「高级类型值」，不是裸字节。
set(KVI_BINARY "${CMAKE_SOURCE_DIR}/kvi/target/release/kvi")

add_test(NAME cli
         COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/cli_test.sh"
                 "${KVI_BINARY}"
                 "${CMAKE_CURRENT_BINARY_DIR}/cli_test.shm")
