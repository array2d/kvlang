#ifndef BINRWIR_BINFUNC_H
#define BINRWIR_BINFUNC_H

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

#define BINRWIR_SYMBOL_INDEX_NONE UINT16_MAX
#define BINRWIR_LITERAL_INDEX_NONE UINT32_MAX

typedef struct binrwir_binfunc_identity {
    uint64_t snapshot_generation;
    uint64_t program_generation;
    uint32_t schema_version;
    uint32_t rwir_manifest_version;
    uint32_t binrwir_binary_interface_generation;
    uint32_t canonical_encoding_revision;
} binrwir_binfunc_identity_t;

typedef struct binrwir_binfunc binrwir_binfunc_t;
typedef struct binrwir_binfunc_builder binrwir_binfunc_builder_t;

typedef struct binrwir_binfunc_view {
    binrwir_binfunc_identity_t identity;
    binrwir_signature_view_t signature;
    
    size_t instruction_count;
} binrwir_binfunc_view_t;

runtimec_status_t binrwir_binfunc_get_identity(
    const binrwir_binfunc_t *program,
    binrwir_binfunc_identity_t *output_identity);

runtimec_status_t binrwir_binfunc_validate(const binrwir_binfunc_t *program);
runtimec_status_t binrwir_binfunc_get_view(const binrwir_binfunc_t *program,
                                           binrwir_binfunc_view_t *output_view);
runtimec_status_t binrwir_binfunc_get_instruction(
    const binrwir_binfunc_t *program, uint32_t instruction_identifier,
    binrwir_instruction_view_t *output_instruction);

runtimec_status_t binrwir_binfunc_find_instruction(
    const binrwir_binfunc_t *program, uint32_t source_logical_program_counter,
    uint32_t *output_instruction_identifier);

runtimec_status_t binrwir_binfunc_get_symbol_count(
    const binrwir_binfunc_t *program, size_t *output_symbol_count);

runtimec_status_t binrwir_binfunc_get_symbol(
    const binrwir_binfunc_t *program, uint16_t symbol_index,
    runtimec_string_view_t *output_name,
    runtimec_string_view_t *output_langtype);

runtimec_status_t binrwir_binfunc_get_literal_count(
    const binrwir_binfunc_t *program, size_t *output_literal_count);

runtimec_status_t binrwir_binfunc_get_literal(
    const binrwir_binfunc_t *program, uint32_t literal_index,
    runtimec_bytes_view_t *output_wire_bytes,
    runtimec_string_view_t *output_langtype);
runtimec_status_t binrwir_binfunc_destroy(binrwir_binfunc_t *program);

bool binrwir_symbol_name_is_member(runtimec_string_view_t name);

runtimec_status_t binrwir_binfunc_builder_create(
    const runtimec_allocator_t *allocator,
    const binrwir_binfunc_identity_t *identity,
    binrwir_binfunc_builder_t **output_builder);
runtimec_status_t
binrwir_binfunc_builder_reset(binrwir_binfunc_builder_t *builder);
runtimec_status_t binrwir_binfunc_builder_destroy(
    binrwir_binfunc_builder_t *builder);

runtimec_status_t binrwir_binfunc_builder_add_parameter(
    binrwir_binfunc_builder_t *builder, binrwir_parameter_direction_t direction,
    uint32_t argument_index, runtimec_string_view_t parameter_type,
    uint32_t *output_parameter_index);

runtimec_status_t binrwir_binfunc_builder_add_alias_rule(
    binrwir_binfunc_builder_t *builder, const binrwir_alias_rule_t *rule,
    uint32_t *output_alias_rule_identifier);

runtimec_status_t binrwir_binfunc_builder_find_symbol(
    const binrwir_binfunc_builder_t *builder, runtimec_string_view_t name,
    uint16_t *output_symbol_index);

runtimec_status_t binrwir_binfunc_builder_add_symbol(
    binrwir_binfunc_builder_t *builder, runtimec_string_view_t name,
    runtimec_string_view_t langtype, uint16_t *output_symbol_index);

bool binrwir_literal_is_none_value(runtimec_bytes_view_t wire_bytes);

runtimec_status_t binrwir_binfunc_builder_add_literal(
    binrwir_binfunc_builder_t *builder, runtimec_bytes_view_t wire_bytes,
    runtimec_string_view_t langtype, uint32_t *output_literal_index);

runtimec_status_t binrwir_binfunc_builder_add_instruction(
    binrwir_binfunc_builder_t *builder,
    const binrwir_instruction_build_request_t *request,
    uint32_t *output_instruction_identifier);

runtimec_status_t binrwir_binfunc_builder_finalize(
    binrwir_binfunc_builder_t *builder, binrwir_binfunc_t **output_program);

_Static_assert(sizeof(binrwir_binfunc_identity_t) == 32U,
               "binrwir program identity must remain 32 bytes");

#ifdef __cplusplus
}
#endif

#endif
