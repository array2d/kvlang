/*
 * 组件：engine/execution_window 的实现
 * 作用：执行窗口：取权威快照 → 查 compilation repository → 未命中则 vmkv
 *       等价编码 canonical BinRWIR → BinRWIR Compiler 编译 → 读执行目标 →
 *       单次分派 → 提交完成边界的 checkpoint → 返回结构化退出。
 * 联系：调度与「下一条指令归谁」不在本层，属 kvspace 调度结构与外部调度者。
 *       本层是唯一同时消费 vmkv、vmbin 与 BinRWIR JIT 的文件；host 能力只经
 *       session 借出的能力表使用，不直接接触 kvspace。
 * 约束：执行期不重新编译、不重新解析地址、不重新选择执行器，所选入口失败也
 *       不切换；只有带 COMPLETED_STATE_COMMITTED 的退出才提交 checkpoint，
 *       未完成指令的状态整体丢弃；形参槽号与 vmbin/frame_slot 的分区一致，
 *       代数取 host 解析那一刻的真实版本。
 */

#include "engine/execution_window.h"

#include <stdio.h>
#include <string.h>

#include "binrwir/execution_frame.h"
#include "binrwir/frame_bindings.h"
#include "binrwir/program.h"
#include "binrwir_compiler/compiler.h"
#include "binrwir_compiler/jit/executor.h"
#include "contract/checkpoint.h"
#include "engine/compilation_repository.h"
#include "engine/session.h"
#include "vmbin/frame_slot.h"
#include "vmbin/interpreter.h"
#include "vmkv/encoder.h"
#include "vmkv/rwir_manifest.h"

/* `‥pc` 的文本形状：函数绝对路径 + `[<source pc>,0]`（同 kvcpu 的写回串）。 */
#define RUNTIMEC_EXECUTION_WINDOW_PROGRAM_COUNTER_PREFIX "/["
#define RUNTIMEC_EXECUTION_WINDOW_PROGRAM_COUNTER_SUFFIX ",0]"
#define RUNTIMEC_EXECUTION_WINDOW_UNSIGNED_DECIMAL_CAPACITY 11U

typedef enum runtimec_execution_window_diagnostic_t {
    RUNTIMEC_EXECUTION_WINDOW_DIAG_INVALID_REQUEST = 1,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_MISSING_HOST_CAPABILITY = 2,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_SNAPSHOT_FAILED = 3,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_REPOSITORY_FAILED = 4,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_ENCODE_FAILED = 5,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_COMPILE_FAILED = 6,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_COMPILATION_INVALID = 7,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_COUNT = 8,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_UNRESOLVED = 9,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_BINDING_FAILED = 10,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_FRAME_FAILED = 11,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_PROGRAM_COUNTER_NOT_FOUND = 12,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_FRAME_ENSURE_FAILED = 13,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_EXECUTION_FAILED = 14,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_CHECKPOINT_FAILED = 15,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_OUT_OF_MEMORY = 16
} runtimec_execution_window_diagnostic_t;

static void runtimec_execution_window_reset_result(runtimec_execution_window_result_t *result,
                                        runtimec_status_t status,
                                        uint32_t detail_code) {
    *result = (runtimec_execution_window_result_t){0};
    result->status = status;
    result->execution_exit.kind = BINRWIR_EXECUTION_EXIT_ERROR;
    result->execution_exit.status = status;
    result->execution_exit.detail_code = detail_code;
    result->execution_exit.logical_program_counter =
        BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE;
    result->execution_exit.resume_logical_program_counter =
        BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE;
}

static runtimec_status_t runtimec_execution_window_fail(runtimec_execution_window_result_t *result,
                                             runtimec_error_t *error,
                                             runtimec_status_t status,
                                             runtimec_execution_window_diagnostic_t detail,
                                             const char *message) {
    runtimec_execution_window_reset_result(result, status, (uint32_t)detail);
    runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT, detail,
                       runtimec_string_view_from_c_string(message),
                       runtimec_string_view_make(NULL, 0U));
    return status;
}

/*
 * `‥pc` 文本由执行窗口组装：函数绝对路径 + `[<source pc>,0]`。这是 kvspace
 * 帧计数器的既有形状（kvcpu 写回同一个串），不是新的键空间。
 * output_text 由调用方用同一 allocator 释放。
 */
