#ifndef VMBIN_PROGRAM_H
#define VMBIN_PROGRAM_H

/*
 * 组件：vmbin/program
 * 作用：定义不可变 BinRWIR Program、构造器和只读视图。
 * 联系：vmkv 构造 Program；interpreter 与 vmjit 只读消费。
 * 约束：可变绑定、统计、缓存和机器码不得进入 Program。
 */

#include <stddef.h>
#include <stdint.h>

#include "contract/allocator.h"
#include "contract/status.h"
#include "vmbin/instruction.h"
#include "vmbin/reference.h"
#include "vmbin/signature.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMBIN_IDENTIFIER_NONE UINT32_MAX

typedef enum vmbin_program_flags {
    VMBIN_PROGRAM_FLAG_NONE = 0,
    VMBIN_PROGRAM_FLAG_TYPED = 1U << 0,
    VMBIN_PROGRAM_FLAG_HAS_CONTROL_FLOW = 1U << 1,
    VMBIN_PROGRAM_FLAG_HAS_EFFECTS = 1U << 2
} vmbin_program_flags_t;

typedef enum vmbin_register_derived_access {
    VMBIN_REGISTER_DERIVED_ACCESS_NONE = 0,
    VMBIN_REGISTER_DERIVED_ACCESS_READ = 1U << 0,
    VMBIN_REGISTER_DERIVED_ACCESS_WRITE = 1U << 1
} vmbin_register_derived_access_t;

typedef enum vmbin_register_flags {
    VMBIN_REGISTER_FLAG_NONE = 0,
    VMBIN_REGISTER_FLAG_PARAMETER = 1U << 0,
    VMBIN_REGISTER_FLAG_POINTER = 1U << 1,
    VMBIN_REGISTER_FLAG_MEMBER = 1U << 2,
    VMBIN_REGISTER_FLAG_PINNED = 1U << 3
} vmbin_register_flags_t;

typedef enum vmbin_literal_flags {
    VMBIN_LITERAL_FLAG_NONE = 0,
    VMBIN_LITERAL_FLAG_NULL = 1U << 0,
    VMBIN_LITERAL_FLAG_RELOCATABLE = 1U << 1
} vmbin_literal_flags_t;

typedef enum vmbin_callsite_flags {
    VMBIN_CALLSITE_FLAG_NONE = 0,
    VMBIN_CALLSITE_FLAG_TAIL = 1U << 0,
    VMBIN_CALLSITE_FLAG_MAY_YIELD = 1U << 1
} vmbin_callsite_flags_t;

typedef enum vmbin_call_alias_mapping_flags {
    VMBIN_CALL_ALIAS_MAPPING_FLAG_NONE = 0,
    VMBIN_CALL_ALIAS_MAPPING_FLAG_PROJECTED = 1U << 0,
    VMBIN_CALL_ALIAS_MAPPING_FLAG_INDIRECT = 1U << 1
} vmbin_call_alias_mapping_flags_t;

typedef struct vmbin_program_identity {
    uint64_t snapshot_generation;
    uint64_t program_generation;
    uint32_t schema_version;
    uint32_t rwir_manifest_version;
    uint32_t binrwir_binary_interface_generation;
    uint32_t compiler_revision;
} vmbin_program_identity_t;

typedef struct vmbin_register_descriptor {
    uint32_t symbol_identifier;
    uint32_t type_identifier;
    uint32_t auxiliary;
    uint16_t flags;
    /* Cache only: validators derive and check this against the signature. */
    uint8_t derived_access;
    uint8_t reserved_byte;
} vmbin_register_descriptor_t;

typedef struct vmbin_literal_descriptor {
    uint32_t byte_offset;
    uint32_t byte_size;
    uint32_t type_identifier;
    uint16_t flags;
    uint16_t alignment;
} vmbin_literal_descriptor_t;

typedef struct vmbin_symbol_descriptor {
    uint32_t byte_offset;
    uint32_t byte_size;
} vmbin_symbol_descriptor_t;

typedef struct vmbin_callsite {
    uint32_t callee_symbol_identifier;
    uint32_t binding_offset;
    uint32_t alias_mapping_offset;
    uint8_t read_count;
    uint8_t write_count;
    uint8_t alias_mapping_count;
    uint8_t flags;
} vmbin_callsite_t;

typedef struct vmbin_call_alias_mapping {
    uint16_t read_argument;
    uint16_t write_argument;
    uint16_t alias_rule_ordinal;
    uint16_t flags;
} vmbin_call_alias_mapping_t;

