/*
 * 组件：vmbin/program 的实现
 * 作用：Program 查询和构造入口；当前均明确返回未实现。
 * 约束：每个输出在失败前进入无效状态。
 */

#include "vmbin/program.h"

#include <string.h>

static void vmbin_program_identity_invalidate(
    vmbin_program_identity_t *identity) {
    if (identity != NULL) {
        memset(identity, 0, sizeof(*identity));
        identity->schema_version = UINT32_MAX;
        identity->rwir_manifest_version = UINT32_MAX;
        identity->binrwir_binary_interface_generation = UINT32_MAX;
    }
}

runtimec_status_t vmbin_program_get_identity(
    const vmbin_program_t *program,
    vmbin_program_identity_t *output_identity) {
    vmbin_program_identity_invalidate(output_identity);
    if (program == NULL || output_identity == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmbin_program_validate(const vmbin_program_t *program) {
    if (program == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmbin_program_get_view(
    const vmbin_program_t *program, vmbin_program_view_t *output_view) {
    if (output_view != NULL) {
        memset(output_view, 0, sizeof(*output_view));
        vmbin_program_identity_invalidate(&output_view->identity);
    }
    if (program == NULL || output_view == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmbin_program_get_instruction(
    const vmbin_program_t *program, uint32_t instruction_identifier,
    vmbin_instruction_view_t *output_instruction) {
    if (output_instruction != NULL) {
        memset(output_instruction, 0, sizeof(*output_instruction));
    }
    if (program == NULL || instruction_identifier == VMBIN_IDENTIFIER_NONE ||
        output_instruction == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmbin_program_destroy(vmbin_program_t *program) {
    if (program == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmbin_program_builder_create(
    const runtimec_allocator_t *allocator,
    const vmbin_program_identity_t *identity,
    vmbin_program_builder_t **output_builder) {
    if (output_builder != NULL) {
        *output_builder = NULL;
    }
    if (allocator == NULL || identity == NULL || output_builder == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmbin_program_builder_reset(vmbin_program_builder_t *builder) {
    if (builder == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmbin_program_builder_destroy(
    vmbin_program_builder_t *builder) {
    if (builder == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmbin_program_builder_add_parameter(
    vmbin_program_builder_t *builder, vmbin_parameter_direction_t direction,
    uint32_t register_identifier, uint16_t *output_parameter_ordinal) {
    if (output_parameter_ordinal != NULL) {
        *output_parameter_ordinal = UINT16_MAX;
    }
    if (builder == NULL || direction == VMBIN_PARAMETER_DIRECTION_INVALID ||
        register_identifier == VMBIN_IDENTIFIER_NONE ||
        output_parameter_ordinal == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmbin_program_builder_add_alias_rule(
    vmbin_program_builder_t *builder, const vmbin_alias_rule_t *rule,
    uint32_t *output_alias_rule_identifier) {
    if (output_alias_rule_identifier != NULL) {
        *output_alias_rule_identifier = VMBIN_IDENTIFIER_NONE;
    }
    if (builder == NULL || rule == NULL ||
        output_alias_rule_identifier == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmbin_program_builder_add_register(
    vmbin_program_builder_t *builder,
    const vmbin_register_descriptor_t *register_descriptor,
    uint32_t *output_register_identifier) {
    if (output_register_identifier != NULL) {
        *output_register_identifier = VMBIN_IDENTIFIER_NONE;
    }
    if (builder == NULL || register_descriptor == NULL ||
        output_register_identifier == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmbin_program_builder_add_symbol(
    vmbin_program_builder_t *builder, const uint8_t *bytes, size_t byte_count,
    uint32_t *output_symbol_identifier) {
    if (output_symbol_identifier != NULL) {
        *output_symbol_identifier = VMBIN_IDENTIFIER_NONE;
    }
    if (builder == NULL || (byte_count != 0U && bytes == NULL) ||
        output_symbol_identifier == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmbin_program_builder_add_literal(
    vmbin_program_builder_t *builder,
    const vmbin_literal_descriptor_t *literal_descriptor,
    const uint8_t *bytes, size_t byte_count,
    uint32_t *output_literal_identifier) {
    if (output_literal_identifier != NULL) {
        *output_literal_identifier = VMBIN_IDENTIFIER_NONE;
    }
    if (builder == NULL || literal_descriptor == NULL ||
        (byte_count != 0U && bytes == NULL) ||
        output_literal_identifier == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmbin_program_builder_add_callsite(
    vmbin_program_builder_t *builder,
    const vmbin_callsite_build_request_t *request,
    uint32_t *output_callsite_identifier) {
    if (output_callsite_identifier != NULL) {
        *output_callsite_identifier = VMBIN_IDENTIFIER_NONE;
    }
    if (builder == NULL || request == NULL ||
        output_callsite_identifier == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t vmbin_program_builder_add_instruction(
    vmbin_program_builder_t *builder,
    const vmbin_instruction_build_request_t *request,
    uint32_t *output_instruction_identifier) {
    if (output_instruction_identifier != NULL) {
        *output_instruction_identifier = VMBIN_IDENTIFIER_NONE;
    }
    if (builder == NULL || request == NULL ||
        output_instruction_identifier == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}

runtimec_status_t
vmbin_program_builder_finalize(vmbin_program_builder_t *builder,
                               vmbin_program_t **output_program) {
    if (output_program != NULL) {
        *output_program = NULL;
    }
    if (builder == NULL || output_program == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_NOT_IMPLEMENTED;
}
