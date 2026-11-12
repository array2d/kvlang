#ifndef RUNTIMEC_VMKV_LOCAL_TYPES_H
#define RUNTIMEC_VMKV_LOCAL_TYPES_H

#include <stdbool.h>
#include <stddef.h>

#include "contract/allocator.h"
#include "contract/status.h"
#include "contract/view.h"
#include "vmkv/source.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vmkv_local_types vmkv_local_types_t;

runtimec_status_t vmkv_local_types_collect(
    vmkv_function_source_t *function, runtimec_allocator_t allocator,
    vmkv_local_types_t **output_local_types, runtimec_error_t *error);

runtimec_status_t vmkv_local_types_destroy(vmkv_local_types_t *local_types,
                                           runtimec_allocator_t allocator);

bool vmkv_local_types_find(const vmkv_local_types_t *local_types,
                           runtimec_string_view_t name,
                           runtimec_string_view_t *output_langtype);

bool vmkv_local_types_find(const vmkv_local_types_t *local_types,
                           runtimec_string_view_t name,
                           runtimec_string_view_t *output_langtype);

#ifdef __cplusplus
}
#endif

#endif
