/*
 * 组件：BinRWIR Compiler 的实现（VMBIN 档）
 * 作用：把已地址化的 canonical Program 编译成完整 compilation：Program 的
 *       不可变副本 + 派生的 execution plan + 已确定的执行目标。本阶段固定产出
 *       VMBIN 目标，不带机器码。
 * 联系：route 唯一调用者；plan 由 optimization 派生，JIT 不在本档。
 * 约束：不读 RWIR、不解析 key、不缓存 V.body、不执行 Program；执行目标在这里
 *       一次选定，执行期不得再选。真实编译错误绝不降级成 VMBIN plan——本档没有
 *       这种错误来源，因为 JIT 未启用；位置留在 compile 的目标判定处。
 */

#include "binrwir_compiler/compiler.h"

#include <string.h>

#include "binrwir_compiler/optimization.h"

struct binrwir_compiler_t {
    runtimec_allocator_t allocator;
    uint32_t compiler_revision;
    /* plan 身份里的代次：同一份 Program 每编译一次前进一格。 */
    uint64_t plan_generation;
};

struct binrwir_compilation_t {
    runtimec_allocator_t allocator;
    binrwir_compilation_view_t view;
    /* compilation 独占的两份派生对象：Program 副本与 execution plan。 */
    binrwir_program_t *canonical_program;
    binrwir_execution_plan_t *execution_plan;
};

static runtimec_status_t binrwir_compiler_fail(runtimec_error_t *error,
                                               runtimec_status_t status,
                                               int64_t code,
                                               const char *message) {
    runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT, code,
                       runtimec_string_view_from_c_string(message),
                       runtimec_string_view_make(NULL, 0U));
    return status;
}

static void binrwir_compilation_reset_view(binrwir_compilation_view_t *view) {
    if (view != NULL) {
        *view = (binrwir_compilation_view_t){
            .execution_target = BINRWIR_EXECUTION_TARGET_INVALID,
            .canonical_program = NULL,
            .execution_plan = NULL,
            .binrwir_jit_code = NULL,
        };
    }
}

/*
 * enable_jit 为真时 JIT 字段必须齐全；为假时四个字段必须全零。这条判据只描述
 * 配置是否自洽，不描述能力是否存在——能力由 create 单独判。
 */
static bool binrwir_compiler_jit_configuration_is_valid(
    const binrwir_compiler_configuration_t *configuration) {
    if (configuration->enable_jit) {
        return configuration->jit_machine_code_capacity > 0U &&
               configuration->jit_target.architecture !=
                   BINRWIR_JIT_ARCHITECTURE_NONE &&
               runtimec_string_view_is_valid(configuration->jit_target.triple);
    }
    return configuration->jit_machine_code_capacity == 0U &&
           configuration->jit_target.architecture ==
               BINRWIR_JIT_ARCHITECTURE_NONE &&
           configuration->jit_target.triple.data == NULL &&
           configuration->jit_target.triple.size == 0U &&
           configuration->jit_target.feature_mask == 0U;
}

