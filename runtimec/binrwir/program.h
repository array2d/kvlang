#ifndef BINRWIR_PROGRAM_H
#define BINRWIR_PROGRAM_H

/*
 * 组件：binrwir/program
 * 作用：定义与 RWIR 语义等价的 canonical、不可变 BinRWIR Program。
 * 联系：vmkv 构造 Program；Compiler、vmbin 与 vmjit 只读消费同一 Program。
 * 约束：类型特化、kvspace 寻址、融合、虚拟寄存器、控制流派生计划、
 *       可变绑定、机器码、缓存和统计均不得进入 Program。
 */

#include <stddef.h>
#include <stdint.h>

#include "contract/allocator.h"
#include "contract/status.h"
#include "binrwir/instruction.h"
#include "binrwir/reference.h"
#include "binrwir/signature.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BINRWIR_IDENTIFIER_NONE UINT32_MAX

typedef enum binrwir_register_flags {
    BINRWIR_REGISTER_FLAG_NONE = 0,
    BINRWIR_REGISTER_FLAG_PARAMETER = 1U << 0,
    BINRWIR_REGISTER_FLAG_POINTER = 1U << 1,
    BINRWIR_REGISTER_FLAG_MEMBER = 1U << 2
} binrwir_register_flags_t;

typedef enum binrwir_literal_flags {
    BINRWIR_LITERAL_FLAG_NONE = 0,
    BINRWIR_LITERAL_FLAG_NULL = 1U << 0,
    BINRWIR_LITERAL_FLAG_RELOCATABLE = 1U << 1
} binrwir_literal_flags_t;

typedef enum binrwir_callsite_flags {
    BINRWIR_CALLSITE_FLAG_NONE = 0,
    BINRWIR_CALLSITE_FLAG_TAIL = 1U << 0,
    BINRWIR_CALLSITE_FLAG_MAY_YIELD = 1U << 1
} binrwir_callsite_flags_t;

typedef enum binrwir_call_alias_mapping_flags {
    BINRWIR_CALL_ALIAS_MAPPING_FLAG_NONE = 0,
    BINRWIR_CALL_ALIAS_MAPPING_FLAG_PROJECTED = 1U << 0,
    BINRWIR_CALL_ALIAS_MAPPING_FLAG_INDIRECT = 1U << 1
} binrwir_call_alias_mapping_flags_t;

typedef struct binrwir_program_identity {
    uint64_t snapshot_generation;
    uint64_t program_generation;
    uint32_t schema_version;
    uint32_t rwir_manifest_version;
    uint32_t binrwir_binary_interface_generation;
    uint32_t canonical_encoding_revision;
} binrwir_program_identity_t;

typedef struct binrwir_register_descriptor {
    uint32_t symbol_identifier;
    uint32_t type_identifier;
    uint32_t auxiliary;
    uint16_t flags;
    uint16_t reserved_16_bits;
} binrwir_register_descriptor_t;

typedef struct binrwir_literal_descriptor {
    uint32_t byte_offset;
    uint32_t byte_size;
    uint32_t type_identifier;
    uint16_t flags;
    uint16_t alignment;
} binrwir_literal_descriptor_t;

typedef struct binrwir_symbol_descriptor {
    uint32_t byte_offset;
    uint32_t byte_size;
} binrwir_symbol_descriptor_t;

typedef struct binrwir_callsite {
    uint32_t callee_symbol_identifier;
    uint32_t binding_offset;
    uint32_t alias_mapping_offset;
    uint8_t read_count;
    uint8_t write_count;
    uint8_t alias_mapping_count;
    uint8_t flags;
} binrwir_callsite_t;

typedef struct binrwir_call_alias_mapping {
    uint16_t read_argument;
    uint16_t write_argument;
    uint16_t alias_rule_ordinal;
    uint16_t flags;
} binrwir_call_alias_mapping_t;

typedef struct binrwir_callsite_build_request {
    uint32_t callee_symbol_identifier;
    uint8_t flags;
    uint8_t reserved_bytes[3];
    binrwir_reference_span_t read_bindings;
    binrwir_reference_span_t write_bindings;
    const binrwir_call_alias_mapping_t *alias_mappings;
    size_t alias_mapping_count;
} binrwir_callsite_build_request_t;

