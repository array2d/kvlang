#ifndef RUNTIMEC_ENGINE_EXECUTION_WINDOW_H
#define RUNTIMEC_ENGINE_EXECUTION_WINDOW_H

#include <stddef.h>
#include <stdint.h>
#include "contract/status.h"
#include "contract/diagnostic_band.h"
#include "contract/view.h"
#include "engine/session.h"
#include "binrwir/execution_exit.h"
#include "binrwir/binfunc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum runtimec_execution_window_diagnostic_t {
    RUNTIMEC_EXECUTION_WINDOW_DIAG_INVALID_REQUEST = UINT32_C(0x4000) + 1U,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_MISSING_HOST_CAPABILITY = UINT32_C(0x4000) + 2U,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_SNAPSHOT_FAILED = UINT32_C(0x4000) + 3U,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_REPOSITORY_FAILED = UINT32_C(0x4000) + 4U,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_ENCODE_FAILED = UINT32_C(0x4000) + 5U,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_COMPILE_FAILED = UINT32_C(0x4000) + 6U,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_COMPILATION_INVALID = UINT32_C(0x4000) + 7U,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_COUNT = UINT32_C(0x4000) + 8U,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_UNRESOLVED = UINT32_C(0x4000) + 9U,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_BINDING_FAILED = UINT32_C(0x4000) + 10U,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_FRAME_FAILED = UINT32_C(0x4000) + 11U,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_PROGRAM_COUNTER_NOT_FOUND = UINT32_C(0x4000) + 12U,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_FRAME_ENSURE_FAILED = UINT32_C(0x4000) + 13U,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_EXECUTION_FAILED = UINT32_C(0x4000) + 14U,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_CHECKPOINT_FAILED = UINT32_C(0x4000) + 15U,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_LOCAL_FAILED = UINT32_C(0x4000) + 16U,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_OUT_OF_MEMORY = UINT32_C(0x4000) + 17U,
    
    RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_KEY_INVALID = UINT32_C(0x4000) + 18U,
    
    RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_LITERAL_INVALID = UINT32_C(0x4000) + 19U,
    
    RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_MATERIALIZE_FAILED = UINT32_C(0x4000) + 20U,
    
    RUNTIMEC_EXECUTION_WINDOW_DIAG_SPECIALIZE_FAILED = UINT32_C(0x4000) + 21U,
    
    RUNTIMEC_EXECUTION_WINDOW_DIAG_HANDOFF_UNCLAIMED = UINT32_C(0x4000) + 22U
} runtimec_execution_window_diagnostic_t;

_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(
                   RUNTIMEC_EXECUTION_WINDOW_DIAG_SPECIALIZE_FAILED,
                   RUNTIMEC_DIAGNOSTIC_BAND_EXECUTION_WINDOW),
               "window diagnostic must stay inside its band");
_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(
                   RUNTIMEC_EXECUTION_WINDOW_DIAG_HANDOFF_UNCLAIMED,
                   RUNTIMEC_DIAGNOSTIC_BAND_EXECUTION_WINDOW),
               "handoff-unclaimed diagnostic must stay inside its band");

typedef enum runtimec_argument_source {
    RUNTIMEC_ARGUMENT_SOURCE_INVALID = 0,
    
    RUNTIMEC_ARGUMENT_SOURCE_EXISTING_KEY = 1,
    
    RUNTIMEC_ARGUMENT_SOURCE_LITERAL = 2,
    
    RUNTIMEC_ARGUMENT_SOURCE_WRITE_TARGET = 3
} runtimec_argument_source_t;

typedef struct runtimec_argument_description {
    runtimec_argument_source_t source;
    uint32_t reserved_32_bits;
    runtimec_string_view_t key;
    runtimec_bytes_view_t literal_bytes;
    runtimec_string_view_t literal_langtype;
} runtimec_argument_description_t;

typedef struct runtimec_execution_window_request_t {
    
    runtimec_string_view_t function_path;
    
    runtimec_string_view_t vthread_identifier;
    
    runtimec_string_view_t frame_path;
    int32_t stack_identifier;
    
    uint32_t source_logical_program_counter;
    
    const runtimec_argument_description_t *arguments;
    size_t argument_count;
} runtimec_execution_window_request_t;

typedef struct runtimec_execution_window_result_t {
    runtimec_status_t status;
    binrwir_execution_exit_t execution_exit;
    runtimec_string_view_t return_value;
    
    const binrwir_binfunc_t *program;
} runtimec_execution_window_result_t;

runtimec_status_t runtimec_execution_window_execute(
    runtimec_session_t *session,
    const runtimec_execution_window_request_t *request,
    runtimec_execution_window_result_t *output_result,
    runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
