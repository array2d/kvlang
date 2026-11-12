#ifndef VMBIN_HANDLERS_CONTAINER_H
#define VMBIN_HANDLERS_CONTAINER_H

#include <stddef.h>
#include <stdint.h>

#include "contract/status.h"
#include "contract/diagnostic_band.h"
#include "contract/view.h"
#include "vmbin/handlers/handler_call.h"

#include "binrwir/registry.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum runtimec_container_diagnostic {
    RUNTIMEC_CONTAINER_DIAG_NONE = 0,
    
    RUNTIMEC_CONTAINER_DIAG_NEW_BODY_EXCEEDS_BUFFER = 0x4301,
    
    RUNTIMEC_CONTAINER_DIAG_UNPARSEABLE_LANGTYPE = 0x4302,
    
    RUNTIMEC_CONTAINER_DIAG_INDEX_OUT_OF_RANGE = 0x4303,
    
    RUNTIMEC_CONTAINER_DIAG_VALUE_WIDTH_MISMATCH = 0x4304,
    
    RUNTIMEC_CONTAINER_DIAG_SCATTER_HAS_HOLE = 0x4306,
    
    RUNTIMEC_CONTAINER_DIAG_ELEMENT_COUNT_MISMATCH = 0x4307,
    
    RUNTIMEC_CONTAINER_DIAG_RESHAPE_SIZE_MISMATCH = 0x4305,
    
    RUNTIMEC_CONTAINER_DIAG_TARGET_KEY_UNRESOLVED = 0x4308
} runtimec_container_diagnostic_t;

_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(RUNTIMEC_CONTAINER_DIAG_RESHAPE_SIZE_MISMATCH, RUNTIMEC_DIAGNOSTIC_BAND_CONTAINER),
               "RUNTIMEC_CONTAINER_DIAG_RESHAPE_SIZE_MISMATCH must stay inside its diagnostic band");
_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(RUNTIMEC_CONTAINER_DIAG_TARGET_KEY_UNRESOLVED, RUNTIMEC_DIAGNOSTIC_BAND_CONTAINER),
               "RUNTIMEC_CONTAINER_DIAG_TARGET_KEY_UNRESOLVED must stay inside its diagnostic band");

typedef struct runtimec_container_langtype {
    
    runtimec_string_view_t base;
    uint32_t element_size;
    int32_t dimension_count;
    
    int64_t dimensions[VMBIN_HANDLER_MAX_INDEX_COUNT];
} runtimec_container_langtype_t;

runtimec_status_t runtimec_container_langtype_parse(
    runtimec_string_view_t langtype, runtimec_container_langtype_t *output);

int64_t runtimec_container_shape_numel(runtimec_string_view_t langtype);

bool runtimec_container_open_marker(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, char *buffer,
    size_t capacity, binrwir_execution_exit_t *output_exit);

const binrwir_registry_entry_t *runtimec_container_handler_entries(
    size_t *output_count);

#ifdef __cplusplus
}
#endif

#endif
