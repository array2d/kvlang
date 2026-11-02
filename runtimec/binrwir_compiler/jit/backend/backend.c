/*
 * 组件：binrwir_compiler/jit/backend/backend
 * 作用：按目标选择 backend；当前显式返回 UNSUPPORTED。未知 processor
 *       不得默认 x86_64。
 * 联系：binrwir_compiler/jit/compiler 调用；x86_64/aarch64 是并列 peer。
 */

#include "binrwir_compiler/jit/backend.h"

#include <stddef.h>

runtimec_status_t binrwir_jit_backend_for_target(
    const binrwir_jit_target_t *target,
    const binrwir_jit_backend_t **output_backend) {
    if (output_backend != NULL) {
        *output_backend = NULL;
    }
    if (target == NULL || output_backend == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_UNSUPPORTED;
}
