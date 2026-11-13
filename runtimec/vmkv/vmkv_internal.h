#ifndef RUNTIMEC_VMKV_INTERNAL_H
#define RUNTIMEC_VMKV_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "contract/allocator.h"
#include "contract/pathspace_access.h"
#include "contract/view.h"
#include "vmkv/source.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMKV_COUNTS_HEADER_SIZE 5U

enum {
    VMKV_DIAG_INVALID_REQUEST = 1,
    VMKV_DIAG_HOST_CAPABILITY_MISSING = 2,
    VMKV_DIAG_HOST_FAILURE = 3,
    VMKV_DIAG_MALFORMED_KEY = 4,
    VMKV_DIAG_MALFORMED_VALUE = 5,
    VMKV_DIAG_MISSING_SIGNATURE_ANCHOR = 6,
    VMKV_DIAG_MISSING_PARAMETER_DEFINITION = 7,
    VMKV_DIAG_MISSING_OPCODE = 8,
    VMKV_DIAG_ARGUMENT_COUNT_MISMATCH = 9,
    VMKV_DIAG_EMPTY_OPCODE = 10,
    VMKV_DIAG_OUT_OF_MEMORY = 11
};

typedef struct vmkv_allocation {
    void *pointer;
    size_t size;
    size_t alignment;
} vmkv_allocation_t;

typedef struct vmkv_param_definition {
    const char *type_name;
    size_t type_name_size;
    bool inplace;
    bool present;
} vmkv_param_definition_t;

typedef struct vmkv_source_slot {
    uint32_t line;
    int32_t column;
    const uint8_t *wire;
    size_t wire_size;
    const uint8_t *body;
    size_t body_size;
    runtimec_string_view_t langtype;
    bool present;
} vmkv_source_slot_t;

struct vmkv_function_source_t {
    runtimec_pathspace_access_t *pathspace_access;
    runtimec_snapshot_t snapshot;
    const char *function_path;
    size_t function_path_size;
    
    uint32_t read_parameter_count;
    uint32_t write_parameter_count;
    bool dynamic_parameters;
    
    vmkv_param_definition_t *read_parameters;
    vmkv_param_definition_t *write_parameters;
    
    vmkv_source_slot_t *slots;
    size_t slot_count;
    uint32_t instruction_line_count;
    vmkv_allocation_t *allocations;
    size_t allocation_count;
    size_t allocation_capacity;
};

void *vmkv_source_allocate(vmkv_function_source_t *function, size_t size,
                           size_t alignment);

bool vmkv_source_split_wire(const uint8_t *wire, size_t wire_size,
                            const uint8_t **output_body,
                            size_t *output_body_size,
                            runtimec_string_view_t *output_langtype);

const vmkv_source_slot_t *vmkv_source_find_slot(
    const vmkv_function_source_t *function, uint32_t line, int32_t column);

#ifdef __cplusplus
}
#endif

#endif