/* 用 builder 把 Program 逐项重放成新 Program：这是唯一的深拷贝路径。 */
static runtimec_status_t binrwir_compiler_clone_program(
    const runtimec_allocator_t *allocator, const binrwir_program_t *source,
    binrwir_program_t **output_program, runtimec_error_t *error) {
    binrwir_program_view_t view;
    binrwir_program_builder_t *builder = NULL;
    binrwir_program_t *clone = NULL;
    size_t ordinal;
    runtimec_status_t status;

    *output_program = NULL;
    status = binrwir_program_get_view(source, &view);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return binrwir_compiler_fail(error, status, 20,
                                     "canonical program is not readable");
    }
    status = binrwir_program_builder_create(allocator, &view.identity, &builder);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return binrwir_compiler_fail(error, status, 21,
                                     "program clone builder failed");
    }
    for (ordinal = 0U; ordinal < view.signature.read_parameter_count; ++ordinal) {
        uint32_t parameter_index = 0U;

        status = binrwir_program_builder_add_parameter(
            builder, BINRWIR_PARAMETER_DIRECTION_READ,
            view.signature.read_parameter_argument_indices[ordinal],
            view.signature.read_parameter_types[ordinal], &parameter_index);
        if (status != RUNTIMEC_STATUS_SUCCESS ||
            parameter_index != (uint32_t)ordinal) {
            status = RUNTIMEC_STATUS_INVALID;
            goto fail;
        }
    }
    for (ordinal = 0U; ordinal < view.signature.write_parameter_count;
         ++ordinal) {
        uint32_t parameter_index = 0U;

        status = binrwir_program_builder_add_parameter(
            builder, BINRWIR_PARAMETER_DIRECTION_WRITE,
            view.signature.write_parameter_argument_indices[ordinal],
            view.signature.write_parameter_types[ordinal], &parameter_index);
        if (status != RUNTIMEC_STATUS_SUCCESS ||
            parameter_index != (uint32_t)ordinal) {
            status = RUNTIMEC_STATUS_INVALID;
            goto fail;
        }
    }
    for (ordinal = 0U; ordinal < view.signature.alias_rule_count; ++ordinal) {
        uint32_t alias_rule_identifier = 0U;

        status = binrwir_program_builder_add_alias_rule(
            builder, &view.signature.alias_rules[ordinal],
            &alias_rule_identifier);
        if (status != RUNTIMEC_STATUS_SUCCESS ||
            alias_rule_identifier != (uint32_t)ordinal) {
            status = RUNTIMEC_STATUS_INVALID;
            goto fail;
        }
    }
    {
        size_t symbol_count = 0U;

        status = binrwir_program_get_symbol_count(source, &symbol_count);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            goto fail;
        }
        for (ordinal = 0U; ordinal < symbol_count; ++ordinal) {
            runtimec_string_view_t name;
            uint16_t symbol_index = 0U;

            status = binrwir_program_get_symbol(source, (uint16_t)ordinal, &name);
            if (status != RUNTIMEC_STATUS_SUCCESS) {
                goto fail;
            }
            /* Program 的符号池已是名字字典序；重放后下标必须原地不动。 */
            status = binrwir_program_builder_add_symbol(builder, name,
                                                        &symbol_index);
            if (status != RUNTIMEC_STATUS_SUCCESS ||
                symbol_index != (uint16_t)ordinal) {
                status = RUNTIMEC_STATUS_INVALID;
                goto fail;
            }
        }
    }
    {
        size_t literal_count = 0U;

        status = binrwir_program_get_literal_count(source, &literal_count);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            goto fail;
        }
        for (ordinal = 0U; ordinal < literal_count; ++ordinal) {
            runtimec_bytes_view_t wire_bytes;
            runtimec_string_view_t langtype;
            uint32_t literal_index = BINRWIR_LITERAL_INDEX_NONE;

            status = binrwir_program_get_literal(source, (uint32_t)ordinal,
                                                 &wire_bytes, &langtype);
            if (status != RUNTIMEC_STATUS_SUCCESS) {
                goto fail;
            }
            /* 字面量池按下标被 payload 引用，重放必须保持同一顺序。 */
            status = binrwir_program_builder_add_literal(
                builder, wire_bytes, langtype, &literal_index);
            if (status != RUNTIMEC_STATUS_SUCCESS ||
                literal_index != (uint32_t)ordinal) {
                status = RUNTIMEC_STATUS_INVALID;
                goto fail;
            }
        }
    }
    for (ordinal = 0U; ordinal < view.instruction_count; ++ordinal) {
        binrwir_instruction_view_t instruction;
        binrwir_instruction_build_request_t request;
        uint32_t instruction_identifier = BINRWIR_IDENTIFIER_NONE;

        status = binrwir_program_get_instruction(source, (uint32_t)ordinal,
                                                 &instruction);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            goto fail;
        }
        request.source_logical_program_counter =
            instruction.source_logical_program_counter;
        request.operation_identifier =
            instruction.instruction->operation_identifier;
        request.read_arguments = instruction.read_arguments;
        request.write_arguments = instruction.write_arguments;
        status = binrwir_program_builder_add_instruction(
            builder, &request, &instruction_identifier);
        if (status != RUNTIMEC_STATUS_SUCCESS ||
            instruction_identifier != (uint32_t)ordinal) {
            status = RUNTIMEC_STATUS_INVALID;
            goto fail;
        }
    }
    status = binrwir_program_builder_finalize(builder, &clone);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        goto fail;
    }
    (void)binrwir_program_builder_destroy(builder);
    *output_program = clone;
    return RUNTIMEC_STATUS_SUCCESS;

fail:
    (void)binrwir_program_builder_destroy(builder);
    return binrwir_compiler_fail(error, status, 22,
                                 "canonical program cannot be copied");
}