typedef struct vmbin_callsite_build_request {
    uint32_t callee_symbol_identifier;
    uint8_t flags;
    uint8_t reserved_bytes[3];
    vmbin_reference_span_t read_bindings;
    vmbin_reference_span_t write_bindings;
    const vmbin_call_alias_mapping_t *alias_mappings;
    size_t alias_mapping_count;
} vmbin_callsite_build_request_t;

typedef struct vmbin_program vmbin_program_t;
typedef struct vmbin_program_builder vmbin_program_builder_t;

typedef struct vmbin_program_view {
    vmbin_program_identity_t identity;
    vmbin_signature_view_t signature;
    const vmbin_instruction_t *instructions;
    size_t instruction_count;
    const uint32_t *source_logical_program_counters;
    const vmbin_reference_t *references;
    size_t reference_count;
    const vmbin_register_descriptor_t *registers;
    size_t register_count;
    const vmbin_literal_descriptor_t *literals;
    size_t literal_count;
    const vmbin_symbol_descriptor_t *symbols;
    size_t symbol_count;
    const vmbin_callsite_t *callsites;
    size_t callsite_count;
    const vmbin_call_alias_mapping_t *alias_mappings;
    size_t alias_mapping_count;
    const uint8_t *literal_bytes;
    size_t literal_byte_count;
    const uint8_t *symbol_bytes;
    size_t symbol_byte_count;
    uint32_t flags;
    uint32_t reserved_32_bits;
} vmbin_program_view_t;

runtimec_status_t vmbin_program_get_identity(
    const vmbin_program_t *program, vmbin_program_identity_t *output_identity);
runtimec_status_t vmbin_program_validate(const vmbin_program_t *program);
runtimec_status_t vmbin_program_get_view(const vmbin_program_t *program,
                                         vmbin_program_view_t *output_view);
runtimec_status_t vmbin_program_get_instruction(
    const vmbin_program_t *program, uint32_t instruction_identifier,
    vmbin_instruction_view_t *output_instruction);
runtimec_status_t vmbin_program_destroy(vmbin_program_t *program);

runtimec_status_t vmbin_program_builder_create(
    const runtimec_allocator_t *allocator,
    const vmbin_program_identity_t *identity,
    vmbin_program_builder_t **output_builder);
runtimec_status_t vmbin_program_builder_reset(vmbin_program_builder_t *builder);
runtimec_status_t vmbin_program_builder_destroy(
    vmbin_program_builder_t *builder);

runtimec_status_t vmbin_program_builder_add_parameter(
    vmbin_program_builder_t *builder, vmbin_parameter_direction_t direction,
    uint32_t register_identifier, uint16_t *output_parameter_ordinal);
runtimec_status_t vmbin_program_builder_add_alias_rule(
    vmbin_program_builder_t *builder, const vmbin_alias_rule_t *rule,
    uint32_t *output_alias_rule_identifier);
runtimec_status_t vmbin_program_builder_add_register(
    vmbin_program_builder_t *builder,
    const vmbin_register_descriptor_t *register_descriptor,
    uint32_t *output_register_identifier);
runtimec_status_t vmbin_program_builder_add_symbol(
    vmbin_program_builder_t *builder, const uint8_t *bytes, size_t byte_count,
    uint32_t *output_symbol_identifier);
runtimec_status_t vmbin_program_builder_add_literal(
    vmbin_program_builder_t *builder,
    const vmbin_literal_descriptor_t *literal_descriptor,
    const uint8_t *bytes, size_t byte_count,
    uint32_t *output_literal_identifier);
runtimec_status_t vmbin_program_builder_add_callsite(
    vmbin_program_builder_t *builder,
    const vmbin_callsite_build_request_t *request,
    uint32_t *output_callsite_identifier);
runtimec_status_t vmbin_program_builder_add_instruction(
    vmbin_program_builder_t *builder,
    const vmbin_instruction_build_request_t *request,
    uint32_t *output_instruction_identifier);
runtimec_status_t
vmbin_program_builder_finalize(vmbin_program_builder_t *builder,
                               vmbin_program_t **output_program);

_Static_assert(sizeof(vmbin_register_descriptor_t) == 16U,
               "vmbin register descriptions must remain 16 bytes");
_Static_assert(sizeof(vmbin_program_identity_t) == 32U,
               "vmbin program identity must remain 32 bytes");
_Static_assert(sizeof(vmbin_literal_descriptor_t) == 16U,
               "vmbin literal descriptions must remain 16 bytes");
_Static_assert(sizeof(vmbin_callsite_t) == 16U,
               "vmbin callsites must remain 16 bytes");
_Static_assert(sizeof(vmbin_call_alias_mapping_t) == 8U,
               "vmbin call alias mappings must remain 8 bytes");

#ifdef __cplusplus
}
#endif

#endif
