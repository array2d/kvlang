
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
