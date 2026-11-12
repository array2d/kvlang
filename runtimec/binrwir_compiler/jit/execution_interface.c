
#include "binrwir_compiler/jit/execution_interface.h"

runtimec_status_t binrwir_jit_execution_interface(
    binrwir_jit_execution_interface_t *output_interface) {
    if (output_interface != NULL) {
        *output_interface = (binrwir_jit_execution_interface_t){0};
    }
    if (output_interface == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
