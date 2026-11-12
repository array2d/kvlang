#ifndef VMBIN_HANDLERS_SCATTER_H
#define VMBIN_HANDLERS_SCATTER_H

#include <stddef.h>

#include "binrwir/registry.h"

#ifdef __cplusplus
extern "C" {
#endif

const binrwir_registry_entry_t *runtimec_scatter_handler_entries(
    size_t *output_count);

#ifdef __cplusplus
}
#endif

#endif
