
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