static runtimec_status_t runtimec_execution_window_make_program_counter(
    const runtimec_allocator_t *allocator, runtimec_string_view_t function_path,
    uint32_t source_program_counter, char **output_text) {
    const size_t capacity = function_path.size +
                            sizeof(RUNTIMEC_EXECUTION_WINDOW_PROGRAM_COUNTER_PREFIX) +
                            RUNTIMEC_EXECUTION_WINDOW_UNSIGNED_DECIMAL_CAPACITY +
                            sizeof(RUNTIMEC_EXECUTION_WINDOW_PROGRAM_COUNTER_SUFFIX);
    char *text;
    int written;
    char counter_text[RUNTIMEC_EXECUTION_WINDOW_UNSIGNED_DECIMAL_CAPACITY];

    *output_text = NULL;
    if (function_path.size == 0U) {
        return RUNTIMEC_STATUS_INVALID;
    }
    text = (char *)runtimec_allocator_allocate(allocator, capacity, 1U);
    if (text == NULL) {
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    written = snprintf(counter_text, sizeof(counter_text), "%u",
                       source_program_counter);
    if (written <= 0 || (size_t)written >= sizeof(counter_text)) {
        (void)runtimec_allocator_deallocate(allocator, text, capacity, 1U);
        return RUNTIMEC_STATUS_INVALID;
    }
    memcpy(text, function_path.data, function_path.size);
    size_t length = function_path.size;
    const size_t prefix_size = sizeof(RUNTIMEC_EXECUTION_WINDOW_PROGRAM_COUNTER_PREFIX) - 1U;
    memcpy(text + length, RUNTIMEC_EXECUTION_WINDOW_PROGRAM_COUNTER_PREFIX, prefix_size);
    length += prefix_size;
    memcpy(text + length, counter_text, (size_t)written);
    length += (size_t)written;
    const size_t suffix_size = sizeof(RUNTIMEC_EXECUTION_WINDOW_PROGRAM_COUNTER_SUFFIX) - 1U;
    memcpy(text + length, RUNTIMEC_EXECUTION_WINDOW_PROGRAM_COUNTER_SUFFIX, suffix_size);
    length += suffix_size;
    text[length] = '\0';
    *output_text = text;
    return RUNTIMEC_STATUS_SUCCESS;
}

/* 解析一个绝对槽键并写进绑定表；代数必须与本次快照一致且非零。 */
static runtimec_status_t runtimec_execution_window_bind_slot(
    const runtimec_host_services_t *host, const runtimec_snapshot_t *snapshot,
    binrwir_frame_bindings_t *bindings, uint16_t binding_slot,
    runtimec_string_view_t key_text, uint64_t *generation_in_out,
    runtimec_execution_window_diagnostic_t *output_detail, runtimec_error_t *error) {
    runtimec_xvalue_address_t xvalue_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
    uint64_t generation = BINRWIR_FRAME_BINDING_GENERATION_NONE;
    const runtimec_key_view_t slot_key =
        runtimec_key_view_make(key_text.data, key_text.size);
    runtimec_status_t status = host->resolve_frame_xvalue_address(
        host->context, snapshot, slot_key, &xvalue_address, &generation, error);

    if (status != RUNTIMEC_STATUS_SUCCESS) {
        *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_UNRESOLVED;
        return status;
    }
    /* 零地址、零代数都不是可用的绑定：未经证明的地址一律不绑。 */
    if (xvalue_address == RUNTIMEC_XVALUE_ADDRESS_NONE || generation == 0U) {
        *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_UNRESOLVED;
        return RUNTIMEC_STATUS_INVALID;
    }
    if (*generation_in_out == BINRWIR_FRAME_BINDING_GENERATION_NONE) {
        *generation_in_out = generation;
    } else if (*generation_in_out != generation) {
        /* 绑到一半存储换代了：这一帧的地址不再属于同一份快照。 */
        *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_UNRESOLVED;
        return RUNTIMEC_STATUS_ERROR;
    }
    if (binrwir_frame_bindings_bind(bindings, binding_slot, slot_key, slot_key,
                                    xvalue_address, generation) !=
        RUNTIMEC_STATUS_SUCCESS) {
        *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_BINDING_FAILED;
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * 顶层帧的形参绑定。槽号分区与 vmbin/frame_slot 完全一致（读参 0..nr-1、
 * 写参 nr..nr+nw-1），实参是调用方给的绝对槽键，route 不拼不猜。
 *
 * 局部符号本阶段不绑：符号池只有名字，局部槽的真实键属于 vthread 帧的坐标
 * 空间，而 route 没有任何事实源能推出那个前缀。未绑的局部槽在访问时失败封闭。
 */
static runtimec_status_t runtimec_execution_window_bind_parameters(
    const runtimec_host_services_t *host, const runtimec_snapshot_t *snapshot,
    const binrwir_program_t *program, const runtimec_execution_window_request_t *request,
    binrwir_frame_bindings_t *bindings, uint64_t *output_generation,
    runtimec_execution_window_diagnostic_t *output_detail, runtimec_error_t *error) {
    binrwir_program_view_t program_view;
    size_t read_count;
    size_t write_count;
    size_t ordinal;

    *output_generation = BINRWIR_FRAME_BINDING_GENERATION_NONE;
    if (binrwir_program_get_view(program, &program_view) !=
        RUNTIMEC_STATUS_SUCCESS) {
        *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_FRAME_FAILED;
        return RUNTIMEC_STATUS_INVALID;
    }
    read_count = program_view.signature.read_parameter_count;
    write_count = program_view.signature.write_parameter_count;
    /* 读参在前、写参在后，数量必须等于签名总数。 */
    if (read_count > SIZE_MAX - write_count ||
        request->argument_count != read_count + write_count) {
        *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_COUNT;
        return RUNTIMEC_STATUS_INVALID;
    }
    for (ordinal = 0U; ordinal < read_count; ++ordinal) {
        uint16_t binding_slot = BINRWIR_FRAME_BINDING_SLOT_NONE;
        if (vmbin_frame_slot_of_parameter(program,
                                          BINRWIR_PARAMETER_DIRECTION_READ,
                                          (uint32_t)ordinal,
                                          &binding_slot) !=
            RUNTIMEC_STATUS_SUCCESS) {
            *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_BINDING_FAILED;
            return RUNTIMEC_STATUS_INVALID;
        }
        runtimec_status_t status = runtimec_execution_window_bind_slot(
            host, snapshot, bindings, binding_slot, request->arguments[ordinal],
            output_generation, output_detail, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
    }
    for (ordinal = 0U; ordinal < write_count; ++ordinal) {
        uint16_t binding_slot = BINRWIR_FRAME_BINDING_SLOT_NONE;
        if (vmbin_frame_slot_of_parameter(program,
                                          BINRWIR_PARAMETER_DIRECTION_WRITE,
                                          (uint32_t)ordinal,
                                          &binding_slot) !=
            RUNTIMEC_STATUS_SUCCESS) {
            *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_BINDING_FAILED;
            return RUNTIMEC_STATUS_INVALID;
        }
        runtimec_status_t status = runtimec_execution_window_bind_slot(
            host, snapshot, bindings, binding_slot,
            request->arguments[read_count + ordinal], output_generation,
            output_detail, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/* 只有写已落到存储的退出才提交；resume pc 为空表示没有可恢复位置。 */
static runtimec_string_view_t runtimec_execution_window_persistent_status(
    uint16_t exit_kind) {
    switch (exit_kind) {
    case BINRWIR_EXECUTION_EXIT_DONE:
        return runtimec_string_view_from_c_string("done");
    case BINRWIR_EXECUTION_EXIT_YIELD:
        return runtimec_string_view_from_c_string("paused");
    case BINRWIR_EXECUTION_EXIT_HANDOFF:
        return runtimec_string_view_from_c_string("waiting");
    case BINRWIR_EXECUTION_EXIT_ERROR:
        return runtimec_string_view_from_c_string("error");
    case BINRWIR_EXECUTION_EXIT_CALL:
    case BINRWIR_EXECUTION_EXIT_BUILTIN:
    default:
        return runtimec_string_view_from_c_string("running");
    }
}

static runtimec_status_t runtimec_execution_window_commit_checkpoint(
    runtimec_session_t *session, const runtimec_host_services_t *host,
    const runtimec_snapshot_t *snapshot,
    const runtimec_execution_window_request_t *request,
    const binrwir_execution_exit_t *execution_exit, runtimec_error_t *error) {
    runtimec_checkpoint_t checkpoint;
    char *program_counter_text = NULL;
    uint32_t source_program_counter;
    runtimec_status_t status;

    /* 未完成的指令没有可提交的状态：整条丢弃。 */
    if ((execution_exit->flags &
         BINRWIR_EXECUTION_EXIT_FLAG_COMPLETED_STATE_COMMITTED) == 0U) {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    /*
     * DONE 提交的是「刚完成那条指令」的位置（resume 为 NONE 只表示不必再恢复）；
     * 其余完成边界提交可恢复的位置，退一步用刚完成的位置。
     */
    if (execution_exit->kind == BINRWIR_EXECUTION_EXIT_DONE) {
        source_program_counter = execution_exit->logical_program_counter;
    } else if (execution_exit->resume_logical_program_counter !=
               BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE) {
        source_program_counter = execution_exit->resume_logical_program_counter;
    } else {
        source_program_counter = execution_exit->logical_program_counter;
    }
    /* 位置本身未知就失败封闭：宁可红，也不把 `‥pc` 写成一个编出来的位置。 */
    if (source_program_counter == BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE) {
        runtimec_error_set(error, RUNTIMEC_STATUS_INVALID,
                           RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                           RUNTIMEC_EXECUTION_WINDOW_DIAG_CHECKPOINT_FAILED,
                           runtimec_string_view_from_c_string(
                               "completed exit carries no source "
                               "program counter"),
                           runtimec_string_view_make(NULL, 0U));
        return RUNTIMEC_STATUS_INVALID;
    }
    /* prefix 只来自快照里的函数键：route 只格式化，不推导路径。 */
    status = runtimec_execution_window_make_program_counter(
        &host->allocator,
        runtimec_string_view_make(snapshot->function_key.data,
                                  snapshot->function_key.size),
        source_program_counter, &program_counter_text);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    runtimec_checkpoint_reset(&checkpoint);
    checkpoint.contract_version = snapshot->contract_version;
    checkpoint.source_revision = snapshot->source_revision;
    status = runtimec_session_take_checkpoint_sequence(session,
                                                       &checkpoint.sequence);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        (void)runtimec_allocator_deallocate(&host->allocator,
                                            program_counter_text,
                                            strlen(program_counter_text) + 1U,
                                            1U);
        return status;
    }
    /* runtime/frame 标识本阶段留空：适配层不读，填一个编出来的值是第二事实。 */
    checkpoint.vthread_identifier = request->vthread_identifier;
    checkpoint.logical_program_counter =
        runtimec_string_view_from_c_string(program_counter_text);
    checkpoint.persistent_status =
        runtimec_execution_window_persistent_status(execution_exit->kind);
    checkpoint.outcome = execution_exit->status;
    status = host->checkpoint(host->context, &checkpoint, error);
    (void)runtimec_allocator_deallocate(&host->allocator, program_counter_text,
                                        strlen(program_counter_text) + 1U, 1U);
    return status;
}

/*
 * 起始位置换算：调用方给的是 source logical program counter（layout 的 irseq），
 * 执行游标是稠密的指令标识符，两者唯一的映射就是 binrwir_program_find_instruction。
 */
static runtimec_status_t runtimec_execution_window_find_instruction(
    const binrwir_program_t *program, uint32_t source_program_counter,
    uint32_t *output_instruction_identifier,
    runtimec_execution_window_diagnostic_t *output_detail) {
    *output_instruction_identifier = BINRWIR_IDENTIFIER_NONE;
    if (binrwir_program_find_instruction(program, source_program_counter,
                                         output_instruction_identifier) !=
            RUNTIMEC_STATUS_SUCCESS ||
        *output_instruction_identifier == BINRWIR_IDENTIFIER_NONE) {
        *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_PROGRAM_COUNTER_NOT_FOUND;
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/* 未命中的完整编译链：编码 → 编译 → 入仓库并取租约。 */
static runtimec_status_t runtimec_execution_window_compile_and_insert(
    const runtimec_session_runtime_components_t *components,
    const runtimec_host_services_t *host, const runtimec_execution_window_request_t *request,
    const runtimec_snapshot_t *snapshot,
    const runtimec_compilation_repository_key_t *key,
    runtimec_compilation_repository_lease_t *output_lease,
    runtimec_execution_window_diagnostic_t *output_detail, runtimec_error_t *error) {
    vmkv_encode_request_t encode_request;
    binrwir_program_t *program = NULL;
    binrwir_compilation_t *compilation = NULL;
    runtimec_status_t status;

    encode_request.host_services = host;
    encode_request.function_path = request->function_path;
    encode_request.expected_snapshot = *snapshot;
    status = vmkv_encode(components->vmkv_encoder, &encode_request, &program,
                         error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_ENCODE_FAILED;
        return status;
    }
    /* 编译器接管 program：成败都由它处置，route 不再销毁。 */
    status = binrwir_compiler_compile(components->binrwir_compiler, program,
                                      &compilation, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_COMPILE_FAILED;
        return status;
    }
    status = runtimec_compilation_repository_insert(
        components->compilation_repository, key, compilation, output_lease,
        error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        runtimec_error_t ignored;
        runtimec_error_reset(&ignored);
        (void)binrwir_compilation_destroy(compilation, &ignored);
        *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_REPOSITORY_FAILED;
        return status;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

runtimec_status_t runtimec_execution_window_execute(
    runtimec_session_t *session, const runtimec_execution_window_request_t *request,
    runtimec_execution_window_result_t *output_result, runtimec_error_t *error) {
    const runtimec_host_services_t *host = NULL;
    runtimec_session_runtime_components_t components;
    runtimec_snapshot_t snapshot;
    runtimec_compilation_repository_key_t key;
    runtimec_compilation_repository_lease_t lease;
    runtimec_compilation_repository_lookup_result_t lookup_result =
        RUNTIMEC_COMPILATION_REPOSITORY_LOOKUP_INVALID;
    binrwir_compilation_view_t compilation_view;
    binrwir_program_view_t program_view;
    binrwir_frame_bindings_t *bindings = NULL;
    binrwir_execution_frame_t frame;
    binrwir_execution_exit_t execution_exit;
    runtimec_execution_window_diagnostic_t detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_INVALID_REQUEST;
    uint64_t binding_generation = BINRWIR_FRAME_BINDING_GENERATION_NONE;
    size_t binding_capacity = 0U;
    uint32_t instruction_identifier = BINRWIR_IDENTIFIER_NONE;
    bool lease_held = false;
    bool frame_ready = false;
    runtimec_status_t status;

    if (output_result == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    runtimec_execution_window_reset_result(output_result, RUNTIMEC_STATUS_INVALID,
                                (uint32_t)RUNTIMEC_EXECUTION_WINDOW_DIAG_INVALID_REQUEST);
    if (session == NULL || request == NULL || error == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    if (!runtimec_string_view_is_valid(request->function_path) ||
        request->function_path.size == 0U ||
        !runtimec_string_view_is_valid(request->vthread_identifier) ||
        request->vthread_identifier.size == 0U ||
        !runtimec_string_view_is_valid(request->frame_path) ||
        request->frame_path.size == 0U ||
        (request->argument_count != 0U && request->arguments == NULL)) {
        return runtimec_execution_window_fail(
            output_result, error, RUNTIMEC_STATUS_INVALID,
            RUNTIMEC_EXECUTION_WINDOW_DIAG_INVALID_REQUEST,
            "invalid execution window request");
    }
    if (runtimec_session_get_host_services(session, &host) !=
            RUNTIMEC_STATUS_SUCCESS ||
        host == NULL ||
        runtimec_session_get_runtime_components(session, &components) !=
            RUNTIMEC_STATUS_SUCCESS ||
        host->source_revision == NULL ||
        host->resolve_frame_xvalue_address == NULL ||
        host->ensure_frame == NULL ||
        host->checkpoint == NULL ||
        !runtimec_allocator_is_valid(&host->allocator)) {
        return runtimec_execution_window_fail(
            output_result, error, RUNTIMEC_STATUS_NOT_IMPLEMENTED,
            RUNTIMEC_EXECUTION_WINDOW_DIAG_MISSING_HOST_CAPABILITY,
            "host services lack a capability this route requires");
    }

    /* 1. 权威快照：source revision 是地址与 compilation 的共同世代。 */
    runtimec_snapshot_reset(&snapshot);
    snapshot.contract_version = runtimec_contract_version();
    snapshot.function_key =
        runtimec_key_view_make(request->function_path.data, request->function_path.size);
    status = host->source_revision(host->context, snapshot.function_key,
                                   &snapshot.source_revision, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_execution_window_fail(output_result, error, status,
                                   RUNTIMEC_EXECUTION_WINDOW_DIAG_SNAPSHOT_FAILED,
                                   "source revision is unavailable");
    }
    /* 零代数的快照不能作为缓存身份，也不能拿来绑地址。 */
    if (snapshot.source_revision.generation == 0U ||
        snapshot.source_revision.instance_identifier == 0U) {
        return runtimec_execution_window_fail(output_result, error,
                                   RUNTIMEC_STATUS_UNSUPPORTED,
                                   RUNTIMEC_EXECUTION_WINDOW_DIAG_SNAPSHOT_FAILED,
                                   "source revision is not addressable");
    }

    /* 2. 查编译仓库。 */
    memset(&key, 0, sizeof(key));
    key.snapshot = snapshot;
    key.rwir_manifest_version = VMKV_RWIR_MANIFEST_VERSION;
    key.binrwir_binary_interface_generation = BINRWIR_BINARY_INTERFACE_GENERATION;
    if (vmkv_encoder_get_canonical_encoding_revision(
            components.vmkv_encoder, &key.canonical_encoding_revision) !=
            RUNTIMEC_STATUS_SUCCESS ||
        binrwir_compiler_get_revision(components.binrwir_compiler,
                                      &key.compiler_revision) !=
            RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_execution_window_fail(output_result, error,
                                   RUNTIMEC_STATUS_INVALID,
                                   RUNTIMEC_EXECUTION_WINDOW_DIAG_REPOSITORY_FAILED,
                                   "component revision is unavailable");
    }
    memset(&lease, 0, sizeof(lease));
    status = runtimec_compilation_repository_find(
        components.compilation_repository, &key, &lookup_result, &lease, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_execution_window_fail(output_result, error, status,
                                   RUNTIMEC_EXECUTION_WINDOW_DIAG_REPOSITORY_FAILED,
                                   "compilation lookup failed");
    }
    if (lookup_result == RUNTIMEC_COMPILATION_REPOSITORY_LOOKUP_FOUND) {
        lease_held = true;
    } else if (lookup_result == RUNTIMEC_COMPILATION_REPOSITORY_LOOKUP_NOT_FOUND) {
        status = runtimec_execution_window_compile_and_insert(
            &components, host, request, &snapshot, &key, &lease, &detail, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return runtimec_execution_window_fail(output_result, error, status, detail,
                                       "compilation was not produced");
        }
        lease_held = true;
    } else {
        return runtimec_execution_window_fail(output_result, error, RUNTIMEC_STATUS_INVALID,
                                   RUNTIMEC_EXECUTION_WINDOW_DIAG_REPOSITORY_FAILED,
                                   "compilation lookup returned no verdict");
    }

    /* 3. 读执行目标：只读，不重新判定，不提供第二入口。 */
    status = binrwir_compilation_get_view(lease.compilation, &compilation_view,
                                          error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        output_result->status = status;
        goto release_lease;
    }
    /* 目标必须是两个已知取值之一：未知目标不是「那就跑 vmbin」。 */
    if (compilation_view.canonical_program == NULL ||
        (compilation_view.execution_target != BINRWIR_EXECUTION_TARGET_JIT &&
         compilation_view.execution_target != BINRWIR_EXECUTION_TARGET_VMBIN) ||
        (compilation_view.execution_target == BINRWIR_EXECUTION_TARGET_JIT &&
         compilation_view.binrwir_jit_code == NULL) ||
        (compilation_view.execution_target == BINRWIR_EXECUTION_TARGET_VMBIN &&
         compilation_view.binrwir_jit_code != NULL)) {
        status = runtimec_execution_window_fail(output_result, error,
                                     RUNTIMEC_STATUS_INVALID,
                                     RUNTIMEC_EXECUTION_WINDOW_DIAG_COMPILATION_INVALID,
                                     "compilation is not executable");
        goto release_lease;
    }
    if (binrwir_program_get_view(compilation_view.canonical_program,
                                 &program_view) != RUNTIMEC_STATUS_SUCCESS ||
        vmbin_frame_slot_capacity(compilation_view.canonical_program,
                                  &binding_capacity) !=
            RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_execution_window_fail(output_result, error,
                                     RUNTIMEC_STATUS_INVALID,
                                     RUNTIMEC_EXECUTION_WINDOW_DIAG_FRAME_FAILED,
                                     "program view is unavailable");
        goto release_lease;
    }

    /* 4. 顶层帧的形参绑定：代数取解析那一刻的真实存储版本，必须非零。 */
    if (binrwir_frame_bindings_create(&host->allocator,
                                      binding_capacity == 0U ? 1U
                                                             : binding_capacity,
                                      &bindings) != RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_execution_window_fail(output_result, error,
                                     RUNTIMEC_STATUS_OUT_OF_MEMORY,
                                     RUNTIMEC_EXECUTION_WINDOW_DIAG_OUT_OF_MEMORY,
                                     "frame bindings allocation failed");
        goto release_lease;
    }
    /*
     * 建帧先于一切：帧目录必须先存在，形参与 `‥pc` 的父路径才有着落，而
     * 「这一帧在跑哪个函数」的事实（`‥lib`）也只有这里写。失败即整体失败，
     * 一条指令都不执行。
     */
    status = host->ensure_frame(
        host->context, &snapshot,
        runtimec_key_view_make(request->frame_path.data,
                               request->frame_path.size),
        request->function_path, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_execution_window_fail(
            output_result, error, status,
            RUNTIMEC_EXECUTION_WINDOW_DIAG_FRAME_ENSURE_FAILED,
            "frame could not be established");
        goto release_bindings;
    }
    status = runtimec_execution_window_bind_parameters(
        host, &snapshot, compilation_view.canonical_program, request, bindings,
        &binding_generation, &detail, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_execution_window_fail(output_result, error, status, detail,
                                     "frame parameter binding failed");
        goto release_bindings;
    }
    if (binding_generation == BINRWIR_FRAME_BINDING_GENERATION_NONE) {
        status = runtimec_execution_window_fail(output_result, error,
                                     RUNTIMEC_STATUS_INVALID,
                                     RUNTIMEC_EXECUTION_WINDOW_DIAG_BINDING_FAILED,
                                     "frame binding has no storage generation");
        goto release_bindings;
    }
    if (runtimec_execution_window_find_instruction(
            compilation_view.canonical_program,
            request->source_logical_program_counter, &instruction_identifier,
            &detail) != RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_execution_window_fail(
            output_result, error, RUNTIMEC_STATUS_INVALID, detail,
            "source program counter is not an instruction of this program");
        goto release_bindings;
    }
    if (binrwir_execution_frame_initialize(
            &frame, compilation_view.canonical_program, NULL, binding_generation,
            program_view.identity.program_generation,
            instruction_identifier) != RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_execution_window_fail(output_result, error,
                                     RUNTIMEC_STATUS_INVALID,
                                     RUNTIMEC_EXECUTION_WINDOW_DIAG_FRAME_FAILED,
                                     "execution frame initialization failed");
        goto release_bindings;
    }
    frame.bindings = bindings;
    frame_ready = true;

    /* 5. 单次分派：目标已定，失败也不换入口。 */
    memset(&execution_exit, 0, sizeof(execution_exit));
    if (compilation_view.execution_target == BINRWIR_EXECUTION_TARGET_JIT) {
        status = binrwir_jit_execute(compilation_view.binrwir_jit_code, &frame,
                                     &execution_exit, error);
    } else {
        status = vmbin_execute(compilation_view.canonical_program,
                               compilation_view.execution_plan, &frame,
                               components.builtin_registry, NULL,
                               &execution_exit);
    }
    output_result->execution_exit = execution_exit;
    output_result->status = execution_exit.status;
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        /* 所选入口失败就沿这条路径上报，不切换到另一个执行器。 */
        output_result->status = status;
        output_result->execution_exit.status = status;
        goto release_frame;
    }

    /* 6. 提交：只有写已落存储的完成边界才有 checkpoint。 */
    status = runtimec_execution_window_commit_checkpoint(session, host, &snapshot, request,
                                              &execution_exit, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        output_result->status = status;
        output_result->execution_exit.status = status;
        goto release_frame;
    }
    status = RUNTIMEC_STATUS_SUCCESS;

release_frame:
    if (frame_ready) {
        (void)binrwir_execution_frame_destroy(&frame);
    }
release_bindings:
    if (bindings != NULL) {
        (void)binrwir_frame_bindings_destroy(bindings);
    }
release_lease:
    if (lease_held) {
        (void)runtimec_compilation_repository_release(
            components.compilation_repository, &lease);
    }
    return status;
}
