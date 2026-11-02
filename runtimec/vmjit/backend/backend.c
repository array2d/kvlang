/*
 * 组件：vmjit/backend/backend
 * 作用：按目标选择 backend；当前显式返回 FALLBACK。未知 processor
 *       不得默认 x86_64。
 * 联系：vmjit/compiler 调用；x86_64/aarch64 是并列 peer。
 */

#include "vmjit/backend.h"

#include <stddef.h>

runtimec_status_t vmjit_backend_for_target(
    const vmjit_target_t *target,
    const vmjit_backend_t **output_backend) {
    if (output_backend != NULL) {
        *output_backend = NULL;
    }
    if (target == NULL || output_backend == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_FALLBACK;
}
