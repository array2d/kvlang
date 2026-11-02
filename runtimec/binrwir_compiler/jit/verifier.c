/* binrwir_compiler/jit/verifier 的失败封闭骨架。 */

#include "binrwir_compiler/jit/verifier.h"

runtimec_status_t binrwir_jit_verify_program(
    const binrwir_program_t *program,
    const binrwir_jit_target_t *target,
    binrwir_jit_verified_program_t *output_verified_program,
    binrwir_jit_refusal_t *refusal,
    runtimec_error_t *error) {
    (void)error;
    if (output_verified_program != NULL) {
        *output_verified_program = (binrwir_jit_verified_program_t){0};
    }
    if (refusal != NULL) {
        *refusal = (binrwir_jit_refusal_t){
            .reason = BINRWIR_JIT_REFUSAL_INVALID_PROGRAM,
        };
    }
    if (program == NULL || target == NULL || output_verified_program == NULL ||
        refusal == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    refusal->reason = BINRWIR_JIT_REFUSAL_NONE;
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