runtimec_status_t binrwir_compiler_create(
    const runtimec_allocator_t *allocator,
    const binrwir_compiler_configuration_t *configuration,
    binrwir_compiler_t **output_compiler, runtimec_error_t *error) {
    binrwir_compiler_t *compiler;

    if (output_compiler != NULL) {
        *output_compiler = NULL;
    }
    if (!runtimec_allocator_is_valid(allocator) || configuration == NULL ||
        output_compiler == NULL ||
        configuration->structure_size != sizeof(*configuration) ||
        configuration->compiler_revision == 0U ||
        !binrwir_compiler_jit_configuration_is_valid(configuration)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (configuration->enable_jit) {
        /* 本阶段只有 VMBIN 档：配置自洽也不代表能力存在。 */
        return binrwir_compiler_fail(error, RUNTIMEC_STATUS_NOT_IMPLEMENTED, 1,
                                     "JIT compilation is not implemented yet");
    }
    compiler = (binrwir_compiler_t *)runtimec_allocator_allocate(
        allocator, sizeof(*compiler), _Alignof(binrwir_compiler_t));
    if (compiler == NULL) {
        return binrwir_compiler_fail(error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 2,
                                     "compiler allocation failed");
    }
    compiler->allocator = *allocator;
    compiler->compiler_revision = configuration->compiler_revision;
    compiler->plan_generation = 0U;
    *output_compiler = compiler;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_compiler_destroy(binrwir_compiler_t *compiler,
                                           runtimec_error_t *error) {
    runtimec_allocator_t allocator;

    (void)error;
    if (compiler == NULL || !runtimec_allocator_is_valid(&compiler->allocator)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    allocator = compiler->allocator;
    return runtimec_allocator_deallocate(&allocator, compiler,
                                         sizeof(*compiler),
                                         _Alignof(binrwir_compiler_t));
}

runtimec_status_t binrwir_compiler_get_revision(
    const binrwir_compiler_t *compiler, uint32_t *output_compiler_revision) {
    if (output_compiler_revision != NULL) {
        *output_compiler_revision = UINT32_MAX;
    }
    if (compiler == NULL || output_compiler_revision == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    *output_compiler_revision = compiler->compiler_revision;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_compiler_compile(
    binrwir_compiler_t *compiler, const binrwir_program_t *canonical_program,
    binrwir_compilation_t **output_compilation, runtimec_error_t *error) {
    binrwir_program_t *program_copy = NULL;
    binrwir_execution_plan_t *execution_plan = NULL;
    binrwir_compilation_t *compilation = NULL;
    uint64_t plan_generation;
    runtimec_status_t status;

    if (output_compilation != NULL) {
        *output_compilation = NULL;
    }
    if (compiler == NULL || canonical_program == NULL ||
        output_compilation == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (binrwir_program_validate(canonical_program) != RUNTIMEC_STATUS_SUCCESS) {
        return binrwir_compiler_fail(error, RUNTIMEC_STATUS_INVALID, 3,
                                     "canonical program is not valid");
    }
    if (compiler->plan_generation == UINT64_MAX) {
        return binrwir_compiler_fail(error, RUNTIMEC_STATUS_INVALID, 4,
                                     "plan generation is exhausted");
    }
    status = binrwir_compiler_clone_program(&compiler->allocator,
                                            canonical_program, &program_copy,
                                            error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    plan_generation = compiler->plan_generation + 1U;
    status = binrwir_compiler_build_execution_plan(
        &compiler->allocator, program_copy, plan_generation,
        compiler->compiler_revision, &execution_plan, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        (void)binrwir_program_destroy(program_copy);
        return status;
    }
    /*
     * 目标判定点：本档只有 VMBIN。将来 JIT 打开时，JIT 的不可编译判定在这里
     * 形成 VMBIN plan，真实编译错误在这里直接返回、绝不降级。
     */
    compilation = (binrwir_compilation_t *)runtimec_allocator_allocate(
        &compiler->allocator, sizeof(*compilation),
        _Alignof(binrwir_compilation_t));
    if (compilation == NULL) {
        (void)binrwir_execution_plan_destroy(execution_plan);
        (void)binrwir_program_destroy(program_copy);
        return binrwir_compiler_fail(error, RUNTIMEC_STATUS_OUT_OF_MEMORY, 5,
                                     "compilation allocation failed");
    }
    memset(compilation, 0, sizeof(*compilation));
    compilation->allocator = compiler->allocator;
    compilation->canonical_program = program_copy;
    compilation->execution_plan = execution_plan;
    compilation->view.execution_target = BINRWIR_EXECUTION_TARGET_VMBIN;
    compilation->view.canonical_program = program_copy;
    compilation->view.execution_plan = execution_plan;
    compilation->view.binrwir_jit_code = NULL;
    compiler->plan_generation = plan_generation;
    *output_compilation = compilation;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_compilation_get_view(
    const binrwir_compilation_t *compilation,
    binrwir_compilation_view_t *output_view, runtimec_error_t *error) {
    (void)error;
    binrwir_compilation_reset_view(output_view);
    if (compilation == NULL || output_view == NULL ||
        compilation->canonical_program == NULL ||
        compilation->execution_plan == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    *output_view = compilation->view;
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t binrwir_compilation_destroy(
    binrwir_compilation_t *compilation, runtimec_error_t *error) {
    runtimec_allocator_t allocator;
    runtimec_error_t ignored;
    runtimec_status_t status;

    (void)error;
    if (compilation == NULL ||
        !runtimec_allocator_is_valid(&compilation->allocator)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    allocator = compilation->allocator;
    runtimec_error_reset(&ignored);
    if (compilation->execution_plan != NULL) {
        status = binrwir_execution_plan_destroy(compilation->execution_plan);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
    }
    if (compilation->canonical_program != NULL) {
        status = binrwir_program_destroy(compilation->canonical_program);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
    }
    return runtimec_allocator_deallocate(&allocator, compilation,
                                         sizeof(*compilation),
                                         _Alignof(binrwir_compilation_t));
}
