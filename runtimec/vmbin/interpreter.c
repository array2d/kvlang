/*
 * 组件：vmbin/interpreter 的实现
 * 作用：唯一 vmbin 执行入口：取指、按 reference_kind 解析操作数、core 规则与
 *       builtin 分发、完成边界推进。
 * 约束：阶段 1 的全部值访问都经 vmbin/value_access 按地址读写值体（以 shm 为准）；
 *       基址 → 值体只走合同的 runtimec_xvalue_body_address，本文件不写偏移算术、
 *       不保留跨指令的值体指针或副本。指令只在写已落值体后才推进游标；失败一律
 *       产生结构化 ERROR 出口且不推进。
 */

#include "interpreter.h"

#include <string.h>

#include "contract/error.h"
#include "core_operations.h"
#include "value_access.h"

/* opid 0 不对应任何能力行：它不是「未知算子」，是非法指令头。 */
#define VMBIN_OPERATION_IDENTIFIER_NONE UINT32_C(0)

#define VMBIN_SCRATCH_INITIAL_CAPACITY 32U

typedef struct vmbin_execution {
    const binrwir_program_t *program;
    const binrwir_registry_t *builtin_registry;
    vmbin_profile_t *profile;
    binrwir_execution_frame_t *frame;
    size_t instruction_count;
    /* 只装本层写下的错误分类；不保留任何值字节。 */
    runtimec_error_t error;
} vmbin_execution_t;

static void vmbin_interpreter_error_exit(binrwir_execution_exit_t *output_exit,
                                         runtimec_status_t status,
                                         uint32_t detail_code,
                                         uint32_t source_program_counter) {
    *output_exit = (binrwir_execution_exit_t){0};
    output_exit->kind = BINRWIR_EXECUTION_EXIT_ERROR;
    output_exit->status = status;
    output_exit->logical_program_counter = source_program_counter;
    output_exit->resume_logical_program_counter = source_program_counter;
    output_exit->detail_code = detail_code;
}

/* 完成出口：写已落存储、游标已推进，才置 COMPLETED_STATE_COMMITTED。 */
static void vmbin_interpreter_committed_exit(
    binrwir_execution_exit_t *output_exit, uint16_t kind,
    uint32_t source_program_counter, uint32_t resume_program_counter,
    uint32_t target_identifier) {
    *output_exit = (binrwir_execution_exit_t){0};
    output_exit->kind = kind;
    output_exit->status = RUNTIMEC_STATUS_SUCCESS;
    output_exit->flags = BINRWIR_EXECUTION_EXIT_FLAG_COMPLETED_STATE_COMMITTED;
    output_exit->logical_program_counter = source_program_counter;
    output_exit->resume_logical_program_counter = resume_program_counter;
    output_exit->target_identifier = target_identifier;
}

static runtimec_status_t vmbin_interpreter_fail(
    vmbin_execution_t *execution,
    const binrwir_instruction_view_t *instruction, runtimec_status_t status,
    uint32_t detail_code, binrwir_execution_exit_t *output_exit) {
    (void)execution;
    vmbin_interpreter_error_exit(output_exit, status, detail_code,
                                 instruction->source_logical_program_counter);
    return status;
}

/* 失败细节优先用下层写下的稳定错误码，没有才用本层分类。 */
static runtimec_status_t vmbin_interpreter_fail_with_error(
    vmbin_execution_t *execution,
    const binrwir_instruction_view_t *instruction, runtimec_status_t status,
    uint32_t default_detail_code, binrwir_execution_exit_t *output_exit) {
    uint32_t detail_code = default_detail_code;

    if (runtimec_error_is_set(&execution->error) &&
        execution->error.code > 0 &&
        (uint64_t)execution->error.code <= UINT32_MAX) {
        detail_code = (uint32_t)execution->error.code;
    }
    return vmbin_interpreter_fail(execution, instruction, status, detail_code,
                                  output_exit);
}

/* 借出一个参数项的值体位置；读写信都从这里拿地址，不复制。 */
static runtimec_status_t vmbin_interpreter_locate(
    vmbin_execution_t *execution, const binrwir_argument_t *argument,
    vmbin_value_location_t *output_location) {
    return vmbin_value_access_locate(execution->frame, argument,
                                     output_location, &execution->error);
}

