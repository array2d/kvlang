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
#include "binrwir/specialize.h"
#include "binrwir_compiler/compiler.h"
#include "binrwir_compiler/jit/executor.h"
#include "contract/checkpoint.h"
#include "engine/compilation_repository.h"
#include "engine/session.h"
#include "vmbin/frame_slot.h"
#include "vmbin/interpreter.h"
#include "vmkv/encoder.h"
#include "vmkv/rwir.h"
#include "vmkv/rwir_manifest.h"

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
    RUNTIMEC_EXECUTION_WINDOW_DIAG_LOCAL_FAILED = 16,
    RUNTIMEC_EXECUTION_WINDOW_DIAG_OUT_OF_MEMORY = 17,
    /* 实参描述的 key 不合法（空、不可解析）。 */
    RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_KEY_INVALID = 18,
    /* 字面量描述缺 bytes 或 langtype。 */
    RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_LITERAL_INVALID = 19,
    /* 物化本身失败（写不进去、key 放不下）。 */
    RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_MATERIALIZE_FAILED = 20,
    /*
     * 特化失败（编码成功之后、编译之前）。
     *
     * **必须与 5（编码失败）分开**：一个码两个成因，看日志的人就只能靠第二个通道
     * （比如「runner 有没有打 encode 的 message」）去分诊，而那个通道一换就失效，
     * 失效时还不报错。诊断码本身要说得出「死在哪一步」。
     */
    RUNTIMEC_EXECUTION_WINDOW_DIAG_SPECIALIZE_FAILED = 21
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
    /*
     * **下层带上来具体消息时，上层不许用通用消息覆盖它。**
     * 诊断信息的价值沿调用链递减：上层只知道「这一步失败了」，下层才知道
     * 「为什么失败」。把编译器的「某条指令无法编码」换成「编译没产出」，
     * 等于把根因擦掉，只留一个数字。
     *
     * `detail` 仍然写进 result 的执行退出——那是本层的分类，与 error 的
     * message 不冲突：分类回答「哪一步」，消息回答「为什么」。
     *
     * 前提：execute 入口已 reset error，因此这里的非空消息一定来自本次调用的
     * 下游，不会是上一次调用的残留。
     */
    if (error != NULL && error->message.size != 0U) {
        error->status = status;
        return status;
    }
    runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT, detail,
                       runtimec_string_view_from_c_string(message),
                       RUNTIMEC_PROGRAM_COUNTER_NONE);
    return status;
}

/*
 * `vid` 是**数字**（spec `02-kvspace模型/02-结构域.kv`「`vid` 是数字，如
 * `/vthread/2`，不是名字」）。这里拦的是「空」和「非数字」，不拦范围——位数由
 * 调用方定，本层不发明上限。
 *
 * 为什么必须在这里拦：`vthread_identifier` 会被原样拼进 checkpoint 的落点，
 * `"v1"`、`"vthread-1"` 这类串不会当场报错，只会在 kvspace 里长出一棵语法上
 * 合法但语义上不存在的子树——错的地方离出错的地方太远。
 */
static bool runtimec_execution_window_vthread_is_numeric(
    runtimec_string_view_t vthread_identifier) {
    size_t ordinal;

    if (vthread_identifier.size == 0U) {
        return false;
    }
    for (ordinal = 0U; ordinal < vthread_identifier.size; ++ordinal) {
        const char character = vthread_identifier.data[ordinal];

        if (character < '0' || character > '9') {
            return false;
        }
    }
    return true;
}

/*
 * `‥pc` 现在是两个整数，不是路径文本：`{stack_identifier, s0}`。
 * 线程身份由存放路径给出（`/vthread/{vid}/‥pc·*`），函数身份由该帧的 `‥lib`
 * 给出——三样各归其位，pc 里不再编码任何文本。
 *
 * 本阶段执行窗口一次调用只跑一个窗口，没有跨调用的帧栈，所以 stackid 由请求
 * 给出（`frame_path` 里的帧坐标即其数值来源由调用方负责，本层不解析文本）。
 */
static runtimec_program_counter_t runtimec_execution_window_make_program_counter(
    int32_t stack_identifier, uint32_t source_program_counter) {
    runtimec_program_counter_t counter;

    counter.stack_identifier = stack_identifier;
    counter.source_instruction_ordinal =
        source_program_counter > (uint32_t)INT32_MAX
            ? RUNTIMEC_PROGRAM_COUNTER_ORDINAL_NONE
            : (int32_t)source_program_counter;
    return counter;
}

