#ifndef RUNTIMEC_VMKV_SOURCE_H
#define RUNTIMEC_VMKV_SOURCE_H

#include "contract/pathspace_access.h"
#include "contract/snapshot.h"
#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vmkv_function_source_t vmkv_function_source_t;

runtimec_status_t vmkv_source_open_function(
    runtimec_pathspace_access_t *pathspace_access,
    runtimec_string_view_t function_path,
    vmkv_function_source_t **output_function,
    runtimec_snapshot_t *output_snapshot,
    runtimec_error_t *error);

runtimec_status_t vmkv_source_close_function(vmkv_function_source_t *function);

#ifdef __cplusplus
}
#endif

#endif
