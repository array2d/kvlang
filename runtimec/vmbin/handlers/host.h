#ifndef VMBIN_HANDLERS_HOST_H
#define VMBIN_HANDLERS_HOST_H

#include <stddef.h>

#include "binrwir/registry.h"

#ifdef __cplusplus
extern "C" {
#endif

const binrwir_registry_entry_t *runtimec_host_handler_entries(
    size_t *output_count);

#ifdef __cplusplus
}
#endif

#endif