/* 1/2/4/8 字节整数按主机字节序解成无符号值；其它宽度不是本阶段的值形状。 */
static int vmbin_interpreter_decode_unsigned(const uint8_t *bytes,
                                             size_t size,
                                             uint64_t *output_value) {
    uint64_t value = 0U;

    if (bytes == NULL) {
        return 0;
    }
    switch (size) {
    case 1U: {
        uint8_t narrow = 0U;
        memcpy(&narrow, bytes, sizeof(narrow));
        value = narrow;
        break;
    }
    case 2U: {
        uint16_t narrow = 0U;
        memcpy(&narrow, bytes, sizeof(narrow));
        value = narrow;
        break;
    }
    case 4U: {
        uint32_t narrow = 0U;
        memcpy(&narrow, bytes, sizeof(narrow));
        value = narrow;
        break;
    }
    case 8U:
        memcpy(&value, bytes, sizeof(value));
        break;
    default:
        return 0;
    }
    *output_value = value;
    return 1;
}

/* 跳转目标是 source logical program counter，换成 Program 内的指令标识符。 */
static runtimec_status_t vmbin_interpreter_resolve_target(
    vmbin_execution_t *execution, const binrwir_argument_t *argument,
    uint32_t *output_instruction_identifier) {
    vmbin_value_location_t target_location;
    uint64_t target = 0U;
    runtimec_status_t status;

    *output_instruction_identifier = BINRWIR_IDENTIFIER_NONE;
    status = vmbin_interpreter_locate(execution, argument, &target_location);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (!vmbin_interpreter_decode_unsigned(target_location.read_bytes,
                                           target_location.size, &target) ||
        target > UINT32_MAX) {
        runtimec_error_set(&execution->error, RUNTIMEC_STATUS_INVALID,
                           RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                           (int64_t)VMBIN_INTERPRETER_DIAG_UNDECODABLE_TARGET,
                           runtimec_string_view_from_c_string(
                               "branch target is not a 64-bit counter"),
                           RUNTIMEC_PROGRAM_COUNTER_NONE);
        return RUNTIMEC_STATUS_INVALID;
    }
    status = binrwir_program_find_instruction(
        execution->program, (uint32_t)target, output_instruction_identifier);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        runtimec_error_set(&execution->error, RUNTIMEC_STATUS_INVALID,
                           RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                           (int64_t)VMBIN_INTERPRETER_DIAG_UNRESOLVED_TARGET,
                           runtimec_string_view_from_c_string(
                               "branch target is not in the program"),
                           RUNTIMEC_PROGRAM_COUNTER_NONE);
    }
    return status;
}

/* 只做顺序推进；完成边界的全部写已经到存储。 */
static void vmbin_interpreter_advance(vmbin_execution_t *execution,
                                      uint32_t instruction_identifier) {
    execution->frame->logical_program_counter = instruction_identifier + 1U;
    if (execution->profile != NULL) {
        execution->profile->instruction_count += 1U;
    }
}

