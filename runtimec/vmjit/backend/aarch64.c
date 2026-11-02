/*
 * 组件：vmjit/backend/aarch64
 * 作用：AArch64 backend 入口；当前显式返回 UNSUPPORTED。它不是 x86 完成后的
 *       占位：必须真实实现同一 backend contract，否则不可编译。
 * 联系：与 x86_64 peer；只消费 lower/emit request。
 */

#include "vmjit/backend.h"

#include <stddef.h>

runtimec_status_t vmjit_aarch64_backend(
    const vmjit_backend_t **output_backend) {
    if (output_backend == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    *output_backend = NULL;
    return RUNTIMEC_STATUS_UNSUPPORTED;
}
