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

#include <stdio.h>
#include <string.h>

#include "contract/error.h"
#include "contract/kvspace_access.h"
#include "contract/program_counter.h"
#include "core_operations.h"
#include "frame_slot.h"
#include "frame_target_key.h"
#include "value_access.h"
#include "vthread_status.h"

#define VMBIN_SCRATCH_INITIAL_CAPACITY 32U

typedef struct vmbin_execution {
    const binrwir_binfunc_t *program;
    const binrwir_registry_t *builtin_registry;
    vmbin_profile_t *profile;
    binrwir_binvthread_frame_t *frame;
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

/*
 * `exit.detail_code` **只承载本层（解释器）的分类**，下层的码留在
 * `execution->error.code` 里——调用方要细节就读 error。
 *
 * 曾经这里把 `error.code` 抄进 detail_code，理由是「给更具体的细节」。代价是
 * 这个字段变成混合命名空间：解释器、执行窗口、值访问三个枚举同值 6，同一个
 * 数字在三种解释下都含义明确，于是没有任何地方会报「你不确定」。实际后果是
 * 全队两次误判（UNRESOLVED_TARGET 被读成 COMPILE_FAILED，真实原因却是
 * 值访问的 SIZE_MISMATCH）。
 *
 * 判据：**同一份数据不许有两种解释**。细节已经在 error 里，抄第二份是冗余，
 * 而冗余的代价是命名空间被毁。
 */
/*
 * 失败出口：**已有内层诊断时保留成因，不许用兜底码盖掉**。
 *
 * 判据：诊断的「成因」由产生它的那一层负责，兜底只在没人负责时使用。
 * 反例（实测）：`h·*k -> p:int64` 键不存在时，值访问层已经写了
 *   `0x4406 None cannot be stored in a declared slot: …`
 * 而本层随后用 `0x4107 INSTRUCTION_FAILED` 覆盖，`kvlangrun` 只打出兜底码——
 * 人看到「指令执行失败」又要从头查一轮。k_nucleotide 在这上面绕过三轮。
 *
 * **「已有」按内容判，不按指针非空**：`runtimec_error_t` 是值结构，
 * `detail_code == 0 && message.size == 0` 才是「没人写过」。
 *
 * **状态码照常传播**：保留消息不等于把失败当成功，`status` 原样返回。
 *
 * **不需要额外清空**：`vmbin_interpreter_step` 每步开头都会 reset
 * `execution->error`，所以这里读到的一定是**本条指令**的诊断，不会捡到上一条
 * 的陈旧值。
 */
static runtimec_status_t vmbin_interpreter_fail_with_error(
    vmbin_execution_t *execution,
    const binrwir_instruction_view_t *instruction, runtimec_status_t status,
    uint32_t default_detail_code, binrwir_execution_exit_t *output_exit) {
    const runtimec_error_t *inner = &execution->error;
    const bool has_inner_diagnostic =
        inner->code != 0 || inner->message.size != 0U;

    if (!has_inner_diagnostic) {
        return vmbin_interpreter_fail(execution, instruction, status,
                                      default_detail_code, output_exit);
    }
    /*
     * 保留成因：出口照常是 ERROR、`status` 照常是真实失败状态，
     * 但 `detail_code` 用**内层那一位**——它才回答「为什么失败」。
     */
    vmbin_interpreter_error_exit(
        output_exit, status, (uint32_t)(inner->code < 0 ? 0 : inner->code),
        instruction->source_logical_program_counter);
    /* 内层消息一并带出去：`kvlangrun` 与 runner 打印时优先用它。 */
    output_exit->resume_logical_program_counter =
        instruction->source_logical_program_counter;
    return status;
}

/* 借出一个参数项的值体位置；读写信都从这里拿地址，不复制。 */
/*
 * 本文件的三处调用（copy、branch 条件、goto/branch 目标）**全都只认 body**：
 * copy 是值对值地搬，另两处把字节解成整数当跳转目标/条件。没有一处要整条 wire。
 *
 * 因此这里走 `_body` 版本。此前用的是 wire 版（`vmbin_value_access_locate`），
 * 对字面量它返回的是**整条 wire**（头 + body + attr），于是 COPY 把 17 字节的
 * wire 当成值写进 8 字节的 body 槽——写进去的是值头里的 `bodylen`、指令视图
 * 里的 opid 与 source pc，字面量本身反而不在里面。
 *
 * 判据是「这一层要的是值还是整条 XValue」：执行器的算术只吃值。
 */
static runtimec_status_t vmbin_interpreter_locate(
    vmbin_execution_t *execution, const binrwir_argument_t *argument,
    vmbin_value_location_t *output_location) {
    /*
     * **读语义**：这里只服务「把读参的值交给算术/跳转/copy 源」，
     * 解引用到不存在的目标应得 None 而不是失败
     * （`05-runtime语义/12-动态键.kv` §四）。
     */
    return vmbin_value_access_locate_body_read(execution->frame, argument,
                                               output_location,
                                               &execution->error);
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
    status = binrwir_binfunc_find_instruction(
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

/*
 * `copy` 该不该建立**结构引用别名**（而不是复制成员）。
 *
 * 三条判据都要成立，缺一退回字节拷贝：
 *   1. 写槽声明类型是 structref；
 *   2. 源声明类型是 structref；
 *   3. 源的有效长度 == 0（它自己就是容器头）。
 *
 * 第 3 条不能省：源若已经是「持路径的引用值」（body 非空），字节拷贝本身就对，
 * 再走引用分支会把路径当成容器 key 又拼一层。
 */
static bool vmbin_interpreter_copy_binds_reference(
    vmbin_execution_t *execution,
    const binrwir_instruction_view_t *instruction,
    const binrwir_argument_t *read_argument) {
    runtimec_string_view_t source_langtype = runtimec_string_view_make(NULL, 0U);
    runtimec_string_view_t target_langtype = runtimec_string_view_make(NULL, 0U);
    vmbin_value_location_t source_location;

    if (vmbin_value_access_langtype(execution->frame, read_argument,
                                    &source_langtype, NULL) !=
            RUNTIMEC_STATUS_SUCCESS ||
        vmbin_value_access_langtype(
            execution->frame, &instruction->write_arguments.data[0],
            &target_langtype, NULL) != RUNTIMEC_STATUS_SUCCESS) {
        return false;
    }
    /*
     * ── 判据：**目标**要引用，且**源**是一个容器 ────────────────────────
     *
     * 源的 `*` 前缀不能当判据：`(*cur) -> _5` 的源符号是 `*Node`，而 `locate`
     * 已经跟了一跳、`source_location` 拿到的是**容器本身的空 body**——语义上
     * 它此刻就是结构引用。拿 `*` 前缀否掉它，会让「先解引用再存局部」这条
     * 完全走不到引用分支（实测第一版就是这样静默不生效）。
     *
     * **标的判据用 langtype，不用「body 为空」**：空 body 也是合法标量值的形态
     * （空串），拿长度判会把空串误伤成容器。
     *
     * 两处都判 structref 会漏掉 map：`(*arr)·*mid` 里 `arr` 是
     * `'[int64]'int64`（FORM_MAPPING），目标是同名 map——那条链路曾把容器的
     * 空 body 当值拷贝过去，产物是一个「body 空、类型是 map」的副本，
     * 随后 `kvlang·resolve` 取它自己的物理路径，拼出 `_7·k`（从不存在的 key）。
     * 所以源侧改判**容器**（map ∪ structref）。
     */
    if (!vmbin_value_access_langtype_is_container(target_langtype)) {
        return false;
    }
    /*
     * **源侧也要是容器**，否则不成立别名。
     *
     * 指针源（`*T`）不能走这支：它的值是「指向容器的 key」，跟目标要的「容器
     * 引用」不是同一件事。`(*arr) -> _7` 的 `arr` 是 `*'[int64]'int64`，
     * 若这里放行，`copy` 会把**指针的 body（目标 key 文本）**当成别名写进
     * `_7`，而 `_7` 的声明是容器——下游按容器解析它就会拼出错的 key。
     */
    if (!vmbin_value_access_langtype_is_container(source_langtype)) {
        return false;
    }
    /*
     * 用 `locate`（**会跟 dereference**）而不是 `locate_body`：`(*cur) -> _5`
     * 的读参带 `dereference = 1`，`_body` 版本不跟跳，拿到的是指针本身；
     * 这里要判的是「跟跳之后源是不是容器」，所以必须是跟跳后的位置。
     */
    {
        /* 这里的 error 只用来吞掉失败细节：判据是布尔，不是诊断通道。 */
        runtimec_error_t ignored_error;

        runtimec_error_reset(&ignored_error);
        if (vmbin_value_access_locate(execution->frame, read_argument,
                                      &source_location, &ignored_error) !=
            RUNTIMEC_STATUS_SUCCESS) {
            return false;
        }
    }
    return !source_location.is_whole_wire && source_location.size == 0U;
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
    /*
     * ── structref 拷贝：建立**别名**，不复制成员 ────────────────────────────
     *
     * 判据三条同时成立才走引用分支，缺一退回普通字节拷贝（不猜）：
     *   1. **写槽**声明类型是 structref（`/lib/<Name>`）——写进指针槽（`*T`）、
     *      标量、数组都不是这条语义；
     *   2. **源**的声明类型也是 structref；
     *   3. 源的**有效长度 == 0**，即它本身是一个容器（`17-struct.kv`：实例是
     *      空 body 的容器头）。源若已经是「持路径的引用值」，body 非空，走下面的
     *      字节拷贝即可（长度相等，语义也正确）。
     *
     * 写入的内容是**源容器所在的 key 路径**，它从哪来：
     *   - 读参带 `dereference` ⇒ `source.read_bytes` 是解引用前的指针值文本，
     *     就是目标容器 key；但 `locate` 已经跟了一跳，所以这里重新取指针值；
     *   - 否则用 `vmbin_value_access_argument_key`（LOCAL → 帧槽、STATIC → 绝对路径）。
     *
     * **整值写**，不是定宽 memcpy：路径长度与目标槽旧 body 长度基本不等。
     */
    if (vmbin_interpreter_copy_binds_reference(
            execution, instruction,
            &instruction->read_arguments.data[0])) {
        char key_buffer[512];
        const binrwir_argument_t *read_argument =
            &instruction->read_arguments.data[0];
        runtimec_status_t reference_status;

        if (read_argument->dereference != 0U) {
            /*
             * 解引用读参：指针值本身就是目标 key。借 `locate` 的**不跟跳**版本
             * 取它——这里要的是指针里的文本，不是它指向的值。
             */
            binrwir_argument_t plain = *read_argument;
            vmbin_value_location_t pointer_value;

            plain.dereference = 0U;
            reference_status = vmbin_value_access_locate_body(
                execution->frame, &plain, &pointer_value, &execution->error);
            if (reference_status == RUNTIMEC_STATUS_SUCCESS &&
                pointer_value.read_bytes != NULL && pointer_value.size != 0U &&
                pointer_value.size + 1U <= sizeof(key_buffer)) {
                memcpy(key_buffer, pointer_value.read_bytes,
                       pointer_value.size);
                key_buffer[pointer_value.size] = '\0';
                reference_status = RUNTIMEC_STATUS_SUCCESS;
            } else if (reference_status == RUNTIMEC_STATUS_SUCCESS) {
                reference_status = RUNTIMEC_STATUS_INVALID;
            }
        } else if (read_argument->reference_kind ==
                   BINRWIR_REFERENCE_PARAMETER_READ) {
            /*
             * 形参源：它的路径是**调用方那个 key**，记在绑定表里
             * （`binding->caller_key`）——本帧拼 `<frame>/<名>` 拼出来的是形参槽
             * 自己的地址，不是容器所在的位置。
             *
             * 这就是 `(*arr)·*mid` 里 `(*arr)` 那一跳：`arr` 是
             * `*'[int64]'int64` 形参，`locate` 跟跳后拿到调用方的容器（空 body），
             * 要建别名就得知道**容器在哪**，而那个位置只有绑定表的 caller_key 有。
             */
            const binrwir_frame_binding_t *binding = NULL;
            uint16_t binding_slot = BINRWIR_FRAME_BINDING_SLOT_NONE;

            if (execution->frame->bindings == NULL ||
                vmbin_frame_slot_of_argument(execution->frame->program,
                                             read_argument,
                                             &binding_slot) !=
                    RUNTIMEC_STATUS_SUCCESS ||
                binrwir_frame_bindings_get(
                    execution->frame->bindings, binding_slot,
                    execution->frame->snapshot_generation, &binding) !=
                    RUNTIMEC_STATUS_SUCCESS) {
                reference_status = RUNTIMEC_STATUS_UNSUPPORTED;
            } else {
                const runtimec_key_view_t key =
                    binding->caller_key.size != 0U ? binding->caller_key
                                                   : binding->slot_key;

                if (key.size == 0U || key.size + 1U > sizeof(key_buffer)) {
                    reference_status = RUNTIMEC_STATUS_INVALID;
                } else {
                    memcpy(key_buffer, key.data, key.size);
                    key_buffer[key.size] = '\0';
                    reference_status = RUNTIMEC_STATUS_SUCCESS;
                }
            }
        } else {
            reference_status = vmbin_value_access_argument_key(
                execution->frame, read_argument, key_buffer,
                sizeof(key_buffer), &execution->error);
        }
        if (reference_status != RUNTIMEC_STATUS_SUCCESS) {
            return vmbin_interpreter_fail_with_error(
                execution, instruction, reference_status,
                VMBIN_INTERPRETER_DIAG_VALUE_ACCESS, output_exit);
        }
        status = vmbin_value_access_write_whole_value(
            execution->frame, &instruction->write_arguments.data[0],
            runtimec_bytes_view_make(key_buffer, strlen(key_buffer)),
            &execution->error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return vmbin_interpreter_fail_with_error(
                execution, instruction, status,
                VMBIN_INTERPRETER_DIAG_VALUE_ACCESS, output_exit);
        }
        vmbin_interpreter_advance(execution, identifier);
        return RUNTIMEC_STATUS_SUCCESS;
    }
    /*
     * ── `None` 写进**非引用**的声明槽：具名失败 ─────────────────────────────
     *
     * 判据由两个事实合成，缺一不可：
     *
     * 1. **源是 `None`**——共享谓词 `vmbin_value_location_is_none`（空 body ∧
     *    无 langtype 记录 ∧ 非目录键），与编码期 `binrwir_literal_is_none_value`
     *    同一组 wire 事实。
     * 2. **写槽的声明类型不是引用**——spec `03-类型系统/16-ptr`：「空引用的形态是
     *    `*T` + 空 body」，所以 `p:*int64 = None` 是**合法**的空引用，不是
     *    「None 落进有值的位置」。只看第 1 条会把整类指针判空拒掉。
     *
     * 为什么不能用「目标槽宽度」这类代理指标：`None -> s:[]char/utf32` 与
     * `"" -> s:[]char/utf32` 的 body 都是 0 字节，只有 langtype 记录分得开；
     * 而「定宽非零宽」又会把合法的空引用一起误伤。
     *
     * **事实未知（UNKNOWN）不许当成「不是 None」**：那是静默走错分支的来源。
     * 未知就具名失败，指出是这条路径没给出事实。
     */
    {
        vmbin_value_location_t probe;

        const vmbin_value_fact_t none_fact =
            vmbin_value_access_locate_body_read(
                execution->frame, &instruction->read_arguments.data[0],
                &probe, &execution->error) == RUNTIMEC_STATUS_SUCCESS
                ? vmbin_value_location_is_none(&probe)
                : VMBIN_VALUE_FACT_UNKNOWN;

        if (none_fact == VMBIN_VALUE_FACT_UNKNOWN) {
            runtimec_error_set(
                &execution->error, RUNTIMEC_STATUS_INVALID,
                RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                (int64_t)VMBIN_VALUE_ACCESS_DIAG_MALFORMED_WIRE,
                runtimec_string_view_from_c_string(
                    "cannot tell whether the copied value is None: its wire "
                    "facts are unavailable on this path"),
                RUNTIMEC_PROGRAM_COUNTER_NONE);
            return vmbin_interpreter_fail_with_error(
                execution, instruction, RUNTIMEC_STATUS_INVALID,
                VMBIN_INTERPRETER_DIAG_VALUE_ACCESS, output_exit);
        }
        if (none_fact == VMBIN_VALUE_FACT_YES) {
            /*
             * 第 2 条：写槽声明类型是引用（`*T` 或 `/lib/…` structref）⇒
             * `None` 就是空引用，**允许写入**。判据取写参的声明 langtype。
             */
            runtimec_string_view_t target_langtype =
                runtimec_string_view_make(NULL, 0U);
            runtimec_error_t ignored;

            runtimec_error_reset(&ignored);
            const bool target_known =
                vmbin_value_access_langtype(
                    execution->frame, &instruction->write_arguments.data[0],
                    &target_langtype, &ignored) == RUNTIMEC_STATUS_SUCCESS &&
                runtimec_string_view_is_valid(target_langtype) &&
                target_langtype.size != 0U;
            const bool target_is_reference =
                target_known &&
                (target_langtype.data[0] == '*' ||
                 vmbin_value_access_langtype_is_structref(target_langtype));

            if (!target_is_reference) {
                runtimec_error_set(
                    &execution->error, RUNTIMEC_STATUS_ERROR,
                    RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                    (int64_t)VMBIN_VALUE_ACCESS_DIAG_SIZE_MISMATCH,
                    runtimec_string_view_from_c_string(
                        "None cannot be stored in a declared slot: the value has "
                        "an empty body and no langtype; check existence first "
                        "(`base·*key != None`)"),
                    RUNTIMEC_PROGRAM_COUNTER_NONE);
                return vmbin_interpreter_fail_with_error(
                    execution, instruction, RUNTIMEC_STATUS_ERROR,
                    VMBIN_INTERPRETER_DIAG_VALUE_ACCESS, output_exit);
            }
        }
    }
    status = vmbin_value_access_write_body(
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

        if (binrwir_binfunc_get_instruction(execution->program, identifier + 1U,
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
    /*
     * handler 的返回值不参与状态判断。逻辑错误的唯一出口是 `output_exit.kind
     * = ERROR`，由主循环写 `‥status="error"`；本层只看出口决定是否推进。
     */
    *output_exit = (binrwir_execution_exit_t){0};
    {
        const runtimec_status_t handler_status =
            entry->handler(execution->frame, instruction, output_exit);
        /*
         * 过渡兜底：仍有 handler 只回状态、不写出口。它们的状态不是循环判据，
         * 但必须折成一个 ERROR 出口，主循环才能用 `‥status="error"` 停机。
         * 新写的 handler 应当自己置 ERROR 出口，不再依赖这条。
         */
        if (output_exit->kind == BINRWIR_EXECUTION_EXIT_NONE &&
            handler_status != RUNTIMEC_STATUS_SUCCESS) {
            vmbin_interpreter_error_exit(
                output_exit, handler_status,
                VMBIN_INTERPRETER_DIAG_HANDLER_FAILED,
                instruction->source_logical_program_counter);
        }
    }
    if (output_exit->kind != BINRWIR_EXECUTION_EXIT_NONE) {
        /* 侧出口（错误 / call / handoff / yield）：指令未提交，游标不推进。 */
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
    status = binrwir_binfunc_get_instruction(execution->program, identifier,
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

/*
 * 标准 status 循环只做两件事：
 *   1. 读 kvspace 的 `‥status` 字符串，决定继续 / 停；
 *   2. 每步把 `‥pc` 写回 kvspace。
 * handler 的返回值与返回状态不参与判断；错误只写 `‥status = "error"`。
 */
static bool vmbin_interpreter_observable(
    const binrwir_binvthread_frame_t *frame) {
    return (frame->flags & BINRWIR_BINVTHREAD_FRAME_FLAG_OBSERVABLE) != 0U;
}

/* `‥status` 是继续态：init / running / wait。键缺失按继续处理。 */
static bool vmbin_interpreter_status_continue(
    vmbin_execution_t *execution, uint32_t identifier,
    binrwir_execution_exit_t *output_exit) {
    char key[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    runtimec_string_view_t text;
    runtimec_error_t error;
    binrwir_instruction_view_t instruction;

    if (!vmbin_interpreter_observable(execution->frame) ||
        execution->frame->kvspace_access == NULL ||
        execution->frame->kvspace_access->handle == NULL ||
        runtimec_vthread_status_key(
            execution->frame->frame_path,
            runtimec_string_view_from_c_string("/‥status"), key,
            sizeof(key)) != RUNTIMEC_STATUS_SUCCESS) {
        return true;
    }
    runtimec_error_reset(&error);
    if (runtimec_vthread_status_read(
            execution->frame->kvspace_access->handle, key, &text, &error) !=
        RUNTIMEC_STATUS_SUCCESS) {
        return true;
    }
    if ((text.size == 4U && memcmp(text.data, "init", 4U) == 0) ||
        (text.size == 7U && memcmp(text.data, "running", 7U) == 0) ||
        (text.size == 4U && memcmp(text.data, "wait", 4U) == 0)) {
        return true;
    }
    if (binrwir_binfunc_get_instruction(execution->program, identifier,
                                        &instruction) !=
        RUNTIMEC_STATUS_SUCCESS) {
        return false;
    }
    if (text.size == 6U && memcmp(text.data, "paused", 6U) == 0) {
        vmbin_interpreter_committed_exit(
            output_exit, BINRWIR_EXECUTION_EXIT_YIELD,
            instruction.source_logical_program_counter,
            instruction.source_logical_program_counter, 0U);
    } else if (text.size == 5U && memcmp(text.data, "error", 5U) == 0) {
        vmbin_interpreter_error_exit(
            output_exit, RUNTIMEC_STATUS_ERROR, 0U,
            instruction.source_logical_program_counter);
    } else {
        vmbin_interpreter_committed_exit(
            output_exit, BINRWIR_EXECUTION_EXIT_DONE,
            instruction.source_logical_program_counter,
            instruction.source_logical_program_counter, 0U);
    }
    return false;
}

/* 错误只写 `‥status = "error"`；没有状态返回、没有新增诊断码。 */
static void vmbin_interpreter_write_error_status(
    const binrwir_binvthread_frame_t *frame) {
    char key[RUNTIMEC_KVSPACE_KEY_CAPACITY];
    static const char error_text[] = "error";

    if (frame->kvspace_access == NULL ||
        frame->kvspace_access->handle == NULL ||
        runtimec_vthread_status_key(
            frame->frame_path,
            runtimec_string_view_from_c_string("/‥status"), key,
            sizeof(key)) != RUNTIMEC_STATUS_SUCCESS) {
        return;
    }
    (void)runtimec_vthread_status_write(
        frame->kvspace_access->handle, key,
        runtimec_bytes_view_make((const uint8_t *)error_text,
                                 sizeof(error_text) - 1U));
}

/* 提交 `‥pc`：失败就写 `‥status = "error"`，不返回状态。 */
static void vmbin_interpreter_commit_pc(vmbin_execution_t *execution,
                                        uint32_t source_program_counter) {
    runtimec_program_counter_t counter;
    runtimec_error_t error;

    if (!vmbin_interpreter_observable(execution->frame) ||
        execution->frame->kvspace_access == NULL ||
        !runtimec_string_view_is_valid(execution->frame->vthread_identifier) ||
        execution->frame->vthread_identifier.size == 0U) {
        return;
    }
    runtimec_error_reset(&error);
    counter.stack_identifier = execution->frame->stack_identifier;
    counter.source_instruction_ordinal = (int32_t)source_program_counter;
    if (runtimec_kvspace_access_write_program_counter(
            execution->frame->kvspace_access,
            execution->frame->vthread_identifier, counter, &error) !=
        RUNTIMEC_STATUS_SUCCESS) {
        vmbin_interpreter_write_error_status(execution->frame);
    }
}

/* 入口提交：第一条指令执行前先落一个合法恢复点。 */
static void vmbin_interpreter_commit_entry(vmbin_execution_t *execution) {
    binrwir_instruction_view_t instruction;
    const uint32_t identifier = execution->frame->logical_program_counter;

    if (!vmbin_interpreter_observable(execution->frame) ||
        identifier == BINRWIR_IDENTIFIER_NONE ||
        (size_t)identifier >= execution->instruction_count ||
        binrwir_binfunc_get_instruction(execution->program, identifier,
                                        &instruction) !=
            RUNTIMEC_STATUS_SUCCESS) {
        return;
    }
    vmbin_interpreter_commit_pc(
        execution, instruction.source_logical_program_counter);
}

/* 每步提交：普通指令完成后推进到下一格。 */
static void vmbin_interpreter_commit_step(vmbin_execution_t *execution) {
    binrwir_instruction_view_t instruction;
    const uint32_t identifier = execution->frame->logical_program_counter;

    if (!vmbin_interpreter_observable(execution->frame) ||
        (size_t)identifier >= execution->instruction_count ||
        binrwir_binfunc_get_instruction(execution->program, identifier,
                                        &instruction) !=
            RUNTIMEC_STATUS_SUCCESS) {
        return;
    }
    vmbin_interpreter_commit_pc(
        execution, instruction.source_logical_program_counter);
}

runtimec_status_t
vmbin_execute(const binrwir_binfunc_t *program,
              const binrwir_execution_plan_t *execution_plan,
              binrwir_binvthread_frame_t *frame,
              const binrwir_registry_t *builtin_registry,
              void *kvspace_handle,
              vmbin_profile_t *profile,
              binrwir_execution_exit_t *output_exit) {
    vmbin_execution_t execution;
    binrwir_binfunc_view_t program_view;
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
    status = binrwir_binfunc_validate(program);
    if (status != RUNTIMEC_STATUS_SUCCESS ||
        binrwir_binfunc_get_view(program, &program_view) !=
            RUNTIMEC_STATUS_SUCCESS) {
        return RUNTIMEC_STATUS_INVALID;
    }
    memset(&execution, 0, sizeof(execution));
    runtimec_error_reset(&execution.error);
    execution.program = program;
    execution.builtin_registry = builtin_registry;
    execution.profile = profile;
    execution.frame = frame;
    /* 本层不认识存储：只把句柄放进 Frame，由 handler 直接调 kvspace ABI。 */
    frame->kvspace_handle = kvspace_handle;
    /* call 出口要靠它判断「有没有后继指令」，从而给出可恢复位置。 */
    execution.instruction_count = program_view.instruction_count;

    /*
     * 标准 status 循环：
     *   读 `‥status` 字符串 → 执行一条 → 提交 `‥pc`。
     * handler 的返回值与 exit.status 不参与判断；错误只写 `‥status="error"`。
     */
    vmbin_interpreter_commit_entry(&execution);
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
        /*
         * 唯一的 continue 判据：`‥status` 字符串是 init / running / wait。
         * 暂停停在当前指令之前（`‥pc` 已由上一格提交指向它），恢复从同一位置重入。
         */
        if (!vmbin_interpreter_status_continue(&execution, identifier,
                                               output_exit)) {
            status = output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR
                         ? RUNTIMEC_STATUS_ERROR
                         : RUNTIMEC_STATUS_SUCCESS;
            break;
        }
        /*
         * 执行一条。**不看返回值**：handler 逻辑错误的唯一出口是把
         * `‥status` 写成 "error"。
         */
        (void)vmbin_interpreter_step(&execution, identifier, output_exit);
        if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) {
            binrwir_instruction_view_t failing_instruction;
            /*
             * 唯一失败通道是 `‥status="error"`。位置只在诊断里用；handler 没写
             * 就从当前游标补上。
             */
            if ((output_exit->logical_program_counter == 0U ||
                 output_exit->logical_program_counter ==
                     BINRWIR_EXECUTION_PROGRAM_COUNTER_NONE) &&
                binrwir_binfunc_get_instruction(program, identifier,
                                                &failing_instruction) ==
                    RUNTIMEC_STATUS_SUCCESS) {
                output_exit->logical_program_counter =
                    failing_instruction.source_logical_program_counter;
                output_exit->resume_logical_program_counter =
                    failing_instruction.source_logical_program_counter;
            }
            vmbin_interpreter_write_error_status(frame);
            status = output_exit->status != RUNTIMEC_STATUS_SUCCESS
                         ? output_exit->status
                         : RUNTIMEC_STATUS_ERROR;
            break;
        }
        if (output_exit->kind != BINRWIR_EXECUTION_EXIT_NONE) {
            /* CALL / DONE / HANDOFF / YIELD：控制流出口，不是状态。 */
            status = RUNTIMEC_STATUS_SUCCESS;
            break;
        }
        /*
         * 普通指令完成后推进 `‥pc`。控制流出口可能改变 stackid，位置由
         * 窗口层 checkpoint 提交（不能只改 s0）。
         */
        vmbin_interpreter_commit_step(&execution);
    }
    return status;
}