static runtimec_status_t vmbin_interpreter_execute_copy(
    vmbin_execution_t *execution,
    const binrwir_instruction_view_t *instruction, uint32_t identifier,
    binrwir_execution_exit_t *output_exit) {
    vmbin_value_location_t source;
    runtimec_status_t status;

    if (instruction->read_arguments.count != 1U ||
        instruction->write_arguments.count != 1U) {
        return vmbin_interpreter_fail(
            execution, instruction, RUNTIMEC_STATUS_INVALID,
            VMBIN_INTERPRETER_DIAG_WRONG_ARITY, output_exit);
    }
    status = vmbin_interpreter_locate(execution,
                                      &instruction->read_arguments.data[0],
                                      &source);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return vmbin_interpreter_fail_with_error(
            execution, instruction, status, VMBIN_INTERPRETER_DIAG_VALUE_ACCESS,
            output_exit);
    }
    status = vmbin_value_access_write(
        execution->frame, &instruction->write_arguments.data[0],
        runtimec_bytes_view_make(source.read_bytes, source.size),
        &execution->error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return vmbin_interpreter_fail_with_error(
            execution, instruction, status, VMBIN_INTERPRETER_DIAG_VALUE_ACCESS,
            output_exit);
    }
    vmbin_interpreter_advance(execution, identifier);
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t vmbin_interpreter_execute_goto(
    vmbin_execution_t *execution,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    uint32_t target_identifier = BINRWIR_IDENTIFIER_NONE;
    runtimec_status_t status;

    if (instruction->read_arguments.count != 1U) {
        return vmbin_interpreter_fail(
            execution, instruction, RUNTIMEC_STATUS_INVALID,
            VMBIN_INTERPRETER_DIAG_WRONG_ARITY, output_exit);
    }
    status = vmbin_interpreter_resolve_target(
        execution, &instruction->read_arguments.data[0], &target_identifier);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return vmbin_interpreter_fail_with_error(
            execution, instruction, status, VMBIN_INTERPRETER_DIAG_VALUE_ACCESS,
            output_exit);
    }
    execution->frame->logical_program_counter = target_identifier;
    if (execution->profile != NULL) {
        execution->profile->instruction_count += 1U;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t vmbin_interpreter_execute_branch(
    vmbin_execution_t *execution,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    vmbin_value_location_t condition_location;
    uint64_t condition = 0U;
    uint32_t target_identifier = BINRWIR_IDENTIFIER_NONE;
    runtimec_status_t status;

    if (instruction->read_arguments.count != 3U) {
        return vmbin_interpreter_fail(
            execution, instruction, RUNTIMEC_STATUS_INVALID,
            VMBIN_INTERPRETER_DIAG_WRONG_ARITY, output_exit);
    }
    status = vmbin_interpreter_locate(execution,
                                      &instruction->read_arguments.data[0],
                                      &condition_location);
    if (status == RUNTIMEC_STATUS_SUCCESS &&
        !vmbin_interpreter_decode_unsigned(condition_location.read_bytes,
                                           condition_location.size,
                                           &condition)) {
        status = RUNTIMEC_STATUS_INVALID;
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return vmbin_interpreter_fail_with_error(
            execution, instruction, status, VMBIN_INTERPRETER_DIAG_VALUE_ACCESS,
            output_exit);
    }
    status = vmbin_interpreter_resolve_target(
        execution, &instruction->read_arguments.data[condition != 0U ? 1U : 2U],
        &target_identifier);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return vmbin_interpreter_fail_with_error(
            execution, instruction, status, VMBIN_INTERPRETER_DIAG_VALUE_ACCESS,
            output_exit);
    }
    execution->frame->logical_program_counter = target_identifier;
    if (execution->profile != NULL) {
        execution->profile->instruction_count += 1U;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/* call：目标由第一个 FUNCTION 参数项给出（slot = 符号池下标）。 */
static runtimec_status_t vmbin_interpreter_execute_call(
    vmbin_execution_t *execution,
    const binrwir_instruction_view_t *instruction, uint32_t identifier,
    binrwir_execution_exit_t *output_exit) {
    size_t argument_ordinal;
    uint32_t target_identifier = BINRWIR_IDENTIFIER_NONE;
    uint32_t resume_program_counter = BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE;

    for (argument_ordinal = 0U;
         argument_ordinal < instruction->read_arguments.count;
         ++argument_ordinal) {
        const binrwir_argument_t *argument =
            &instruction->read_arguments.data[argument_ordinal];

        if (argument->reference_kind == BINRWIR_REFERENCE_FUNCTION) {
            target_identifier = (uint32_t)argument->slot;
            break;
        }
    }
    if (target_identifier == BINRWIR_IDENTIFIER_NONE) {
        return vmbin_interpreter_fail(
            execution, instruction, RUNTIMEC_STATUS_INVALID,
            VMBIN_INTERPRETER_DIAG_INVALID_INSTRUCTION, output_exit);
    }
    if (identifier + 1U < execution->instruction_count) {
        binrwir_instruction_view_t next_instruction;

        if (binrwir_program_get_instruction(execution->program, identifier + 1U,
                                            &next_instruction) ==
            RUNTIMEC_STATUS_SUCCESS) {
            resume_program_counter =
                next_instruction.source_logical_program_counter;
        }
    }
    vmbin_interpreter_advance(execution, identifier);
    vmbin_interpreter_committed_exit(
        output_exit, BINRWIR_EXECUTION_EXIT_CALL,
        instruction->source_logical_program_counter, resume_program_counter,
        target_identifier);
    return RUNTIMEC_STATUS_SUCCESS;
}

/* return：控制交回调用方；本帧没有待提交的写。 */
static runtimec_status_t vmbin_interpreter_execute_return(
    vmbin_execution_t *execution,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    if (execution->profile != NULL) {
        execution->profile->instruction_count += 1U;
    }
    vmbin_interpreter_committed_exit(
        output_exit, BINRWIR_EXECUTION_EXIT_DONE,
        instruction->source_logical_program_counter,
        BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE, 0U);
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * builtin：命中注册表就调用 handler。handler 不写 kind 时视为「已完成」，
 * 按普通指令推进游标；写了 kind 就是侧出口，游标保持不动。
 */
static runtimec_status_t vmbin_interpreter_dispatch_builtin(
    vmbin_execution_t *execution,
    const binrwir_instruction_view_t *instruction, uint32_t identifier,
    binrwir_execution_exit_t *output_exit) {
    const binrwir_registry_entry_t *entry = NULL;
    runtimec_status_t status = binrwir_registry_find(
        execution->builtin_registry,
        instruction->instruction->operation_identifier, &entry);

    /* registry 是 session 自己装的表：Program 里出现表里没有的 opid，说明编译期
     * 与执行期的表不一致，是内部不一致，报 INVALID。外部能力走 FOREIGN 标记，
     * 不从这里出去——两者不要混。registry 未命中返回 UNSUPPORTED，这里把它
     * 折成 INVALID：本层没有「不支持」这种外部语义。
     *
     * 但「表被写坏」是另一回事：条目存在而 handler 为空，说明表被绕过 append
     * 构造过。它与「表里没有这个 opid」成因不同、修法不同，必须分开诊断。 */
    if (status == RUNTIMEC_STATUS_INVALID) {
        return vmbin_interpreter_fail(
            execution, instruction, RUNTIMEC_STATUS_INVALID,
            VMBIN_INTERPRETER_DIAG_CORRUPT_REGISTRY, output_exit);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return vmbin_interpreter_fail(
            execution, instruction, RUNTIMEC_STATUS_INVALID,
            VMBIN_INTERPRETER_DIAG_INVALID_INSTRUCTION, output_exit);
    }
    *output_exit = (binrwir_execution_exit_t){0};
    status = entry->handler(execution->frame, instruction,
                                      output_exit);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        vmbin_interpreter_error_exit(
            output_exit, status, VMBIN_INTERPRETER_DIAG_HANDLER_FAILED,
            instruction->source_logical_program_counter);
        return status;
    }
    if (output_exit->kind != BINRWIR_EXECUTION_EXIT_NONE) {
        /* handler 主动侧退出：指令未提交，游标不推进。 */
        return RUNTIMEC_STATUS_SUCCESS;
    }
    vmbin_interpreter_advance(execution, identifier);
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t vmbin_interpreter_step(
    vmbin_execution_t *execution, uint32_t identifier,
    binrwir_execution_exit_t *output_exit) {
    binrwir_instruction_view_t instruction;
    runtimec_status_t status;

    /* 每一步都从「没有侧出口」开始；只有 call/return/handoff/handler 才写它。 */
    *output_exit = (binrwir_execution_exit_t){0};
    status = binrwir_program_get_instruction(execution->program, identifier,
                                             &instruction);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        *output_exit = (binrwir_execution_exit_t){0};
        output_exit->kind = BINRWIR_EXECUTION_EXIT_ERROR;
        output_exit->status = RUNTIMEC_STATUS_INVALID;
        output_exit->detail_code = VMBIN_INTERPRETER_DIAG_INVALID_INSTRUCTION;
        output_exit->logical_program_counter =
            BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE;
        output_exit->resume_logical_program_counter =
            BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE;
        return RUNTIMEC_STATUS_INVALID;
    }
    runtimec_error_reset(&execution->error);
    switch (instruction.instruction->operation_identifier) {
    case VMBIN_OPERATION_IDENTIFIER_COPY:
        return vmbin_interpreter_execute_copy(execution, &instruction,
                                              identifier, output_exit);
    case VMBIN_OPERATION_IDENTIFIER_GOTO:
        return vmbin_interpreter_execute_goto(execution, &instruction,
                                              output_exit);
    case VMBIN_OPERATION_IDENTIFIER_BRANCH:
        return vmbin_interpreter_execute_branch(execution, &instruction,
                                                output_exit);
    case VMBIN_OPERATION_IDENTIFIER_CALL:
        return vmbin_interpreter_execute_call(execution, &instruction,
                                              identifier, output_exit);
    case VMBIN_OPERATION_IDENTIFIER_RETURN:
        return vmbin_interpreter_execute_return(execution, &instruction,
                                                output_exit);
    case BINRWIR_OPERATION_IDENTIFIER_FOREIGN:
        /* 外部能力的结构标记：不执行、不写、不推进游标，把控制交出去。
         * 不置 COMPLETED_STATE_COMMITTED——本条一个字节都没写，置了就是谎报，
         * 下游会为一条不存在的提交做 checkpoint。 */
        *output_exit = (binrwir_execution_exit_t){0};
        output_exit->kind = BINRWIR_EXECUTION_EXIT_HANDOFF;
        output_exit->status = RUNTIMEC_STATUS_UNSUPPORTED;
        output_exit->logical_program_counter =
            instruction.source_logical_program_counter;
        output_exit->resume_logical_program_counter =
            instruction.source_logical_program_counter;
        return RUNTIMEC_STATUS_SUCCESS;
    case VMBIN_OPERATION_IDENTIFIER_NONE:
        return vmbin_interpreter_fail(
            execution, &instruction, RUNTIMEC_STATUS_INVALID,
            VMBIN_INTERPRETER_DIAG_INVALID_INSTRUCTION, output_exit);
    default:
        return vmbin_interpreter_dispatch_builtin(execution, &instruction,
                                                  identifier, output_exit);
    }
}

runtimec_status_t
vmbin_execute(const binrwir_program_t *program,
              const binrwir_execution_plan_t *execution_plan,
              binrwir_execution_frame_t *frame,
              const binrwir_registry_t *builtin_registry,
              vmbin_profile_t *profile,
              binrwir_execution_exit_t *output_exit) {
    vmbin_execution_t execution;
    binrwir_program_view_t program_view;
    runtimec_status_t status;

    /* 执行计划是可删除的派生状态，phase 1 取指不依赖它。 */
    (void)execution_plan;
    if (output_exit != NULL) {
        vmbin_interpreter_error_exit(output_exit, RUNTIMEC_STATUS_INVALID,
                                     VMBIN_INTERPRETER_DIAG_INVALID_REQUEST,
                                     BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE);
    }
    if (program == NULL || frame == NULL || builtin_registry == NULL ||
        output_exit == NULL || frame->program != program) {
        return RUNTIMEC_STATUS_INVALID;
    }
    status = binrwir_program_validate(program);
    if (status != RUNTIMEC_STATUS_SUCCESS ||
        binrwir_program_get_view(program, &program_view) !=
            RUNTIMEC_STATUS_SUCCESS) {
        return RUNTIMEC_STATUS_INVALID;
    }
    memset(&execution, 0, sizeof(execution));
    runtimec_error_reset(&execution.error);
    execution.program = program;
    execution.builtin_registry = builtin_registry;
    execution.profile = profile;
    execution.frame = frame;
    /* call 出口要靠它判断「有没有后继指令」，从而给出可恢复位置。 */
    execution.instruction_count = program_view.instruction_count;

    for (;;) {
        uint32_t identifier = frame->logical_program_counter;

        if (identifier == BINRWIR_IDENTIFIER_NONE) {
            vmbin_interpreter_error_exit(
                output_exit, RUNTIMEC_STATUS_INVALID,
                VMBIN_INTERPRETER_DIAG_INVALID_CURSOR,
                BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE);
            status = RUNTIMEC_STATUS_INVALID;
            break;
        }
        if ((size_t)identifier >= program_view.instruction_count) {
            /* 顺序执行越过末尾：没有待提交的写，按完成处理。 */
            vmbin_interpreter_committed_exit(
                output_exit, BINRWIR_EXECUTION_EXIT_DONE,
                BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE,
                BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE, 0U);
            status = RUNTIMEC_STATUS_SUCCESS;
            break;
        }
        status = vmbin_interpreter_step(&execution, identifier, output_exit);
        if (status != RUNTIMEC_STATUS_SUCCESS ||
            output_exit->kind != BINRWIR_EXECUTION_EXIT_NONE) {
            break;
        }
    }
    return status;
}
