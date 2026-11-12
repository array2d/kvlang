#ifndef RUNTIMEC_TOOLS_DRIVER_OUTPUT_H
#define RUNTIMEC_TOOLS_DRIVER_OUTPUT_H

#include <stddef.h>

#include "binrwir/registry.h"

#ifdef __cplusplus
extern "C" {
#endif

const binrwir_registry_entry_t *runtimec_driver_output_entries(
    size_t *output_count);

#ifdef __cplusplus
}
#endif

#endif
