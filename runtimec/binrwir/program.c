/*
 * 组件：binrwir/program 的实现
 * 作用：地址化 Program 查询和构造入口；当前均明确返回未实现。
 * 约束：每个输出在失败前进入无效状态。
 */

#include "binrwir/program.h"

#include <string.h>

static void binrwir_program_identity_invalidate(
    binrwir_program_identity_t *identity) {
    if (identity != NULL) {
        memset(identity, 0, sizeof(*identity));
        identity->schema_version = UINT32_MAX;
        identity->rwir_manifest_version = UINT32_MAX;
        identity->binrwir_binary_interface_generation = UINT32_MAX;
        identity->canonical_encoding_revision = UINT32_MAX;
    }
}

runtimec_status_t binrwir_program_get_identity(
    const binrwir_program_t *program,
    binrwir_program_identity_t *output_identity) {
    binrwir_program_identity_invalidate(output_identity);
    if (program == NULL || output_identity == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_program_validate(const binrwir_program_t *program) {
    if (program == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_program_get_view(
    const binrwir_program_t *program, binrwir_program_view_t *output_view) {
    if (output_view != NULL) {
        memset(output_view, 0, sizeof(*output_view));
        binrwir_program_identity_invalidate(&output_view->identity);
    }
    if (program == NULL || output_view == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_program_get_instruction(
    const binrwir_program_t *program, uint32_t instruction_identifier,
    binrwir_instruction_view_t *output_instruction) {
    if (output_instruction != NULL) {
        memset(output_instruction, 0, sizeof(*output_instruction));
    }
    if (program == NULL || instruction_identifier == BINRWIR_IDENTIFIER_NONE ||
        output_instruction == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_program_destroy(binrwir_program_t *program) {
    if (program == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_program_builder_create(
    const runtimec_allocator_t *allocator,
    const binrwir_program_identity_t *identity,
    binrwir_program_builder_t **output_builder) {
    if (output_builder != NULL) {
        *output_builder = NULL;
    }
    if (allocator == NULL || identity == NULL || output_builder == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_program_builder_reset(binrwir_program_builder_t *builder) {
    if (builder == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_program_builder_destroy(
    binrwir_program_builder_t *builder) {
    if (builder == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_program_builder_add_parameter(
    binrwir_program_builder_t *builder, binrwir_parameter_direction_t direction,
    uint32_t argument_index, uint32_t *output_parameter_index) {
    if (output_parameter_index != NULL) {
        *output_parameter_index = BINRWIR_ARGUMENT_INDEX_NONE;
    }
    if (builder == NULL || direction == BINRWIR_PARAMETER_DIRECTION_INVALID ||
        argument_index == BINRWIR_ARGUMENT_INDEX_NONE ||
        output_parameter_index == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_program_builder_add_alias_rule(
    binrwir_program_builder_t *builder, const binrwir_alias_rule_t *rule,
    uint32_t *output_alias_rule_identifier) {
    if (output_alias_rule_identifier != NULL) {
        *output_alias_rule_identifier = BINRWIR_IDENTIFIER_NONE;
    }
    if (builder == NULL || rule == NULL ||
        output_alias_rule_identifier == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_program_builder_add_instruction(
    binrwir_program_builder_t *builder,
    const binrwir_instruction_build_request_t *request,
    uint32_t *output_instruction_identifier) {
    if (output_instruction_identifier != NULL) {
        *output_instruction_identifier = BINRWIR_IDENTIFIER_NONE;
    }
    if (builder == NULL || request == NULL ||
        output_instruction_identifier == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t binrwir_program_builder_finalize(
    binrwir_program_builder_t *builder, binrwir_program_t **output_program) {
    if (output_program != NULL) {
        *output_program = NULL;
    }
    if (builder == NULL || output_program == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
