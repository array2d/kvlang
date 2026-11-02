/*
 * 组件：vmjit/backend/x86_64
 * 作用：x86_64 backend 入口；当前显式返回 FALLBACK，route
 *       回退 vmbin。它与 AArch64 是 peer 关系，只拥有
 *       目标机制（编码、调用约定、寄存器、spill、常量池、branch fixup、
 *       代码页切换与 instruction cache 同步）。
 * 联系：vmjit/compiler 通过 backend contract 选择；不得读 RWIR/kvspace。
 */

#include "vmjit/backend.h"

#include <stddef.h>

runtimec_status_t vmjit_x86_64_backend(
    const vmjit_backend_t **output_backend) {
    if (output_backend == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    *output_backend = NULL;
    return RUNTIMEC_STATUS_FALLBACK;
}
