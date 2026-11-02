/* vmjit/verifier 的失败封闭骨架。 */

#include "vmjit/verifier.h"

runtimec_status_t vmjit_verify_program(
    const binrwir_program_t *program,
    const vmjit_target_t *target,
    vmjit_verified_program_t *output_verified_program,
    vmjit_refusal_t *refusal,
    runtimec_error_t *error) {
    (void)error;
    if (output_verified_program != NULL) {
        *output_verified_program = (vmjit_verified_program_t){0};
    }
    if (refusal != NULL) {
        *refusal = (vmjit_refusal_t){
            .reason = VMJIT_REFUSAL_INVALID_PROGRAM,
        };
    }
    if (program == NULL || target == NULL || output_verified_program == NULL ||
        refusal == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    refusal->reason = VMJIT_REFUSAL_NONE;
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
