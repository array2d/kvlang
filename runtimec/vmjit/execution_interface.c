/* vmjit/execution_interface 的失败封闭骨架。 */

#include "vmjit/execution_interface.h"

runtimec_status_t vmjit_execution_interface(
    vmjit_execution_interface_t *output_interface) {
    if (output_interface != NULL) {
        *output_interface = (vmjit_execution_interface_t){0};
    }
    if (output_interface == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
