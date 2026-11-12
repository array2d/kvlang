#ifndef BINRWIR_FORMAT_H
#define BINRWIR_FORMAT_H

#include <stdbool.h>

#include "binrwir/binfunc.h"
#include "contract/status.h"
#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*binrwir_operation_name_function)(
    binrwir_operation_identifier_t operation_identifier,
    runtimec_string_view_t *output_name, void *context);

typedef runtimec_string_view_t (*binrwir_format_sink_function)(
    void *context, runtimec_string_view_t chunk);

runtimec_status_t binrwir_binfunc_format(
    const binrwir_binfunc_t *program, runtimec_string_view_t function_name,
    binrwir_operation_name_function resolve_operation_name,
    void *resolve_context, binrwir_format_sink_function sink,
    void *sink_context);

typedef struct binrwir_format_buffer_sink_context {
    runtimec_allocator_t allocator;
    runtimec_bytes_buffer_t *buffer;
} binrwir_format_buffer_sink_context_t;

runtimec_string_view_t binrwir_format_buffer_sink(void *context,
                                                  runtimec_string_view_t chunk);

#ifdef __cplusplus
}
#endif

#endif
