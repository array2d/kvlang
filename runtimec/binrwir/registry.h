#ifndef BINRWIR_REGISTRY_H
#define BINRWIR_REGISTRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "binrwir/execution_exit.h"
#include "binrwir/binvthread_frame.h"
#include "binrwir/instruction.h"
#include "binrwir/operation_identifiers.h"
#include "binrwir/binfunc.h"
#include "contract/error.h"
#include "contract/status.h"
#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef runtimec_status_t
(*binrwir_registry_handler_t)(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit);

typedef enum binrwir_registry_flags {
    BINRWIR_REGISTRY_FLAG_NONE = 0,
    BINRWIR_REGISTRY_FLAG_EFFECT = 1U << 0,
    BINRWIR_REGISTRY_FLAG_MAY_YIELD = 1U << 1
} binrwir_registry_flags_t;

typedef struct binrwir_registry_entry {
    binrwir_operation_identifier_t operation_identifier;
    uint32_t flags;
    binrwir_registry_handler_t handler;
} binrwir_registry_entry_t;

#define BINRWIR_REGISTRY_ENTRY_LIMIT 2048U

#define BINRWIR_REGISTRY_SPECIALIZED_INDEX_LIMIT \
    (BINRWIR_OPERATION_IDENTIFIER_RESERVED_BASE - \
     BINRWIR_SPECIALIZED_BASE)

typedef struct binrwir_registry {
    binrwir_registry_entry_t storage[BINRWIR_REGISTRY_ENTRY_LIMIT];
    size_t entry_count;
    
    uint16_t specialized_index[BINRWIR_REGISTRY_SPECIALIZED_INDEX_LIMIT];
    
    uint16_t base_index[BINRWIR_REGISTRY_ENTRY_LIMIT];
    size_t base_index_count;
    
    size_t specialized_index_count;
    
    bool index_ready;
} binrwir_registry_t;

typedef struct binrwir_registry_module {
    const char *module_name;
    const binrwir_registry_entry_t *entries;
    size_t entry_count;
} binrwir_registry_module_t;

runtimec_status_t binrwir_registry_initialize(
    binrwir_registry_t *registry);
runtimec_status_t binrwir_registry_reset(
    binrwir_registry_t *registry);
runtimec_status_t binrwir_registry_destroy(
    binrwir_registry_t *registry);

runtimec_status_t binrwir_registry_append(
    binrwir_registry_t *registry, const char *module_name,
    const binrwir_registry_entry_t *entries, size_t entry_count,
    runtimec_error_t *error);

runtimec_status_t
binrwir_registry_find(const binrwir_registry_t *registry,
                            binrwir_operation_identifier_t operation_identifier,
                            const binrwir_registry_entry_t **output_entry);

#ifdef __cplusplus
}
#endif

#endif
