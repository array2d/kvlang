#ifndef BINRWIR_EXECUTION_EXIT_H
#define BINRWIR_EXECUTION_EXIT_H

#include <stdint.h>

#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE UINT32_MAX

typedef enum binrwir_execution_exit_kind {
    BINRWIR_EXECUTION_EXIT_NONE = 0,
    BINRWIR_EXECUTION_EXIT_DONE,
    BINRWIR_EXECUTION_EXIT_CALL,
    BINRWIR_EXECUTION_EXIT_BUILTIN,
    BINRWIR_EXECUTION_EXIT_HANDOFF,
    BINRWIR_EXECUTION_EXIT_YIELD,
    BINRWIR_EXECUTION_EXIT_ERROR
} binrwir_execution_exit_kind_t;

typedef enum binrwir_execution_exit_flags {
    BINRWIR_EXECUTION_EXIT_FLAG_NONE = 0,
    BINRWIR_EXECUTION_EXIT_FLAG_COMPLETED_STATE_COMMITTED = 1U << 0
} binrwir_execution_exit_flags_t;

typedef struct binrwir_execution_exit {
    uint16_t kind; 
    uint16_t flags; 
    runtimec_status_t status; 
    uint32_t logical_program_counter;
    uint32_t resume_logical_program_counter;
    uint32_t detail_code;       
    uint32_t target_identifier;
} binrwir_execution_exit_t;

_Static_assert(sizeof(binrwir_execution_exit_t) == 24U,
               "binrwir structured exits must contain only control state");

#ifdef __cplusplus
}
#endif

#endif
