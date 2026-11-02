/*
 * 组件：binrwir_compiler/jit/backend/aarch64
 * 作用：AArch64 backend 入口；当前显式返回 UNSUPPORTED。它不是 x86 完成后的
 *       占位：必须真实实现同一 register allocation、completed-state commit 与
 *       incomplete-state discard contract，否则不可编译。
 * 联系：与 x86_64 peer；只消费 lower/emit request。
 */

#include "binrwir_compiler/jit/backend.h"

#include <stddef.h>

runtimec_status_t binrwir_jit_aarch64_backend(
    const binrwir_jit_backend_t **output_backend) {
    if (output_backend == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    *output_backend = NULL;
    return RUNTIMEC_STATUS_UNSUPPORTED;
}
