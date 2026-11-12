#ifndef VMBIN_FRAME_DRIVER_H
#define VMBIN_FRAME_DRIVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "binrwir/execution_exit.h"
#include "contract/error.h"
#include "contract/status.h"
#include "engine/execution_window.h"
#include "engine/session.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FRAME_DRIVER_CALLEE_ENTRY_SOURCE_PC 1U

#define FRAME_DRIVER_MAX_CALLEE_ARGUMENT_ENTRIES 16U

#define FRAME_DRIVER_EFFECTIVE_KEY_CAPACITY 256U

#define FRAME_DRIVER_PATH_CAPACITY 1024U

typedef enum frame_driver_outcome {
    FRAME_DRIVER_OUTCOME_INVALID = 0,
    FRAME_DRIVER_OUTCOME_DONE,
    FRAME_DRIVER_OUTCOME_HANDOFF,
    FRAME_DRIVER_OUTCOME_ERROR,
    FRAME_DRIVER_OUTCOME_EXECUTE_FAILED,
    
    FRAME_DRIVER_OUTCOME_YIELD
} frame_driver_outcome_t;

typedef struct frame_driver_effective_key {
    char frame_path[128];
    uint16_t parameter_slot;
    char key[128];
} frame_driver_effective_key_t;

typedef struct frame_driver {
    
    runtimec_session_t *session;
    frame_driver_effective_key_t effective_keys[FRAME_DRIVER_EFFECTIVE_KEY_CAPACITY];
    size_t effective_key_count;
} frame_driver_t;

runtimec_status_t frame_driver_initialize(frame_driver_t *driver,
                                          runtimec_session_t *session);

frame_driver_outcome_t frame_driver_execute(
    frame_driver_t *driver, const char *function_path, uint32_t source_pc,
    int32_t depth, const char *frame_segment,
    const char *vthread_identifier,
    const runtimec_argument_description_t *arguments, size_t argument_count,
    binrwir_execution_exit_t *output_exit, char *output_message,
    size_t output_message_capacity);

#ifdef __cplusplus
}
#endif

#endif
