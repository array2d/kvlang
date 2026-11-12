
#include "binrwir_compiler/jit/executor.h"

runtimec_status_t binrwir_jit_execute(
    const binrwir_jit_code_t *code,
    binrwir_binvthread_frame_t *frame,
    binrwir_execution_exit_t *output_exit,
    runtimec_error_t *error) {
    (void)error;
    if (output_exit != NULL) {
        *output_exit = (binrwir_execution_exit_t){0};
        output_exit->kind = BINRWIR_EXECUTION_EXIT_ERROR;
        output_exit->status = RUNTIMEC_STATUS_INVALID;
    }
    if (code == NULL || frame == NULL || output_exit == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    output_exit->status = RUNTIMEC_STATUS_NOT_IMPLEMENTED;
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
