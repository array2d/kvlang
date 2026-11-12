#ifndef RUNTIMEC_BINRWIR_JIT_EXECUTION_INTERFACE_H
#define RUNTIMEC_BINRWIR_JIT_EXECUTION_INTERFACE_H

#include <stdint.h>

#include "contract/status.h"
#include "binrwir/execution_exit.h"
#include "binrwir/binvthread_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BINRWIR_JIT_EXECUTION_BINARY_INTERFACE_MAJOR_VERSION 0u
#define BINRWIR_JIT_EXECUTION_BINARY_INTERFACE_MINOR_VERSION 3u

typedef runtimec_status_t (*binrwir_jit_machine_code_function_t)(
    void *execution_state,
    binrwir_binvthread_frame_t *frame,
    binrwir_execution_exit_t *exit);

typedef struct binrwir_jit_execution_interface_t {
    uint16_t binary_interface_major_version;
    uint16_t binary_interface_minor_version;
    uint16_t value_body_address_size_bytes;
    uint16_t argument_index_size_bytes;
    uint32_t frame_alignment_bytes;
    uint32_t safepoint_contract_revision;
} binrwir_jit_execution_interface_t;

runtimec_status_t binrwir_jit_execution_interface(
    binrwir_jit_execution_interface_t *output_interface);

#ifdef __cplusplus
}
#endif

#endif
