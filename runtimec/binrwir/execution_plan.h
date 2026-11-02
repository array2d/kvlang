#ifndef BINRWIR_EXECUTION_PLAN_H
#define BINRWIR_EXECUTION_PLAN_H

/*
 * execution plan 是 canonical BinRWIR Program 的只读派生物。它可以随时
 * 删除并从同一 Program 重建；它不是 RWIR 或 BinRWIR 的语义事实源。
 * execution plan 只承载执行加速决策，不持有机器码、缓存、统计、可变
 * execution frame 或 kvspace 值。BinRWIR Compiler 在编译阶段确定执行
 * 目标；vmbin 与 BinRWIR JIT 只执行已选择的目标，不拒绝计划、不重新选路。
 */

#include <stddef.h>
#include <stdint.h>

#include "contract/status.h"
#include "binrwir/program.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BINRWIR_EXECUTION_PLAN_IDENTIFIER_NONE UINT32_MAX

typedef enum binrwir_execution_plan_flags {
    BINRWIR_EXECUTION_PLAN_FLAG_NONE = 0,
    BINRWIR_EXECUTION_PLAN_FLAG_HAS_INSTRUCTION_ENCODING = 1U << 0,
    BINRWIR_EXECUTION_PLAN_FLAG_HAS_TYPE_SPECIALIZATION = 1U << 1,
    BINRWIR_EXECUTION_PLAN_FLAG_HAS_KVSPACE_ADDRESSING = 1U << 2,
    BINRWIR_EXECUTION_PLAN_FLAG_HAS_INSTRUCTION_FUSION = 1U << 3,
    BINRWIR_EXECUTION_PLAN_FLAG_HAS_VIRTUAL_REGISTERS = 1U << 4,
    BINRWIR_EXECUTION_PLAN_FLAG_HAS_CONTROL_FLOW = 1U << 5
} binrwir_execution_plan_flags_t;

typedef enum binrwir_virtual_register_location {
    BINRWIR_VIRTUAL_REGISTER_LOCATION_NONE = 0,
    BINRWIR_VIRTUAL_REGISTER_LOCATION_FRAME = 1,
    BINRWIR_VIRTUAL_REGISTER_LOCATION_EXECUTION_REGISTER = 2,
    BINRWIR_VIRTUAL_REGISTER_LOCATION_SPILL = 3
} binrwir_virtual_register_location_t;

typedef struct binrwir_execution_plan_identity {
    uint64_t program_generation;
    uint64_t plan_generation;
    uint32_t binrwir_binary_interface_generation;
    uint32_t compiler_revision;
} binrwir_execution_plan_identity_t;

typedef struct binrwir_instruction_encoding {
    uint32_t instruction_identifier;
    uint32_t byte_offset;
    uint16_t byte_size;
    uint16_t dispatch_identifier;
    uint32_t flags;
} binrwir_instruction_encoding_t;

typedef struct binrwir_type_specialization {
    uint32_t instruction_identifier;
    uint32_t type_identifier;
    uint32_t guard_identifier;
    uint16_t specialized_operation_code;
    uint16_t flags;
} binrwir_type_specialization_t;

typedef struct binrwir_kvspace_address {
    uint32_t instruction_identifier;
    uint32_t key_symbol_identifier;
    uint32_t address_identifier;
    uint32_t generation_guard_identifier;
    uint64_t byte_offset;
} binrwir_kvspace_address_t;

typedef struct binrwir_instruction_fusion {
    uint32_t first_instruction_identifier;
    uint32_t instruction_count;
    uint32_t fused_operation_identifier;
    uint32_t first_source_mapping;
    uint32_t source_mapping_count;
    uint32_t flags;
} binrwir_instruction_fusion_t;

typedef struct binrwir_virtual_register {
    uint32_t register_identifier;
    uint32_t live_range_begin;
    uint32_t live_range_end;
    uint32_t location_identifier;
    uint16_t location;
    uint16_t flags;
} binrwir_virtual_register_t;

typedef struct binrwir_control_flow_block {
    uint32_t first_instruction_identifier;
    uint32_t instruction_count;
    uint32_t successor_offset;
    uint16_t successor_count;
    uint16_t flags;
} binrwir_control_flow_block_t;

typedef struct binrwir_source_program_counter_mapping {
    uint32_t execution_operation_identifier;
    uint32_t source_logical_program_counter;
    uint32_t first_instruction_identifier;
    uint32_t instruction_count;
} binrwir_source_program_counter_mapping_t;

typedef struct binrwir_execution_plan binrwir_execution_plan_t;

typedef struct binrwir_execution_plan_view {
    binrwir_execution_plan_identity_t identity;
    const binrwir_program_t *program;
    const binrwir_instruction_encoding_t *instruction_encodings;
    size_t instruction_encoding_count;
    const uint8_t *encoded_instruction_bytes;
    size_t encoded_instruction_byte_count;
    const binrwir_type_specialization_t *type_specializations;
    size_t type_specialization_count;
    const binrwir_kvspace_address_t *kvspace_addresses;
    size_t kvspace_address_count;
    const binrwir_instruction_fusion_t *instruction_fusions;
    size_t instruction_fusion_count;
    const binrwir_virtual_register_t *virtual_registers;
    size_t virtual_register_count;
    const binrwir_control_flow_block_t *control_flow_blocks;
    size_t control_flow_block_count;
    const uint32_t *control_flow_successors;
    size_t control_flow_successor_count;
    const binrwir_source_program_counter_mapping_t *source_mappings;
    size_t source_mapping_count;
    uint32_t flags;
    uint32_t reserved_32_bits;
} binrwir_execution_plan_view_t;

_Static_assert(sizeof(binrwir_instruction_encoding_t) == 16U,
               "binrwir instruction encodings must remain 16 bytes");

runtimec_status_t binrwir_execution_plan_validate(
    const binrwir_execution_plan_t *plan);
runtimec_status_t binrwir_execution_plan_get_view(
    const binrwir_execution_plan_t *plan,
    binrwir_execution_plan_view_t *output_view);
runtimec_status_t binrwir_execution_plan_destroy(
    binrwir_execution_plan_t *plan);

#ifdef __cplusplus
}
#endif

#endif