/* 解析一个绝对槽键并写进绑定表；代数必须与本次快照一致且非零。 */
static runtimec_status_t runtimec_execution_window_bind_slot(
    runtimec_kvspace_access_t *host, const runtimec_snapshot_t *snapshot,
    binrwir_frame_bindings_t *bindings, uint16_t binding_slot,
    runtimec_string_view_t key_text, uint64_t *generation_in_out,
    runtimec_execution_window_diagnostic_t *output_detail, runtimec_error_t *error) {
    runtimec_xvalue_address_t xvalue_address = RUNTIMEC_XVALUE_ADDRESS_NONE;
    uint64_t generation = BINRWIR_FRAME_BINDING_GENERATION_NONE;
    const runtimec_key_view_t slot_key =
        runtimec_key_view_make(key_text.data, key_text.size);
    runtimec_status_t status =
        runtimec_kvspace_access_resolve_frame_xvalue_address(
            host, snapshot, slot_key, &xvalue_address, &generation, error);

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
 * 把一个实参**描述**物化成可以绑定的绝对槽键。三种来源各自的处置：
 *
 *   EXISTING_KEY  —— 原样返回，本层不创建、不校验存在性（绑定那步会失败封闭）；
 *   LITERAL       —— 写到被调帧的 `._lit<i>`。**命名归本层**：调用方若也拼这个名字，
 *                    就成了两处维护同一个规则，不同步的症状是「实参找不到」；
 *   WRITE_TARGET  —— 经 `ensure_zero_value` 创建（唯一创建入口），再原样返回。
 *
 * **必须先物化、后取快照**：物化的写会让地址重定位、`source_revision` 前进。
 * 先取快照的话，拿到的世代在物化之后就失效——实测症状是
 * `vmkv encoder: function source revision differs from the request`。
 *
 * 输出 key 借用自 `scratch`（调用方给的足够大的缓冲数组），生存期到本次绑定结束。
 */
static runtimec_status_t runtimec_execution_window_materialize_argument(
    runtimec_kvspace_access_t *host, const runtimec_snapshot_t *snapshot,
    const runtimec_execution_window_request_t *request,
    const runtimec_argument_description_t *description, size_t ordinal,
    char *scratch, size_t scratch_capacity, runtimec_string_view_t *output_key,
    runtimec_execution_window_diagnostic_t *output_detail,
    runtimec_error_t *error) {
    *output_key = runtimec_string_view_make(NULL, 0U);
    switch (description->source) {
    case RUNTIMEC_ARGUMENT_SOURCE_EXISTING_KEY:
        if (!runtimec_string_view_is_valid(description->key) ||
            description->key.size == 0U) {
            *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_KEY_INVALID;
            return RUNTIMEC_STATUS_INVALID;
        }
        *output_key = description->key;
        return RUNTIMEC_STATUS_SUCCESS;
    case RUNTIMEC_ARGUMENT_SOURCE_LITERAL: {
        if (!runtimec_string_view_is_valid(description->literal_langtype) ||
            description->literal_langtype.size == 0U ||
            description->literal_bytes.data == NULL) {
            *output_detail =
                RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_LITERAL_INVALID;
            return RUNTIMEC_STATUS_INVALID;
        }
        /* 名字归本层：`<frame_path>/._lit<序号>`，序号是**实参序号**（从 0 起），
         * 不是 read_arguments 的下标——FUNCTION 引用占第 0 位，两者不等。 */
        char langtype_text[128];
        const int langtype_length =
            snprintf(langtype_text, sizeof(langtype_text), "%.*s",
                     (int)description->literal_langtype.size,
                     description->literal_langtype.data);
        if (langtype_length < 0 ||
            (size_t)langtype_length >= sizeof(langtype_text)) {
            *output_detail =
                RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_LITERAL_INVALID;
            return RUNTIMEC_STATUS_INVALID;
        }
        const int key_length =
            snprintf(scratch, scratch_capacity, "%.*s/._lit%zu",
                     (int)request->frame_path.size, request->frame_path.data,
                     ordinal);
        if (key_length < 0 || (size_t)key_length >= scratch_capacity) {
            *output_detail =
                RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_MATERIALIZE_FAILED;
            return RUNTIMEC_STATUS_INVALID;
        }
        /* 用现有的 `host->write`——**没有 `ensure_literal` 这种能力，
         * 不要发明**。`write` 的 value 是整条 wire（XValue head + body + attr），
         * 而 `literal_bytes` 正是那个形状（`binrwir/argument.h` 的 LITERAL 约定）。 */
        if (runtimec_kvspace_access_write(
                host, snapshot,
                        runtimec_key_view_make(scratch, strlen(scratch)),
                        runtimec_bytes_view_make(description->literal_bytes.data,
                                                 description->literal_bytes.size),
                        error) != RUNTIMEC_STATUS_SUCCESS) {
            *output_detail =
                RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_MATERIALIZE_FAILED;
            return RUNTIMEC_STATUS_INVALID;
        }
        *output_key = runtimec_string_view_from_c_string(scratch);
        return RUNTIMEC_STATUS_SUCCESS;
    }
    case RUNTIMEC_ARGUMENT_SOURCE_WRITE_TARGET: {
        if (!runtimec_string_view_is_valid(description->key) ||
            description->key.size == 0U ||
            !runtimec_string_view_is_valid(description->literal_langtype) ||
            description->literal_langtype.size == 0U) {
            *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_KEY_INVALID;
            return RUNTIMEC_STATUS_INVALID;
        }
        /* **唯一创建入口**：`ensure_zero_value`。驱动不许直接 `set_value`——
         * 绕过会让地址重定位而 `source_revision` 不跟随，绑定期就失配。 */
        if (runtimec_kvspace_access_ensure_zero_value(
                host, snapshot,
                                    runtimec_key_view_make(description->key.data,
                                                           description->key.size),
                                    description->literal_langtype,
                                    error) != RUNTIMEC_STATUS_SUCCESS) {
            *output_detail =
                RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_MATERIALIZE_FAILED;
            return RUNTIMEC_STATUS_INVALID;
        }
        *output_key = description->key;
        return RUNTIMEC_STATUS_SUCCESS;
    }
    default:
        *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_ARGUMENT_KEY_INVALID;
        return RUNTIMEC_STATUS_INVALID;
    }
}

/*
 * 顶层帧的形参绑定。槽号分区与 vmbin/frame_slot 完全一致（读参 0..nr-1、
 * 写参 nr..nr+nw-1），实参是调用方给的绝对槽键，route 不拼不猜。
 *
 * **局部不在这里绑**，但**不是不绑**：局部槽由
 * `runtimec_execution_window_materialize_locals`（本文件 `:482`）在建帧时
 * 拼 `frame_path + "/" + name`（`:573-584`）并 `ensure_zero_value` 后立即绑定，
 * 调用点在 `:953`，早于本函数（`:962`）。本函数只处理**实参**。
 *
 * 早先这里写的是「route 没有任何事实源能推出那个前缀」——那是实现之前的话，
 * 后来做了没更新。它让人（包括写它的人）以为局部未支持，而真相相反。
 * 引用注释当结论前，先看它下面 300 行内有没有实现。
 */
static runtimec_status_t runtimec_execution_window_bind_parameters(
    runtimec_kvspace_access_t *host, const runtimec_snapshot_t *snapshot,
    const binrwir_program_t *program, const runtimec_execution_window_request_t *request,
    binrwir_frame_bindings_t *bindings, uint64_t *output_generation,
    runtimec_execution_window_diagnostic_t *output_detail, runtimec_error_t *error) {
    binrwir_program_view_t program_view;
    size_t read_count;
    size_t write_count;
    size_t ordinal;
    /* 物化后的 key 文本。放本栈上：`bind_slot` 只在本函数内借用。
     * 每个 key 上限 256——`/vthread/<vid>/[<depth>]/<name>` 的实测长度远小于此。 */
    char materialized_keys[64][256];

    /*
     * 「这一帧的绑定已建立」与参数个数无关：**空绑定也是一次绑定**。
     *
     * generation 的事实源是快照的 source revision —— 它回答「这一帧依据哪份存储」，
     * 而不是「从哪个实参解析出来的」。零参数函数没有任何实参可解析，若因此就让
     * generation 留在 NONE，检查点会把「绑定已建立」误判成「没建绑定」，
     * `rwfunc test() -> ()` 这种每条用例的入口形状就永远执行不了。
     *
     * 先记快照世代；有实参时下面会被解析到的世代覆盖（并校验一致）。
     */
    *output_generation = snapshot->source_revision.generation;
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
        runtimec_string_view_t resolved_key;
        runtimec_status_t status = runtimec_execution_window_materialize_argument(
            host, snapshot, request, &request->arguments[ordinal], ordinal,
            materialized_keys[ordinal], sizeof(materialized_keys[ordinal]),
            &resolved_key, output_detail, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
        status = runtimec_execution_window_bind_slot(
            host, snapshot, bindings, binding_slot, resolved_key,
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
        runtimec_string_view_t resolved_key;
        runtimec_status_t status = runtimec_execution_window_materialize_argument(
            host, snapshot, request,
            &request->arguments[read_count + ordinal], read_count + ordinal,
            materialized_keys[read_count + ordinal],
            sizeof(materialized_keys[read_count + ordinal]), &resolved_key,
            output_detail, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
        status = runtimec_execution_window_bind_slot(
            host, snapshot, bindings, binding_slot, resolved_key,
            output_generation, output_detail, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return status;
        }
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * FOREIGN 指令的**算子名**。
 *
 * FOREIGN（`0xFFFE`）是结构标记，**不携带任何信息**（`binrwir/instruction.h:28-37`：
 * 「不执行任何操作、不携带操作数、不写存储」），所以名字不能从它身上取。
 *
 * 但它的 `source_logical_program_counter` 指向 layout 写下的源槽
 * `<function_path>/[<pc>,0]`，**那个槽的 body 里有名字**（counts 头 + 文本）。
 * handoff 请求的 `operation` 字段就填它。
 *
 * **不新增依赖**：本函数本来就调 `vmkv_encode` 读同一个 store（编译仓库未命中时），
 * 这里只是再多读一个槽。
 *
 * **解析归 vmkv**：counts 头的布局是 vmkv 的格式知识，由
 * `vmkv_rwir_operation_name_from_wire` 解析。本层只负责拼 key 与读字节——
 * 自己跳过 5 字节等于把格式复制一份到上层，而那份复制不会随格式变化更新。
 */
static runtimec_status_t runtimec_execution_window_operation_name(
    runtimec_kvspace_access_t *host, const runtimec_snapshot_t *snapshot,
    const char *function_path, uint32_t source_program_counter,
    char *output_key, size_t key_capacity,
    runtimec_string_view_t *output_operation, runtimec_error_t *error) {
    *output_operation = runtimec_string_view_make(NULL, 0U);
    const int key_length = snprintf(output_key, key_capacity, "%s/[%u,0]",
                                    function_path, source_program_counter);
    if (key_length < 0 || (size_t)key_length >= key_capacity) {
        return RUNTIMEC_STATUS_INVALID;
    }
    /* 读回来的 wire = 16 字节 head + body。取出 body 段交给 vmkv 解析。 */
    uint8_t storage[512];
    runtimec_bytes_buffer_t buffer;
    memset(&buffer, 0, sizeof(buffer));
    buffer.data = storage;
    buffer.capacity = sizeof(storage);
    runtimec_status_t status = runtimec_kvspace_access_read(
        host, snapshot,
        runtimec_key_view_make(output_key, (size_t)key_length), &buffer, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    /* 整个 wire 交给 vmkv 解析：**body 偏移归 contract，切分归 vmkv**。
     * 本层只是把读回来的字节原样递过去，不写任何偏移算术——
     * 门禁 `body-address-derivation-outside-contract` 正是拦这个。 */
    return vmkv_rwir_operation_name_from_wire(buffer.data, buffer.size,
                                              output_operation, error);
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
    runtimec_session_t *session, runtimec_kvspace_access_t *host,
    const runtimec_snapshot_t *snapshot,
    const runtimec_execution_window_request_t *request,
    const binrwir_execution_exit_t *execution_exit, runtimec_error_t *error) {
    runtimec_checkpoint_t checkpoint;
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
                           RUNTIMEC_PROGRAM_COUNTER_NONE);
        return RUNTIMEC_STATUS_INVALID;
    }
    runtimec_checkpoint_reset(&checkpoint);
    checkpoint.contract_version = snapshot->contract_version;
    checkpoint.source_revision = snapshot->source_revision;
    status = runtimec_session_take_checkpoint_sequence(session,
                                                       &checkpoint.sequence);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    /* runtime/frame 标识本阶段留空：适配层不读，填一个编出来的值是第二事实。 */
    checkpoint.vthread_identifier = request->vthread_identifier;
    checkpoint.logical_program_counter =
        runtimec_execution_window_make_program_counter(
            request->stack_identifier, source_program_counter);
    checkpoint.persistent_status =
        runtimec_execution_window_persistent_status(execution_exit->kind);
    checkpoint.outcome = execution_exit->status;
    return runtimec_kvspace_access_checkpoint(host, &checkpoint, error);
}

/*
 * 把函数用到的局部变量物化成**真实存在的零值 key**，逐个落在帧目录下：
 *
 *     /vthread/{vid}/{stackid}/{name}
 *
 * **不加 `local/` 前缀**：旧 runtime 与 runtimec2 都没有这一层，帧目录下就是
 * 局部名本身。
 *
 * 为什么要物化：执行器只接受「与当前 body 等长」的写入，而不存在的 key 连地址
 * 都解析不出来。所以局部必须先以正确宽度存在，宽度由符号池里的 langtype 给出。
 *
 * 符号池不记 kind（条目只有 name + langtype），所以「哪些是局部」只能从指令
 * 推导：扫读参与写参、收集 `REFERENCE_LOCAL` 的符号下标去重。**不为此给符号池
 * 加 kind 字段**——为一次查询改数据模型，代价大于收益。
 *
 * 幂等交给 host：同宽已存在即成功，不重复写。失败即整体失败，一条指令都不执行。
 */
static runtimec_status_t runtimec_execution_window_materialize_locals(
    runtimec_kvspace_access_t *host, const runtimec_snapshot_t *snapshot,
    const binrwir_program_t *program,
    const runtimec_execution_window_request_t *request,
    binrwir_frame_bindings_t *bindings, size_t binding_capacity,
    uint64_t *generation_in_out,
    runtimec_execution_window_diagnostic_t *output_detail,
    runtimec_error_t *error) {
    binrwir_program_view_t program_view;
    size_t symbol_count = 0U;
    bool *is_local = NULL;
    size_t instruction_ordinal;
    size_t symbol_ordinal;
    runtimec_status_t status;

    if (binrwir_program_get_view(program, &program_view) !=
            RUNTIMEC_STATUS_SUCCESS ||
        binrwir_program_get_symbol_count(program, &symbol_count) !=
            RUNTIMEC_STATUS_SUCCESS) {
        *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_FRAME_FAILED;
        return RUNTIMEC_STATUS_INVALID;
    }
    /*
     * 帧的 generation 先取：它属于**帧**，不属于「恰好第一个被绑上的槽」。
     *
     * 若拿第一个绑成功的槽当来源，一个只有容器局部的帧就永远没有 generation
     * ——而容器恰恰是唯一由 handler 在执行期懒绑定的一类。同一个 generation
     * 后面还要写进 frame->snapshot_generation，供那些懒绑定使用。
     */
    status = runtimec_kvspace_access_frame_generation(
        host, snapshot, runtimec_key_view_make(request->frame_path.data,
                                               request->frame_path.size),
        generation_in_out, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_BINDING_FAILED;
        return status;
    }
    if (symbol_count == 0U) {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    is_local = (bool *)runtimec_allocator_allocate(
        &host->allocator, sizeof(*is_local) * symbol_count,
        _Alignof(bool));
    if (is_local == NULL) {
        *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_OUT_OF_MEMORY;
        return RUNTIMEC_STATUS_OUT_OF_MEMORY;
    }
    memset(is_local, 0, sizeof(*is_local) * symbol_count);

    /* 1. 扫全部指令的两个参数区，标记哪些符号被当作 LOCAL 引用过。 */
    for (instruction_ordinal = 0U;
         instruction_ordinal < program_view.instruction_count;
         ++instruction_ordinal) {
        binrwir_instruction_view_t instruction;
        binrwir_argument_span_t spans[2];
        size_t span_ordinal;

        if (binrwir_program_get_instruction(program,
                                            (uint32_t)instruction_ordinal,
                                            &instruction) !=
            RUNTIMEC_STATUS_SUCCESS) {
            (void)runtimec_allocator_deallocate(
                &host->allocator, is_local, sizeof(*is_local) * symbol_count,
                _Alignof(bool));
            *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_FRAME_FAILED;
            return RUNTIMEC_STATUS_INVALID;
        }
        spans[0] = instruction.read_arguments;
        spans[1] = instruction.write_arguments;
        for (span_ordinal = 0U; span_ordinal < 2U; ++span_ordinal) {
            size_t argument_ordinal;

            for (argument_ordinal = 0U;
                 argument_ordinal < spans[span_ordinal].count;
                 ++argument_ordinal) {
                const binrwir_argument_t *argument =
                    &spans[span_ordinal].data[argument_ordinal];

                if (argument->reference_kind == BINRWIR_REFERENCE_LOCAL &&
                    (size_t)argument->slot < symbol_count) {
                    is_local[argument->slot] = true;
                }
            }
        }
    }

    /* 2. 逐个物化：key = frame_path + "/" + name。 */
    for (symbol_ordinal = 0U; symbol_ordinal < symbol_count;
         ++symbol_ordinal) {
        runtimec_string_view_t name;
        runtimec_string_view_t langtype;
        char *key_text;
        size_t key_size;

        if (!is_local[symbol_ordinal]) {
            continue;
        }
        if (binrwir_program_get_symbol(program, (uint16_t)symbol_ordinal, &name,
                                       &langtype) != RUNTIMEC_STATUS_SUCCESS ||
            name.size == 0U || langtype.size == 0U) {
            (void)runtimec_allocator_deallocate(
                &host->allocator, is_local, sizeof(*is_local) * symbol_count,
                _Alignof(bool));
            *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_FRAME_FAILED;
            return RUNTIMEC_STATUS_INVALID;
        }
        /*
         * 容器/结构体局部**不建、不绑**：它们的创建源是源码里的 `·new`
         * （`struct·new` / `map·new` / `compactarray·new` …），创建型 handler
         * 建值后当场绑定。帧准备建它们既没有零值可写（长度由内容决定），
         * 又会制造一个「存储有、表里也有」但值不是源码要的那份的假状态。
         *
         * 在 `·new` 之前读它 ⇒ 绑定表里没有 ⇒ `UNBOUND_SLOT` 硬错。这是正确
         * 行为，不为它加兜底。
         */
        if (!runtimec_kvspace_access_langtype_has_fixed_zero_width(langtype)) {
            continue;
        }
        key_size = request->frame_path.size + 1U + name.size;
        key_text = (char *)runtimec_allocator_allocate(
            &host->allocator, key_size + 1U, 1U);
        if (key_text == NULL) {
            (void)runtimec_allocator_deallocate(
                &host->allocator, is_local, sizeof(*is_local) * symbol_count,
                _Alignof(bool));
            *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_OUT_OF_MEMORY;
            return RUNTIMEC_STATUS_OUT_OF_MEMORY;
        }
        memcpy(key_text, request->frame_path.data, request->frame_path.size);
        key_text[request->frame_path.size] = '/';
        memcpy(key_text + request->frame_path.size + 1U, name.data, name.size);
        key_text[key_size] = '\0';

        const runtimec_key_view_t slot_key =
            runtimec_key_view_make(key_text, key_size);
        status = runtimec_kvspace_access_ensure_zero_value(
            host, snapshot, slot_key,
                                         langtype, error);
        if (status == RUNTIMEC_STATUS_SUCCESS) {
            /*
             * 建出值只是「存储里有了」。执行期读它走的是绑定表——不绑就
             * UNBOUND_SLOT。绑定必须紧接在建值之后：中间若被别的路径插进来，
             * 就会出现「存储有、表里没有」的半状态。
             *
             * 槽号是 frame_slot 的分区：读参 + 写参 + 符号下标。
             */
            runtimec_xvalue_address_t xvalue_address =
                RUNTIMEC_XVALUE_ADDRESS_NONE;
            uint64_t generation = BINRWIR_FRAME_BINDING_GENERATION_NONE;
            /*
             * 槽号经 `vmbin/frame_slot` 换算，**不在这里手写加法**：
             * 同一个换算若出现两处，一处改了另一处忘改就是错位绑定，而错位
             * 绑定不会当场报错，只会让值写进别的槽。
             */
            uint16_t binding_slot = BINRWIR_FRAME_BINDING_SLOT_NONE;

            status = vmbin_frame_slot_of_local_symbol(
                program, (uint16_t)symbol_ordinal, &binding_slot);
            if (status != RUNTIMEC_STATUS_SUCCESS) {
                *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_LOCAL_FAILED;
            }
            if (status == RUNTIMEC_STATUS_SUCCESS) {
                status =
                    runtimec_kvspace_access_resolve_frame_xvalue_address(
                        host, snapshot, slot_key, &xvalue_address,
                    &generation, error);
            }
            if (status == RUNTIMEC_STATUS_SUCCESS) {
                if (xvalue_address == RUNTIMEC_XVALUE_ADDRESS_NONE ||
                    generation == BINRWIR_FRAME_BINDING_GENERATION_NONE ||
                    binding_slot >= binding_capacity) {
                    *output_detail =
                        RUNTIMEC_EXECUTION_WINDOW_DIAG_LOCAL_FAILED;
                    status = RUNTIMEC_STATUS_INVALID;
                } else if (*generation_in_out != generation) {
                    /* 绑到一半存储换代：这一帧的局部不再属于同一份快照。 */
                    *output_detail =
                        RUNTIMEC_EXECUTION_WINDOW_DIAG_LOCAL_FAILED;
                    status = RUNTIMEC_STATUS_ERROR;
                }
            }
            if (status == RUNTIMEC_STATUS_SUCCESS &&
                binrwir_frame_bindings_bind(bindings, binding_slot, slot_key,
                                            slot_key, xvalue_address,
                                            generation) !=
                    RUNTIMEC_STATUS_SUCCESS) {
                *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_LOCAL_FAILED;
                status = RUNTIMEC_STATUS_INVALID;
            }
        }
        (void)runtimec_allocator_deallocate(&host->allocator, key_text,
                                            key_size + 1U, 1U);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            (void)runtimec_allocator_deallocate(
                &host->allocator, is_local, sizeof(*is_local) * symbol_count,
                _Alignof(bool));
            *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_LOCAL_FAILED;
            return status;
        }
    }
    (void)runtimec_allocator_deallocate(&host->allocator, is_local,
                                        sizeof(*is_local) * symbol_count,
                                        _Alignof(bool));
    return RUNTIMEC_STATUS_SUCCESS;
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
    runtimec_kvspace_access_t *host, const runtimec_execution_window_request_t *request,
    const runtimec_snapshot_t *snapshot,
    const runtimec_compilation_repository_key_t *key,
    runtimec_compilation_repository_lease_t *output_lease,
    runtimec_execution_window_diagnostic_t *output_detail, runtimec_error_t *error) {
    vmkv_encode_request_t encode_request;
    binrwir_program_t *program = NULL;
    binrwir_compilation_t *compilation = NULL;
    runtimec_status_t status;

    encode_request.kvspace_access = host;
    encode_request.function_path = request->function_path;
    encode_request.expected_snapshot = *snapshot;
    status = vmkv_encode(components->vmkv_encoder, &encode_request, &program,
                         error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_ENCODE_FAILED;
        return status;
    }
    /*
     * 特化：把通用算子按操作数的静态类型组合改写成特化 opid。
     *
     * **位置**：编码产出 Program 之后、编译之前——编译器要看到最终的 opid，
     * 特化放在编译之后就晚了。
     *
     * **per Program 一次**：这条路径只在 `compilation_repository` 未命中时走到
     * （见调用方），命中时直接用缓存的那份 compilation。所以特化天然是
     * 「每份 Program 一次」，绝不放进 `vmbin_execute`——那里每条指令执行前都会
     * 走到，复制一份 Program 的代价与其大小成正比。
     *
     * **失败不回退**：特化失败同样让本次执行失败，没有「回落到未特化」这条路——
     * 半特化的 Program 比不特化更糟：一部分 opid 改了、一部分没改，而注册表只认
     * 其中一套。失败本身与编码失败同样是**失败封闭**，但**诊断码分开**（21），
     * 这样日志能直接说出死在特化而不是编码。
     */
    {
        binrwir_program_t *specialized = NULL;

        status = binrwir_program_specialize(program, &host->allocator,
                                            &specialized, error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            (void)binrwir_program_destroy(program);
            *output_detail = RUNTIMEC_EXECUTION_WINDOW_DIAG_SPECIALIZE_FAILED;
            return status;
        }
        /* 特化产新 Program，旧的那份由本层销毁——它是 `vmkv_encode` 给的，
         * 还没交给编译器，所有权仍在这里。 */
        (void)binrwir_program_destroy(program);
        program = specialized;
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
    runtimec_kvspace_access_t *host = NULL;
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
    /* 调用方给的 kvspace 句柄，原样透传给执行期；本层不连接、不关闭、不解释。 */
    void *kvspace_handle = NULL;
    runtimec_status_t status;

    if (output_result == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    runtimec_execution_window_reset_result(output_result, RUNTIMEC_STATUS_INVALID,
                                (uint32_t)RUNTIMEC_EXECUTION_WINDOW_DIAG_INVALID_REQUEST);
    if (session == NULL || request == NULL || error == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    /* 入口清空 error：`fail` 依据「消息非空」判断是否保留下层诊断，陈旧消息
     * 会让一次全新的失败被误认成「下层已经说过了」。 */
    runtimec_error_reset(error);
    if (!runtimec_string_view_is_valid(request->function_path) ||
        request->function_path.size == 0U ||
        !runtimec_string_view_is_valid(request->vthread_identifier) ||
        !runtimec_execution_window_vthread_is_numeric(
            request->vthread_identifier) ||
        !runtimec_string_view_is_valid(request->frame_path) ||
        request->frame_path.size == 0U ||
        (request->argument_count != 0U && request->arguments == NULL)) {
        return runtimec_execution_window_fail(
            output_result, error, RUNTIMEC_STATUS_INVALID,
            RUNTIMEC_EXECUTION_WINDOW_DIAG_INVALID_REQUEST,
            "invalid execution window request");
    }
    if (runtimec_session_get_kvspace_access(session, &host) !=
            RUNTIMEC_STATUS_SUCCESS ||
        host == NULL ||
        runtimec_session_get_runtime_components(session, &components) !=
            RUNTIMEC_STATUS_SUCCESS ||
        !runtimec_allocator_is_valid(&host->allocator)) {
        return runtimec_execution_window_fail(
            output_result, error, RUNTIMEC_STATUS_NOT_IMPLEMENTED,
            RUNTIMEC_EXECUTION_WINDOW_DIAG_MISSING_HOST_CAPABILITY,
            "host services lack a capability this route requires");
    }
    /*
     * kvspace 句柄**可以为 NULL**：不接存储的 session（多数单测）走到底都不碰
     * 存储，硬要求它非空会让这些用例全部失败。需要存储的 handler 自己在拿到
     * NULL 时失败封闭——那才是「谁需要谁检查」。
     */
    if (runtimec_session_get_kvspace_handle(session, &kvspace_handle) !=
        RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_execution_window_fail(
            output_result, error, RUNTIMEC_STATUS_INVALID,
            RUNTIMEC_EXECUTION_WINDOW_DIAG_INVALID_REQUEST,
            "cannot read the kvspace handle from the session");
    }

    /* 1. 权威快照：source revision 是地址与 compilation 的共同世代。 */
    runtimec_snapshot_reset(&snapshot);
    snapshot.contract_version = runtimec_contract_version();
    snapshot.function_key =
        runtimec_key_view_make(request->function_path.data, request->function_path.size);
    status = runtimec_kvspace_access_source_revision(
        host, snapshot.function_key,
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
    output_result->program = compilation_view.canonical_program;
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
    status = runtimec_kvspace_access_ensure_frame(
        host, &snapshot,
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
    status = runtimec_execution_window_materialize_locals(
        host, &snapshot, compilation_view.canonical_program, request, bindings,
        binding_capacity, &binding_generation, &detail, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_execution_window_fail(
            output_result, error, status, detail,
            "local variables could not be materialized");
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
    /*
     * 值访问写路径需要 host 才能完成两件事：按 langtype 创建尚不存在的写目标
     * （复用 `ensure_zero_value`，零值规则的唯一实现），以及执行期解析 STATIC
     * 且 payload 为 NONE 的写目标。帧只借用这两样，不拥有。
     */
    frame.kvspace_access = host;
    frame.snapshot = snapshot;
    /*
     * 帧路径原样借用——不重新拼接。重新拼就是第二份地址来源，一旦与请求里
     * 那份不一致，事后无法判断哪一份才是调用方要的。借用视图，不拷贝：
     * 字节归请求所有，本次调用内有效。
     */
    frame.frame_path = runtimec_key_view_make(request->frame_path.data,
                                              request->frame_path.size);
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
                               components.builtin_registry, kvspace_handle, NULL,
                               &execution_exit);
    }
    output_result->execution_exit = execution_exit;
    output_result->status = execution_exit.status;
    /*
     * 撞到 FOREIGN 就地停下（`EXIT_HANDOFF`）时，**把活挂进队列**。
     *
     * 不挂的话，`‥status` 写成 `waiting` 就返回了，而后台没有任何认领方——
     * 调用方等一个永远不来的结果，且现象是「执行成功但什么都没发生」。
     *
     * 顺序要紧：**先提交 checkpoint（把 pc 落到 `‥pc`），再挂 handoff**。
     * 反过来的话，扩展可能在 pc 还没落盘时就认领，从旧 pc 续跑。
     */
    if (status == RUNTIMEC_STATUS_SUCCESS &&
        execution_exit.kind == BINRWIR_EXECUTION_EXIT_HANDOFF) {
        char function_path_text[512];
        char operation_key[512];
        runtimec_string_view_t operation;
        const int function_path_length =
            snprintf(function_path_text, sizeof(function_path_text), "%.*s",
                     (int)request->function_path.size,
                     request->function_path.data);
        if (function_path_length < 0 ||
            (size_t)function_path_length >= sizeof(function_path_text)) {
            status = RUNTIMEC_STATUS_INVALID;
        } else {
            status = runtimec_execution_window_operation_name(
                host, &snapshot, function_path_text,
                execution_exit.logical_program_counter, operation_key,
                sizeof(operation_key), &operation, error);
        }
        if (status == RUNTIMEC_STATUS_SUCCESS) {
            runtimec_handoff_request_t handoff_request;
            memset(&handoff_request, 0, sizeof(handoff_request));
            handoff_request.operation = operation;
            runtimec_checkpoint_reset(&handoff_request.checkpoint);
            handoff_request.checkpoint.source_revision = snapshot.source_revision;
            handoff_request.checkpoint.vthread_identifier =
                request->vthread_identifier;
            handoff_request.checkpoint.frame_identifier = request->frame_path;
            handoff_request.checkpoint.persistent_status =
                runtimec_execution_window_persistent_status(execution_exit.kind);
            handoff_request.checkpoint.outcome = execution_exit.status;
            status = runtimec_kvspace_access_handoff(
                host, &handoff_request, error);
        }
        /* 挂不进去就把这次失败**当执行失败上报**：停在 waiting 却没进队列，
         * 与「跑完了」无法区分，那是「以看起来合理的值失败」。 */
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            output_result->status = status;
            output_result->execution_exit.status = status;
        }
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        /* 所选入口失败就沿这条路径上报，不切换到另一个执行器。
         *
         * **必须填 error**：这条路径不经过 `runtimec_execution_window_fail`，
         * 而 `execute` 入口已 reset error——不填的话调用方只拿到一个状态码，
         * 退出明细（detail）与出错位置（pc）全丢。诊断码取执行退出里的
         * detail，位置取该条指令的 source pc。 */
        output_result->status = status;
        output_result->execution_exit.status = status;
        if (error != NULL && error->message.size == 0U) {
            /*
             * 跨枚举复用数值时必须在人可见处标明来源：`detail_code` 是
             * **解释器**的诊断码，而 `RUNTIMEC_ERROR_DOMAIN_CONTRACT` 里也有
             * 一个同值的窗口诊断码（两者都可能是 6）。不写来源，读的人会把
             * `UNRESOLVED_TARGET` 读成 `COMPILE_FAILED`——今天真实发生过。
             *
             * 用线程局部静态缓冲承载带数值的消息：借用视图不会悬空，也不分配。
             *
             * **借用语义的显式化**：该缓冲是「最近一次失败」的消息，下一次失败会
             * 覆盖它。error 只在本次调用内有效——把 error 存起来延后读取，会读到
             * 后一次失败的话。需要跨调用持有就必须自己拷贝。
             *
             * 数值取自 `execution_exit.detail_code`：它可能是解释器诊断（1–10），
             * 也可能是 handler 自己的诊断码（0x4xxx）。消息写「execution exit
             * detail」而不是「interpreter diagnostic」，避免把 handler 码也标错来源。
             */
            static _Thread_local char execution_message[96];
            (void)snprintf(execution_message, sizeof(execution_message),
                           "instruction execution failed (execution exit detail=%u)",
                           (unsigned)execution_exit.detail_code);
            runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                               (int64_t)execution_exit.detail_code,
                               runtimec_string_view_from_c_string(
                                   execution_message),
                               (runtimec_program_counter_t){
                                   .stack_identifier =
                                       (int32_t)request->stack_identifier,
                                   .source_instruction_ordinal =
                                       (int32_t)execution_exit
                                           .logical_program_counter});
        }
        goto release_frame;
    }

    /* 6. 提交：只有写已落存储的完成边界才有 checkpoint。 */
    status = runtimec_execution_window_commit_checkpoint(session, host, &snapshot, request,
                                              &execution_exit, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        output_result->status = status;
        output_result->execution_exit.status = status;
        if (error != NULL && error->message.size == 0U) {
            static _Thread_local char checkpoint_message[96];
            (void)snprintf(checkpoint_message, sizeof(checkpoint_message),
                           "checkpoint commit failed (exit detail=%u)",
                           (unsigned)execution_exit.detail_code);
            runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                               (int64_t)execution_exit.detail_code,
                               runtimec_string_view_from_c_string(
                                   checkpoint_message),
                               (runtimec_program_counter_t){
                                   .stack_identifier =
                                       (int32_t)request->stack_identifier,
                                   .source_instruction_ordinal =
                                       (int32_t)execution_exit
                                           .logical_program_counter});
        }
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
