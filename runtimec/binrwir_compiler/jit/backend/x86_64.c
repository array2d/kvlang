/*
 * 组件：binrwir_compiler/jit/backend/x86_64
 * 作用：x86_64 backend 入口；当前显式返回 UNSUPPORTED。它与 AArch64
 *       是 peer 关系，只拥有
 *       目标机制（编码、调用约定、执行既定 register allocation、spill、
 *       completed-state commit、incomplete-state discard、常量池、branch fixup、
 *       代码页切换与 instruction cache 同步），不得自行发明额外缓存位置，
 *       也不得在 guard/error 路径写回 partial state。
 * 联系：binrwir_compiler/jit/compiler 通过 backend contract 选择；不得读 RWIR/kvspace。
 */

#include "binrwir_compiler/jit/backend.h"

#include <stddef.h>

runtimec_status_t binrwir_jit_x86_64_backend(
    const binrwir_jit_backend_t **output_backend) {
    if (output_backend == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    *output_backend = NULL;
    return RUNTIMEC_STATUS_UNSUPPORTED;
}
