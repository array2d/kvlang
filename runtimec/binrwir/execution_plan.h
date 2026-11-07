#ifndef BINRWIR_EXECUTION_PLAN_H
#define BINRWIR_EXECUTION_PLAN_H

/*
 * execution plan 是 canonical BinRWIR Program 的只读派生物。它可以随时
 * 删除并从同一 Program 重建；它不是 RWIR 或 BinRWIR 的语义事实源。
 * execution plan 只承载类型特化、dispatch 融合、控制流和 source logical
 * program counter 映射。Program 已经是地址化二进制，不得在 plan 中重复
 * 编码 instruction、解析或缓存地址，也不得建立任何中间值存储模型。
 * BinRWIR Compiler 在编译阶段确定执行目标；vmbin 与 BinRWIR JIT 只执行
 * 已选择的目标，不拒绝计划、不重新选路。
 */

#include <stddef.h>
#include <stdint.h>

#include "contract/status.h"
#include "binrwir/binfunc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BINRWIR_EXECUTION_PLAN_IDENTIFIER_NONE UINT32_MAX

typedef enum binrwir_execution_plan_flags {
    BINRWIR_EXECUTION_PLAN_FLAG_NONE = 0,
    BINRWIR_EXECUTION_PLAN_FLAG_HAS_TYPE_SPECIALIZATION = 1U << 0,
    BINRWIR_EXECUTION_PLAN_FLAG_HAS_DISPATCH_FUSION = 1U << 1,
    BINRWIR_EXECUTION_PLAN_FLAG_HAS_CONTROL_FLOW = 1U << 2,
    BINRWIR_EXECUTION_PLAN_FLAG_HAS_SOURCE_MAPPING = 1U << 3
} binrwir_execution_plan_flags_t;

typedef struct binrwir_execution_plan_identity {
    uint64_t snapshot_generation;
    uint64_t program_generation;
    uint64_t plan_generation;
    uint32_t binrwir_binary_interface_generation;
    uint32_t compiler_revision;
} binrwir_execution_plan_identity_t;

typedef struct binrwir_type_specialization {
    uint32_t instruction_identifier;
    uint32_t type_identifier;
    uint32_t guard_identifier;
    uint32_t specialized_operation_identifier;
    uint16_t flags;
} binrwir_type_specialization_t;

/*
 * dispatch fusion 只减少解释器分发。它必须保持每条逻辑 instruction 的
 * V.body 写回和 source mapping，且不得跨越 recovery boundary。
 */
typedef struct binrwir_dispatch_fusion {
    uint32_t first_instruction_identifier;
    uint32_t instruction_count;
    uint32_t fused_dispatch_identifier;
    uint32_t first_source_mapping;
    uint32_t source_mapping_count;
} binrwir_dispatch_fusion_t;

typedef struct binrwir_control_flow_block {
    uint32_t first_instruction_identifier;
    uint32_t instruction_count;
    uint32_t successor_offset;
    uint16_t successor_count;
    uint16_t flags;
} binrwir_control_flow_block_t;

typedef struct binrwir_source_program_counter_mapping {
    uint32_t execution_dispatch_identifier;
    uint32_t source_logical_program_counter;
    uint32_t first_instruction_identifier;
    uint32_t instruction_count;
} binrwir_source_program_counter_mapping_t;

typedef struct binrwir_execution_plan binrwir_execution_plan_t;

typedef struct binrwir_execution_plan_view {
    binrwir_execution_plan_identity_t identity;
    const binrwir_binfunc_t *program;
    const binrwir_type_specialization_t *type_specializations;
    size_t type_specialization_count;
    const binrwir_dispatch_fusion_t *dispatch_fusions;
    size_t dispatch_fusion_count;
    const binrwir_control_flow_block_t *control_flow_blocks;
    size_t control_flow_block_count;
    const uint32_t *control_flow_successors;
    size_t control_flow_successor_count;
    const binrwir_source_program_counter_mapping_t *source_mappings;
    size_t source_mapping_count;
    uint32_t flags;
    uint32_t reserved_32_bits;
} binrwir_execution_plan_view_t;

_Static_assert(sizeof(binrwir_execution_plan_identity_t) == 32U,
               "binrwir execution plan identity must remain 32 bytes");
_Static_assert(
    sizeof(((binrwir_type_specialization_t *)0)
               ->specialized_operation_identifier) == sizeof(uint32_t),
    "specialized operation identifiers must remain 32 bits");

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
