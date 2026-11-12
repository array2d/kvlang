#ifndef RUNTIMEC_BINRWIR_JIT_EXECUTOR_H
#define RUNTIMEC_BINRWIR_JIT_EXECUTOR_H

#include "binrwir/execution_exit.h"
#include "binrwir/binvthread_frame.h"
#include "binrwir_compiler/jit/code.h"
#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

runtimec_status_t binrwir_jit_execute(
    const binrwir_jit_code_t *code,
    binrwir_binvthread_frame_t *frame,
    binrwir_execution_exit_t *output_exit,
    runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
