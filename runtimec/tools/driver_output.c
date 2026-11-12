
#include "tools/driver_output.h"

#include <stdint.h>

#include "binrwir/execution_exit.h"
#include "binrwir/instruction.h"
#include "vmbin/temp_output.h"

static runtimec_status_t driver_output_emit_1(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return runtimec_temp_output_run(frame, instruction, " ", 0, 1, output_exit);
}

static runtimec_status_t driver_output_emit_2(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return runtimec_temp_output_run(frame, instruction, "", 0, 0, output_exit);
}

static runtimec_status_t driver_output_emit_3(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return runtimec_temp_output_run(frame, instruction, " ", 1, 1, output_exit);
}

static const binrwir_registry_entry_t driver_output_entry_table[] = {
    {.operation_identifier = UINT16_C(0xFF01),
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = driver_output_emit_1},
    {.operation_identifier = UINT16_C(0xFF02),
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = driver_output_emit_2},
    {.operation_identifier = UINT16_C(0xFF03),
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = driver_output_emit_3},
};

const binrwir_registry_entry_t *runtimec_driver_output_entries(
    size_t *output_count) {
    if (output_count != NULL) {
        *output_count = sizeof(driver_output_entry_table) /
                        sizeof(driver_output_entry_table[0]);
    }
    return driver_output_entry_table;
}
