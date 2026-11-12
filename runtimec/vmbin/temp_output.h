#ifndef VMBIN_TEMP_OUTPUT_H
#define VMBIN_TEMP_OUTPUT_H

#include <stdint.h>

#include "contract/status.h"
#include "binrwir/binvthread_frame.h"
#include "binrwir/instruction.h"
#include "binrwir/registry.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RUNTIMEC_TEMP_OUTPUT_OPERATION_IDENTIFIER_REMOVED UINT16_C(0xFF01)
#define RUNTIMEC_TEMP_PRINT_OPERATION_IDENTIFIER UINT16_C(0xFF02)
#define RUNTIMEC_TEMP_CERR_OPERATION_IDENTIFIER UINT16_C(0xFF03)

const binrwir_registry_entry_t *runtimec_temp_output_entries(size_t *output_count);

runtimec_status_t runtimec_temp_output_run(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, const char *separator,
    int use_stderr, int trailing_newline,
    binrwir_execution_exit_t *output_exit);

#ifdef __cplusplus
}
#endif

#endif
