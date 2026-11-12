
#include "registry.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "contract/view.h"

static runtimec_status_t registry_fail(runtimec_error_t *error,
                                       runtimec_status_t status, int64_t code,
                                       const char *message) {
    runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT, code,
                       runtimec_string_view_from_c_string(message),
                       RUNTIMEC_PROGRAM_COUNTER_NONE);
    return status;
}

runtimec_status_t binrwir_registry_initialize(
    binrwir_registry_t *registry) {
    if (registry == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    memset(registry, 0, sizeof(*registry));
    
    registry->index_ready = true;
    return RUNTIMEC_STATUS_SUCCESS;
}

static void binrwir_registry_rebuild_index(binrwir_registry_t *registry) {
    size_t entry_ordinal;
    size_t base_count = 0U;

    memset(registry->specialized_index, 0, sizeof(registry->specialized_index));
    registry->base_index_count = 0U;
    registry->specialized_index_count = 0U;

    for (entry_ordinal = 0U; entry_ordinal < registry->entry_count;
         ++entry_ordinal) {
        const binrwir_operation_identifier_t identifier =
            registry->storage[entry_ordinal].operation_identifier;

        if (identifier >= BINRWIR_SPECIALIZED_BASE &&
            identifier < BINRWIR_OPERATION_IDENTIFIER_RESERVED_BASE) {
            
            const size_t offset =
                (size_t)(identifier - BINRWIR_SPECIALIZED_BASE);
            registry->specialized_index[offset] = (uint16_t)(entry_ordinal + 1U);
            registry->specialized_index_count += 1U;
        } else {
            
            registry->base_index[base_count] = (uint16_t)entry_ordinal;
            base_count += 1U;
        }
    }
    
    for (size_t left = 1U; left < base_count; ++left) {
        const uint16_t carried = registry->base_index[left];
        const binrwir_operation_identifier_t carried_identifier =
            registry->storage[carried].operation_identifier;
        size_t position = left;

        while (position > 0U &&
               registry->storage[registry->base_index[position - 1U]]
                       .operation_identifier > carried_identifier) {
            registry->base_index[position] = registry->base_index[position - 1U];
            position -= 1U;
        }
        registry->base_index[position] = carried;
    }
    registry->base_index_count = base_count;
    registry->index_ready = true;
}

runtimec_status_t binrwir_registry_reset(
    binrwir_registry_t *registry) {
    return binrwir_registry_initialize(registry);
}

runtimec_status_t binrwir_registry_destroy(
    binrwir_registry_t *registry) {
    return binrwir_registry_reset(registry);
}

runtimec_status_t binrwir_registry_append(
    binrwir_registry_t *registry, const char *module_name,
    const binrwir_registry_entry_t *entries, size_t entry_count,
    runtimec_error_t *error) {
    runtimec_error_reset(error);
    if (registry == NULL || module_name == NULL ||
        (entry_count != 0U && entries == NULL)) {
        return registry_fail(error, RUNTIMEC_STATUS_INVALID, 1,
                             "invalid builtin registry append request");
    }
    if (entry_count == 0U) {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    if (entry_count > BINRWIR_REGISTRY_ENTRY_LIMIT - registry->entry_count) {
        return registry_fail(
            error, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY, 2,
            "builtin registry is full; capacity is not extendable");
    }

    for (size_t fresh = 0U; fresh < entry_count; ++fresh) {
        const binrwir_registry_entry_t *entry = &entries[fresh];

        if (entry->handler == NULL) {
            return registry_fail(error, RUNTIMEC_STATUS_INVALID, 3,
                                 "builtin module registers an entry with no handler");
        }
        
        if (entry->operation_identifier == BINRWIR_OPERATION_IDENTIFIER_NONE ||
            entry->operation_identifier == BINRWIR_OPERATION_IDENTIFIER_FOREIGN) {
            return registry_fail(
                error, RUNTIMEC_STATUS_INVALID, 4,
                "builtin module registers a structural marker instead of an "
                "operator identifier");
        }
        for (size_t known = 0U; known < registry->entry_count; ++known) {
            if (registry->storage[known].operation_identifier ==
                entry->operation_identifier) {
                return registry_fail(
                    error, RUNTIMEC_STATUS_INVALID, 5,
                    "builtin identifier is registered twice; later module is "
                    "rejected rather than overwriting the earlier one");
            }
        }
        for (size_t peer = fresh + 1U; peer < entry_count; ++peer) {
            if (entries[peer].operation_identifier == entry->operation_identifier) {
                return registry_fail(
                    error, RUNTIMEC_STATUS_INVALID, 6,
                    "builtin module registers the same identifier twice");
            }
        }
    }

    memcpy(&registry->storage[registry->entry_count], entries,
           entry_count * sizeof(*entries));
    registry->entry_count += entry_count;
    binrwir_registry_rebuild_index(registry);
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t
binrwir_registry_find(const binrwir_registry_t *registry,
                            binrwir_operation_identifier_t operation_identifier,
                            const binrwir_registry_entry_t **output_entry) {
    size_t entry_ordinal = SIZE_MAX;

    if (output_entry != NULL) {
        *output_entry = NULL;
    }
    
    if (registry == NULL || operation_identifier == 0U ||
        operation_identifier == BINRWIR_OPERATION_IDENTIFIER_NONE ||
        output_entry == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    
    if (!registry->index_ready) {
        return RUNTIMEC_STATUS_INVALID;
    }
    
    if (registry->base_index_count + registry->specialized_index_count !=
        registry->entry_count) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (operation_identifier >= BINRWIR_SPECIALIZED_BASE &&
        operation_identifier < BINRWIR_OPERATION_IDENTIFIER_RESERVED_BASE) {
        const size_t offset =
            (size_t)(operation_identifier - BINRWIR_SPECIALIZED_BASE);
        const uint16_t slot = registry->specialized_index[offset];

        if (slot == 0U) {
            return RUNTIMEC_STATUS_UNSUPPORTED;
        }
        entry_ordinal = (size_t)(slot - 1U);
    } else {
        
        size_t low = 0U;
        size_t high = registry->base_index_count;

        while (low < high) {
            const size_t middle = low + (high - low) / 2U;
            const binrwir_operation_identifier_t probe =
                registry->storage[registry->base_index[middle]]
                    .operation_identifier;

            if (probe < operation_identifier) {
                low = middle + 1U;
            } else {
                high = middle;
            }
        }
        if (low >= registry->base_index_count ||
            registry->storage[registry->base_index[low]]
                    .operation_identifier != operation_identifier) {
            return RUNTIMEC_STATUS_UNSUPPORTED;
        }
        entry_ordinal = registry->base_index[low];
    }
    
    if (entry_ordinal >= registry->entry_count ||
        registry->storage[entry_ordinal].handler == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    *output_entry = &registry->storage[entry_ordinal];
    return RUNTIMEC_STATUS_SUCCESS;
}