typedef struct binrwir_program binrwir_program_t;
typedef struct binrwir_program_builder binrwir_program_builder_t;

typedef struct binrwir_program_view {
    binrwir_program_identity_t identity;
    binrwir_signature_view_t signature;
    const binrwir_instruction_t *instructions;
    size_t instruction_count;
    const uint32_t *source_logical_program_counters;
    const binrwir_reference_t *references;
    size_t reference_count;
    const binrwir_register_descriptor_t *registers;
    size_t register_count;
    const binrwir_literal_descriptor_t *literals;
    size_t literal_count;
    const binrwir_symbol_descriptor_t *symbols;
    size_t symbol_count;
    const binrwir_callsite_t *callsites;
    size_t callsite_count;
    const binrwir_call_alias_mapping_t *alias_mappings;
    size_t alias_mapping_count;
    const uint8_t *literal_bytes;
    size_t literal_byte_count;
    const uint8_t *symbol_bytes;
    size_t symbol_byte_count;
} binrwir_program_view_t;

runtimec_status_t binrwir_program_get_identity(
    const binrwir_program_t *program, binrwir_program_identity_t *output_identity);
runtimec_status_t binrwir_program_validate(const binrwir_program_t *program);
runtimec_status_t binrwir_program_get_view(const binrwir_program_t *program,
                                         binrwir_program_view_t *output_view);
runtimec_status_t binrwir_program_get_instruction(
    const binrwir_program_t *program, uint32_t instruction_identifier,
    binrwir_instruction_view_t *output_instruction);
runtimec_status_t binrwir_program_destroy(binrwir_program_t *program);

runtimec_status_t binrwir_program_builder_create(
    const runtimec_allocator_t *allocator,
    const binrwir_program_identity_t *identity,
    binrwir_program_builder_t **output_builder);
runtimec_status_t binrwir_program_builder_reset(binrwir_program_builder_t *builder);
runtimec_status_t binrwir_program_builder_destroy(
    binrwir_program_builder_t *builder);

runtimec_status_t binrwir_program_builder_add_parameter(
    binrwir_program_builder_t *builder, binrwir_parameter_direction_t direction,
    uint32_t register_identifier, uint16_t *output_parameter_ordinal);
runtimec_status_t binrwir_program_builder_add_alias_rule(
    binrwir_program_builder_t *builder, const binrwir_alias_rule_t *rule,
    uint32_t *output_alias_rule_identifier);
runtimec_status_t binrwir_program_builder_add_register(
    binrwir_program_builder_t *builder,
    const binrwir_register_descriptor_t *register_descriptor,
    uint32_t *output_register_identifier);
runtimec_status_t binrwir_program_builder_add_symbol(
    binrwir_program_builder_t *builder, const uint8_t *bytes, size_t byte_count,
    uint32_t *output_symbol_identifier);
runtimec_status_t binrwir_program_builder_add_literal(
    binrwir_program_builder_t *builder,
    const binrwir_literal_descriptor_t *literal_descriptor,
    const uint8_t *bytes, size_t byte_count,
    uint32_t *output_literal_identifier);
runtimec_status_t binrwir_program_builder_add_callsite(
    binrwir_program_builder_t *builder,
    const binrwir_callsite_build_request_t *request,
    uint32_t *output_callsite_identifier);
runtimec_status_t binrwir_program_builder_add_instruction(
    binrwir_program_builder_t *builder,
    const binrwir_instruction_build_request_t *request,
    uint32_t *output_instruction_identifier);
runtimec_status_t
binrwir_program_builder_finalize(binrwir_program_builder_t *builder,
                               binrwir_program_t **output_program);

_Static_assert(sizeof(binrwir_register_descriptor_t) == 16U,
               "binrwir register descriptions must remain 16 bytes");
_Static_assert(sizeof(binrwir_program_identity_t) == 32U,
               "binrwir program identity must remain 32 bytes");
_Static_assert(sizeof(binrwir_literal_descriptor_t) == 16U,
               "binrwir literal descriptions must remain 16 bytes");
_Static_assert(sizeof(binrwir_callsite_t) == 16U,
               "binrwir callsites must remain 16 bytes");
_Static_assert(sizeof(binrwir_call_alias_mapping_t) == 8U,
               "binrwir call alias mappings must remain 8 bytes");

#ifdef __cplusplus
}
#endif

#endif
