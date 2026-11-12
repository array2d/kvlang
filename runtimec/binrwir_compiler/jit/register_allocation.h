#ifndef RUNTIMEC_BINRWIR_JIT_REGISTER_ALLOCATION_H
#define RUNTIMEC_BINRWIR_JIT_REGISTER_ALLOCATION_H

#include <stddef.h>
#include <stdint.h>

#include "binrwir/execution_plan.h"
#include "binrwir_compiler/jit/backend.h"
#include "binrwir_compiler/jit/verifier.h"
#include "contract/allocator.h"
#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum binrwir_jit_location_kind {
    BINRWIR_JIT_LOCATION_NONE = 0,
    BINRWIR_JIT_LOCATION_PHYSICAL_REGISTER = 1,
    BINRWIR_JIT_LOCATION_SPILL = 2
} binrwir_jit_location_kind_t;

typedef enum binrwir_jit_location_flags {
    BINRWIR_JIT_LOCATION_FLAG_NONE = 0,
    BINRWIR_JIT_LOCATION_FLAG_READ = 1U << 0,
    BINRWIR_JIT_LOCATION_FLAG_WRITE = 1U << 1,
    BINRWIR_JIT_LOCATION_FLAG_DIRTY = 1U << 2
} binrwir_jit_location_flags_t;

typedef struct binrwir_jit_body_origin {
    uint64_t body_address;
    uint32_t argument_index;
    uint32_t reserved_32_bits;
} binrwir_jit_body_origin_t;

typedef struct binrwir_jit_location_mapping {
    binrwir_jit_body_origin_t body_origin;
    uint32_t location_identifier;
    uint16_t location_kind;
    uint16_t flags;
} binrwir_jit_location_mapping_t;

typedef enum binrwir_jit_write_alias_action {
    BINRWIR_JIT_WRITE_ALIAS_ACTION_INVALID = 0,
    BINRWIR_JIT_WRITE_ALIAS_ACTION_UPDATE_READ_LOCATION = 1,
    BINRWIR_JIT_WRITE_ALIAS_ACTION_INVALIDATE_READ_LOCATION = 2
} binrwir_jit_write_alias_action_t;

typedef struct binrwir_jit_write_alias_mapping {
    uint64_t body_address;
    uint32_t read_argument_index;
    uint32_t write_argument_index;
    uint32_t read_location_identifier;
    uint32_t write_location_identifier;
    uint16_t action;
    uint16_t reserved_16_bits;
} binrwir_jit_write_alias_mapping_t;

typedef struct binrwir_jit_register_allocation
    binrwir_jit_register_allocation_t;

typedef struct binrwir_jit_register_allocation_view {
    const binrwir_jit_location_mapping_t *location_mappings;
    size_t location_mapping_count;
    const binrwir_jit_write_alias_mapping_t *write_alias_mappings;
    size_t write_alias_mapping_count;
} binrwir_jit_register_allocation_view_t;

runtimec_status_t binrwir_jit_register_allocation_create(
    const binrwir_jit_verified_program_t *verified_program,
    const binrwir_execution_plan_t *execution_plan,
    const binrwir_jit_target_t *target,
    const runtimec_allocator_t *allocator,
    binrwir_jit_register_allocation_t **output_allocation,
    binrwir_jit_refusal_t *refusal,
    runtimec_error_t *error);
runtimec_status_t binrwir_jit_register_allocation_get_view(
    const binrwir_jit_register_allocation_t *allocation,
    binrwir_jit_register_allocation_view_t *output_view);
runtimec_status_t binrwir_jit_register_allocation_destroy(
    binrwir_jit_register_allocation_t *allocation);

_Static_assert(sizeof(binrwir_jit_body_origin_t) == 16U,
               "JIT body origins must remain 16 bytes");
_Static_assert(sizeof(binrwir_jit_location_mapping_t) == 24U,
               "JIT location mappings must remain 24 bytes");

#ifdef __cplusplus
}
#endif

#endif
