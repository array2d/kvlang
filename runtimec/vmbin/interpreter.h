#ifndef VMBIN_INTERPRETER_H
#define VMBIN_INTERPRETER_H

#include "binrwir/execution_exit.h"
#include "binrwir/binvthread_frame.h"
#include "binrwir/execution_plan.h"
#include "binrwir/binfunc.h"
#include "binrwir/registry.h"
#include "contract/diagnostic_band.h"
#include "contract/status.h"
#include "profile.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum vmbin_interpreter_diagnostic {
    VMBIN_INTERPRETER_DIAG_INVALID_REQUEST = UINT32_C(0x4100) + 1U,
    VMBIN_INTERPRETER_DIAG_INVALID_CURSOR = UINT32_C(0x4100) + 2U,
    VMBIN_INTERPRETER_DIAG_INVALID_INSTRUCTION = UINT32_C(0x4100) + 3U,
    VMBIN_INTERPRETER_DIAG_WRONG_ARITY = UINT32_C(0x4100) + 4U,
    VMBIN_INTERPRETER_DIAG_UNDECODABLE_TARGET = UINT32_C(0x4100) + 5U,
    VMBIN_INTERPRETER_DIAG_UNRESOLVED_TARGET = UINT32_C(0x4100) + 6U,
    VMBIN_INTERPRETER_DIAG_VALUE_ACCESS = UINT32_C(0x4100) + 7U,
    VMBIN_INTERPRETER_DIAG_HANDLER_FAILED = UINT32_C(0x4100) + 8U,
    VMBIN_INTERPRETER_DIAG_OUT_OF_MEMORY = UINT32_C(0x4100) + 9U,
    
    VMBIN_INTERPRETER_DIAG_CORRUPT_REGISTRY = UINT32_C(0x4100) + 10U,
    
    VMBIN_INTERPRETER_DIAG_CONTRADICTORY_EXIT = UINT32_C(0x4100) + 11U
} vmbin_interpreter_diagnostic_t;

_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(VMBIN_INTERPRETER_DIAG_CONTRADICTORY_EXIT, RUNTIMEC_DIAGNOSTIC_BAND_INTERPRETER),
               "VMBIN_INTERPRETER_DIAG_CONTRADICTORY_EXIT must stay inside its diagnostic band");

#define VMBIN_OPERATION_IDENTIFIER_NONE BINRWIR_OPERATION_IDENTIFIER_NONE

runtimec_status_t
vmbin_execute(const binrwir_binfunc_t *program,
              const binrwir_execution_plan_t *execution_plan,
              binrwir_binvthread_frame_t *frame,
              const binrwir_registry_t *builtin_registry,
              void *pathspace_handle,
              vmbin_profile_t *profile,
              binrwir_execution_exit_t *output_exit);

#ifdef __cplusplus
}
#endif

#endif
