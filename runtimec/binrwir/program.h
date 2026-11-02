#ifndef BINRWIR_PROGRAM_H
#define BINRWIR_PROGRAM_H

/*
 * 组件：binrwir/program
 * 作用：定义与 RWIR 语义等价的 canonical、不可变、地址化 BinRWIR Program。
 * 联系：vmkv 解析 RWIR 并构造 Program；BinRWIR Compiler 与 vmbin 只读消费。
 * 约束：公开二进制载荷只有 opid、argidx、argaddr。可变参数分区、source mapping
 *       与存储 framing 是 Program 私有元数据，不进入 instruction，也不缓存 V.body。
 */

#include <stddef.h>
#include <stdint.h>

#include "binrwir/argument.h"
#include "binrwir/instruction.h"
#include "binrwir/signature.h"
#include "contract/allocator.h"
#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BINRWIR_IDENTIFIER_NONE UINT32_MAX

typedef struct binrwir_program_identity {
    uint64_t snapshot_generation;
    uint64_t program_generation;
    uint32_t schema_version;
    uint32_t rwir_manifest_version;
    uint32_t binrwir_binary_interface_generation;
    uint32_t canonical_encoding_revision;
} binrwir_program_identity_t;

typedef struct binrwir_program binrwir_program_t;
typedef struct binrwir_program_builder binrwir_program_builder_t;

typedef struct binrwir_program_view {
    binrwir_program_identity_t identity;
    binrwir_signature_view_t signature;
    /* 逐条内容只能经 get_instruction 取得，私有 framing 不对外暴露。 */
    size_t instruction_count;
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
    uint32_t argument_index, uint32_t *output_parameter_index);
runtimec_status_t binrwir_program_builder_add_alias_rule(
    binrwir_program_builder_t *builder, const binrwir_alias_rule_t *rule,
    uint32_t *output_alias_rule_identifier);
runtimec_status_t binrwir_program_builder_add_instruction(
    binrwir_program_builder_t *builder,
    const binrwir_instruction_build_request_t *request,
    uint32_t *output_instruction_identifier);
runtimec_status_t binrwir_program_builder_finalize(
    binrwir_program_builder_t *builder, binrwir_program_t **output_program);

_Static_assert(sizeof(binrwir_program_identity_t) == 32U,
               "binrwir program identity must remain 32 bytes");

#ifdef __cplusplus
}
#endif

#endif
