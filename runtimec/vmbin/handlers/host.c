/*
 * 组件：vmbin/handlers/host 的实现
 * 作用：host 服务算子（kvspace / random）的 handler。存储经
 *       `frame->kvspace_handle` 直接调 kvspace C ABI——不封装、不转发。
 * 联系：条目由 engine/session 装配进 `binrwir_registry_t`。
 * 约束：热路径零 malloc；所有缓冲来自栈。
 *
 * 第一版 kvspace 只支持 STATIC key：见 `runtimec_host_static_key` 的注释。
 */

/* nanosleep 是 POSIX：本工程用 C11（`-std=c11` 关闭扩展），必须显式要 POSIX。
 * 必须在**任何** include 之前定义——它影响系统头里暴露哪些声明，晚一步就等于没写。 */
#define _POSIX_C_SOURCE 200809L

#include "vmbin/handlers/host.h"

#include "contract/diagnostic_band.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "binrwir/execution_exit.h"
#include "binrwir/execution_frame.h"
#include "binrwir/instruction.h"
#include "binrwir/operation_identifiers.h"
#include "binrwir/program.h"
#include "contract/member_key_text.h"
#include "contract/status.h"
#include "kvspace/kvspace.h"
#include "vmbin/frame_target_key.h"
#include "vmbin/handlers/handler_call.h"
#include "vmbin/value_access.h"
#include "vmbin/vthread_status.h"

/* 与 shm 后端的 key 上限一致：后端拒绝长度 >= 1024 的 key（见 kvspace-c 的
 * kvspace.c 中该检查），所以这里取同一上限，不发明第二套。 */
#define RUNTIMEC_HOST_KEY_CAPACITY 1024U

/*
 * 把一个读参解析成 NUL 结尾的 key 文本。
 *
 * **第一版只接受 `BINRWIR_REFERENCE_STATIC`。** 这不是漏写，是有意留白：
 *
 *   - STATIC 的 slot 是符号池下标，池里存的就是 key 文本（见
 *     `binrwir/argument.h` 对该引用种类的说明），运行期不需要任何转换。
 *   - 其余引用种类都是**引用解析**问题：值当 key 要把值转成字符串、容器成员要
 *     下钻、`/vthread/` 要还原绝对路径。旧运行时把这三件事写在一个
 *     `member_path_buf` + `path_arg` 里（`runtime/src/rwir_kv.c:38,195`），那是
 *     layout/vmkv 在编码期就该定下来的地址解析，不是 handler 的职责。在 handler
 *     里重写一遍，就是在造第二套地址解析。
 *
 * 遇到其它引用种类返回 UNSUPPORTED（不是静默失败、不是猜一个 key 去打）——
 * 猜错的代价是把一次读悄悄打到别的键上。
 *
 * 待补（后续独立能力）：动态 key 的引用解析。落地前本函数保持失败封闭。
 */
static runtimec_status_t runtimec_host_static_key(
    const binrwir_execution_frame_t *frame, const binrwir_argument_t *argument,
    char *buffer, size_t capacity) {
    runtimec_string_view_t name;
    runtimec_string_view_t langtype;

    if (argument->reference_kind != BINRWIR_REFERENCE_STATIC) {
        return RUNTIMEC_STATUS_UNSUPPORTED;
    }
    if (binrwir_program_get_symbol(frame->program, (uint16_t)argument->slot,
                                   &name, &langtype) !=
            RUNTIMEC_STATUS_SUCCESS ||
        name.size == 0U || name.size >= capacity) {
        return RUNTIMEC_STATUS_INVALID;
    }
    memcpy(buffer, name.data, name.size);
    buffer[name.size] = '\0';
    return RUNTIMEC_STATUS_SUCCESS;
}

/* 统一的失败出口：把状态写进结构化 exit，不推进游标。 */

/*
 * host 的失败细节码。**每个码对应一个确定的成因**，不设「其它」类。
 *
 * 让调用方给码而不是出口自己编：本文件有 27 个失败调用点，一个统一出口若自己
 * 编个笼统码，产出的就是「失败但没原因」——语料里那三条 `detail=0` 正是这么来的。
 */
typedef enum runtimec_host_diagnostic {
    RUNTIMEC_HOST_DIAG_NONE = 0,
    /* 读参或写参个数与算子形状不符。 */
    RUNTIMEC_HOST_DIAG_ARITY = 0x4601,
    /* 取不到 kvspace 句柄（本 session 不接存储）。 */
    RUNTIMEC_HOST_DIAG_MISSING_HANDLE = 0x4602,
    /* 取不到绝对路径键（形参不是 STATIC/LOCAL，或键文本超长）。 */
    RUNTIMEC_HOST_DIAG_KEY_UNRESOLVED = 0x4603,
    /* 后端调用失败（kvspaceGet/Set/Watch/List 等返回非 0）。 */
    RUNTIMEC_HOST_DIAG_BACKEND_FAILED = 0x4604,
    /* 读参的值体无法定位，或宽度与声明不符。 */
    RUNTIMEC_HOST_DIAG_OPERAND_INVALID = 0x4605,
    /* 写参不可写，或写入长度与当前值体不符。 */
    RUNTIMEC_HOST_DIAG_WRITE_FAILED = 0x4606,
    /* 值头几何不可逆（body 地址推不出 XValue 基址）。 */
    RUNTIMEC_HOST_DIAG_MALFORMED_WIRE = 0x4607,
    /* 本执行器不兑现这个算子（能力缺口，不是错误）。 */
    RUNTIMEC_HOST_DIAG_UNSUPPORTED = 0x4608
} runtimec_host_diagnostic_t;

_Static_assert(RUNTIMEC_DIAGNOSTIC_IN_BAND(RUNTIMEC_HOST_DIAG_UNSUPPORTED,
                                           RUNTIMEC_DIAGNOSTIC_BAND_HOST),
               "host diagnostic must stay inside its band");
_Static_assert(RUNTIMEC_HOST_DIAG_NONE == 0,
               "0 is reserved for \"unset\"; failure paths must never report it");

static runtimec_status_t runtimec_host_fail(
    binrwir_execution_exit_t *output_exit, runtimec_status_t status,
    uint32_t detail_code, uint32_t source_program_counter) {
    *output_exit = (binrwir_execution_exit_t){0};
    output_exit->kind = BINRWIR_EXECUTION_EXIT_ERROR;
    output_exit->status = status;
    output_exit->detail_code = detail_code;
    output_exit->logical_program_counter = source_program_counter;
    output_exit->resume_logical_program_counter = source_program_counter;
    return status;
}

/*
 * 准备一次 host ABI 调用：校验读参个数、取句柄、按 `key_count` 取绝对路径。
 *
 * **目的单一**：把「这条指令要调哪个 ABI、参数长什么样」准备好，然后交给调用方。
 * 它不调用 ABI、不做错误码翻译——那两件事属于各自的算子。
 *
 * 失败即置 `exit` 并返回 false，**不写任何输出参数**。
 */
static bool runtimec_host_prepare_call(
    const binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit, size_t expected_reads,
    void **output_handle, char keys[][RUNTIMEC_HOST_KEY_CAPACITY]) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;
    runtimec_status_t status;

    *output_exit = (binrwir_execution_exit_t){0};
    if (instruction->read_arguments.count != expected_reads) {
        runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           RUNTIMEC_HOST_DIAG_ARITY, source_program_counter);
        return false;
    }
    status = runtimec_vthread_status_handle(frame, output_handle);
    for (size_t ordinal = 0U; ordinal < expected_reads &&
                              status == RUNTIMEC_STATUS_SUCCESS;
         ++ordinal) {
        status = runtimec_host_static_key(
            frame, &instruction->read_arguments.data[ordinal], keys[ordinal],
            RUNTIMEC_HOST_KEY_CAPACITY);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        runtimec_host_fail(output_exit, status, RUNTIMEC_HOST_DIAG_KEY_UNRESOLVED, source_program_counter);
        return false;
    }
    return true;
}

/* 调 ABI 并翻译结果：非 0 一律 ERROR，带源 pc。 */
static runtimec_status_t runtimec_host_finish_call(
    binrwir_execution_exit_t *output_exit, int operation_status,
    uint32_t source_program_counter) {
    if (operation_status != 0) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                                  RUNTIMEC_HOST_DIAG_BACKEND_FAILED, source_program_counter);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * `kvspace·get(key)`：读参 1 是 key，结果写进写参 1。
 *
 * 取参顺序依据旧运行时 `runtime/src/rwir_kv.c:332`
 * （`kvlangBuiltinKvspaceGet`：第 0 个读参当路径，经
 * `kvlangBuiltinWriteResult` 写结果）。
 *
 * 缺失 key 不是错误：C ABI 返回 0 且 value=NULL，写参保持原样。
 */
/*
 * 从一个读参取 `uint64` 标量。宽度必须正好 8 字节——不截断、不扩展。
 *
 * 一句话目的：**把一个读参读成一个数**。
 */
static bool runtimec_host_read_uint64(
    const binrwir_execution_frame_t *frame,
    const binrwir_argument_t *argument, uint64_t *output_value,
    binrwir_execution_exit_t *output_exit, uint32_t source_program_counter) {
    vmbin_value_location_t location;
    runtimec_error_t error;

    runtimec_error_reset(&error);
    if (vmbin_value_access_locate_body(frame, argument, &location, &error) !=
            RUNTIMEC_STATUS_SUCCESS ||
        location.read_bytes == NULL || location.size != sizeof(*output_value)) {
        runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           RUNTIMEC_HOST_DIAG_OPERAND_INVALID, source_program_counter);
        return false;
    }
    memcpy(output_value, location.read_bytes, sizeof(*output_value));
    return true;
}

/*
 * 把一个 int64 结果写进写参。`random·*`、`listlen` 这类「算出一个数」的算子共用。
 *
 * 一句话目的：**把一个数落到写参上**。
 */
static runtimec_status_t runtimec_host_store_int64(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction, int64_t value,
    binrwir_execution_exit_t *output_exit, uint32_t source_program_counter) {
    runtimec_error_t error;

    runtimec_error_reset(&error);
    if (vmbin_value_access_write_body(
            frame, &instruction->write_arguments.data[0],
            runtimec_bytes_view_make(&value, sizeof(value)),
            &error) != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                                  RUNTIMEC_HOST_DIAG_WRITE_FAILED, source_program_counter);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}


/*
 * `kvspace·set(key, value)`：两个读参。顺序依据旧运行时
 * `runtime/src/rwir_kv.c:388`（`kvlangBuiltinKvspaceSet`：`in[0]` 是 key、
 * `in[1]` 是 value）。
 *
 * value 取的是**完整 wire**：读参已绑定到值体地址时，`vmbin_value_access_read`
 * 给出的是 body 本身，不含 head——而 `kvspaceSetValue` 要的是完整 XValue。
 * 这里用 `read` 取整值（与 temp_println 同一口径），缺失或长度不符即失败。
 */

/*
 * `kvspace·watch(key, target)`：读参 1 是 key、读参 2 是期望值；
 * 结果写进写参。顺序依据旧运行时 `runtime/src/rwir_kv.c:671`
 * （`kvlangBuiltinKvspaceWatch`：`in[0]` 是 key、`in[1]` 是 target）。
 *
 * 超时不是错误：ABI 返回 0 且 out=NULL，写参保持原样。
 */
static runtimec_status_t runtimec_host_kvspace_watch(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;
    char key[RUNTIMEC_HOST_KEY_CAPACITY];
    uint8_t *observed = NULL;
    uint32_t observed_size = 0U;
    void *handle = NULL;
    vmbin_value_location_t expected_location;
    runtimec_error_t error;
    runtimec_status_t status;

    runtimec_error_reset(&error);
    *output_exit = (binrwir_execution_exit_t){0};
    if (instruction->read_arguments.count != 2U ||
        instruction->write_arguments.count != 1U) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_ARITY, source_program_counter);
    }
    status = runtimec_vthread_status_handle(frame, &handle);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_host_static_key(frame, &instruction->read_arguments.data[0],
                                          key, sizeof(key));
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = vmbin_value_access_locate(
            frame, &instruction->read_arguments.data[1], &expected_location,
            &error);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, status, RUNTIMEC_HOST_DIAG_OPERAND_INVALID, source_program_counter);
    }
    if (kvspaceWatch(handle, key, expected_location.read_bytes,
                     (uint32_t)expected_location.size, UINT64_C(1000000),
                     &observed, &observed_size) != 0) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                                  RUNTIMEC_HOST_DIAG_BACKEND_FAILED, source_program_counter);
    }
    if (observed != NULL && observed_size != 0U) {
        status = vmbin_value_access_write(
            frame, &instruction->write_arguments.data[0],
            runtimec_bytes_view_make(observed, (size_t)observed_size), &error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return runtimec_host_fail(output_exit, status, RUNTIMEC_HOST_DIAG_WRITE_FAILED, source_program_counter);
        }
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * random 一族的随机源。
 *
 * 与旧运行时同源（`runtime/src/rwir_random.c:5` 读 `/dev/urandom`）：换一个源
 * 就换了一种随机性，而「随机数的质量」不是 handler 该单独决定的事。
 *
 * 打不开就返回 0 而不是回退到 `rand()`——回退会让调用方以为拿到了真随机数，
 * 而那条路径可预测。宁可返回 0 并让上层看见同值，也不静默降级。
 */
static uint64_t runtimec_host_random_u64(void) {
    uint64_t value = 0U;
    FILE *source = fopen("/dev/urandom", "rb");

    if (source == NULL) {
        return 0U;
    }
    if (fread(&value, sizeof(value), 1U, source) != 1U) {
        value = 0U;
    }
    fclose(source);
    return value;
}

/* `random·uint64()`：无读参，结果写写参 1。 */
/* `random·u64()`：满 64 位随机数。 */
static runtimec_status_t runtimec_host_random_unsigned_64(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;

    *output_exit = (binrwir_execution_exit_t){0};
    if (instruction->write_arguments.count != 1U) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_ARITY, source_program_counter);
    }
    return runtimec_host_store_int64(frame, instruction,
                                     (int64_t)runtimec_host_random_u64(),
                                     output_exit, source_program_counter);
}

/* `random·int63()`：取 64 位随机数的高 63 位，保证非负（旧实现 `>> 1`）。 */
/* `random·int63()`：取 64 位随机数的高 63 位，保证非负（旧实现 `>> 1`）。 */
static runtimec_status_t runtimec_host_random_signed_63(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;

    *output_exit = (binrwir_execution_exit_t){0};
    if (instruction->write_arguments.count != 1U) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_ARITY, source_program_counter);
    }
    return runtimec_host_store_int64(
        frame, instruction, (int64_t)(runtimec_host_random_u64() >> 1U),
        output_exit, source_program_counter);
}

/*
 * `random·intn(bound)`：读参 1 是 int64 上界，结果写写参 1。
 * 语义按旧实现（`rwir_random.c:30`）：`bound == 0` 返回 0，否则取模。
 */
/* `random·intn(bound) -> n`：`[0, bound)` 半开区间；bound 为 0 时返回 0。 */
static runtimec_status_t runtimec_host_random_below(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;
    uint64_t bound = 0U;
    uint64_t value;

    *output_exit = (binrwir_execution_exit_t){0};
    if (instruction->read_arguments.count != 1U ||
        instruction->write_arguments.count != 1U) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_ARITY, source_program_counter);
    }
    if (!runtimec_host_read_uint64(frame, &instruction->read_arguments.data[0],
                                   &bound, output_exit,
                                   source_program_counter)) {
        return RUNTIMEC_STATUS_ERROR;
    }
    value = bound == 0U ? 0U : runtimec_host_random_u64() % bound;
    return runtimec_host_store_int64(frame, instruction, (int64_t)value,
                                     output_exit, source_program_counter);
}

/*
 * `vthread·setstatus(status)`：把状态写进 `/vthread/{vid}/‥status`。
 *
 * 第一版只写状态键，**不推进 PC**。旧实现（`runtime/src/rwir_vthread.c:100`）
 * 经 `kvlangVthreadNextPc` 同时写 pc 与 status，那需要 runtimec 侧尚未具备的
 * PC 后继计算；只写 status 不会让程序停在错的位置，而写错 pc 会。
 */
/*
 * `vthread·setstatus(text)`：把状态文本写进当前帧的 `/‥status`。
 *
 * 与其它 host 算子不同：key 不是 static key、而是**从 `frame_path` 拼出来**的
 * （`<frame>/‥status`），所以 `prepare_call` 的「取绝对路径」不适用——这里取的
 * 是「当前帧的状态槽」，不是调用方给的路径。
 */
static runtimec_status_t runtimec_host_vthread_setstatus(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;
    char status_key[RUNTIMEC_HOST_KEY_CAPACITY];
    vmbin_value_location_t status_location;
    void *handle = NULL;
    runtimec_error_t error;

    runtimec_error_reset(&error);
    *output_exit = (binrwir_execution_exit_t){0};
    if (instruction->read_arguments.count != 1U) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_ARITY, source_program_counter);
    }
    if (runtimec_vthread_status_handle(frame, &handle) !=
            RUNTIMEC_STATUS_SUCCESS ||
        runtimec_vthread_status_key(
            frame->frame_path, runtimec_string_view_from_c_string("/‥status"),
            status_key, sizeof(status_key)) != RUNTIMEC_STATUS_SUCCESS ||
        vmbin_value_access_locate_body(frame,
                                       &instruction->read_arguments.data[0],
                                       &status_location,
                                       &error) != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_OPERAND_INVALID, source_program_counter);
    }
    if (runtimec_vthread_status_write(
            handle, status_key,
            runtimec_bytes_view_make(status_location.read_bytes,
                                     status_location.size)) !=
        RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                                  RUNTIMEC_HOST_DIAG_BACKEND_FAILED, source_program_counter);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * `vthread·sleep(duration)`：阻塞当前线程 duration 纳秒。
 *
 * 旧实现（`runtime/src/rwir_vthread.c:78`）就是 `nanosleep` 阻塞驱动线程——
 * 单进程模型下没有调度器参与，所以这条**可以做**，不需要引擎。
 *
 * 与旧实现的差异：旧实现前后各做一次 `kvspaceViewQuiesce`（暂停/恢复视图
 * quiesce）。runtimec 这一版没有 view quiesce 这一层，睡眠期间不改视图状态。
 * 这是有意的：没有那层就不假装有。
 */
/* `vthread·sleep(nanoseconds)`：非正值立即返回，不睡。 */
static runtimec_status_t runtimec_host_vthread_sleep(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;
    uint64_t duration_nanoseconds = 0U;

    *output_exit = (binrwir_execution_exit_t){0};
    if (instruction->read_arguments.count != 1U) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_KEY_UNRESOLVED, source_program_counter);
    }
    if (!runtimec_host_read_uint64(frame, &instruction->read_arguments.data[0],
                                   &duration_nanoseconds, output_exit,
                                   source_program_counter)) {
        return RUNTIMEC_STATUS_ERROR;
    }
    if (duration_nanoseconds > 0U) {
        const struct timespec interval = {
            .tv_sec = (time_t)(duration_nanoseconds / UINT64_C(1000000000)),
            .tv_nsec = (long)(duration_nanoseconds % UINT64_C(1000000000)),
        };

        nanosleep(&interval, NULL);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * `vthread·create` / `run` / `call`：**第一版有意留白**。
 *
 * 它们都要引擎参与：create 分配 vid、建栈索引、bootstrap 首指令；run 从持久化
 * PC 驱动子线程到结束；call 在当前 vid 上造一次动态调用帧。三者都不是 handler
 * 拿一个 kvspace 句柄就能自足完成的——那是 engine 的职责。
 *
 * 返回 UNSUPPORTED 并带明确诊断，不假装成功、不返回半成品。
 */
static runtimec_status_t runtimec_host_vthread_not_implemented(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    (void)frame;
    return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_UNSUPPORTED,
                              RUNTIMEC_HOST_DIAG_UNSUPPORTED,
                              instruction->source_logical_program_counter);
}

/*
 * 以下 10 条是 kvspace 的目录操作，形状与已做的三条一致：
 * 读参里的路径走 `runtimec_host_static_key`（STATIC only），其余参数按引用
 * 定位后取值，结果按各算子的既定语义写入写参或直接生效。
 *
 * 取参顺序全部按旧运行时 `runtime/src/rwir_kv.c` 核过，不是按「自然顺序」猜的：
 *   单路径（del / deltree / delextindex）→ `run_path_op`，第 1 读参是路径；
 *   双路径（cp / cpdir / cplist）→ `run_two_path_op`，src、dst 依次；
 *   mkindex → 路径 + capacity；extindex → 路径 + ext 路径；
 *   listlen → 路径，写 int64；listat → 路径 + 下标，写 char。
 */

/* 单路径操作：读参 1 是路径，无写参。 */
/* 单路径操作：读参 1 是路径，无写参。 */
static runtimec_status_t runtimec_host_single_path_operation(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit,
    int (*operation)(void *handle, const char *path, char *error,
                     uint32_t error_capacity)) {
    char keys[1][RUNTIMEC_HOST_KEY_CAPACITY];
    char message[256];
    void *handle = NULL;

    if (!runtimec_host_prepare_call(frame, instruction, output_exit, 1U,
                                    &handle, keys)) {
        return RUNTIMEC_STATUS_ERROR;
    }
    message[0] = '\0';
    return runtimec_host_finish_call(
        output_exit, operation(handle, keys[0], message, sizeof(message)),
        instruction->source_logical_program_counter);
}

/*
 * 复制族（`cp` / `cpdir` / `cplist`）：**src 是读参、dst 是写参**。
 *
 * 形状来自 `specification/kvspace-family.md` §二：复制的目标不是一个「值的位置」，
 * 而是**这次写的落点**——按 #632「写侧带类型」，dst 必须是写参，否则「写哪」这件事
 * 在指令里没有位置。`cp` / `cplist` 的 dst 同时是结果槽（结果 = dst 接到的值，
 * 同类型）；`cpdir` 不产生值，但仍占写参位（它就是写目标）。
 *
 * 声明的 `write_count` 由各算子给出：`cp`/`cplist` 为 1（结果槽即 dst），
 * `cpdir` 也为 1（dst 是写目标）——三者的写参语义相同，差别只在中段是否回写值。
 */
static runtimec_status_t runtimec_host_copy_operation(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit,
    int (*operation)(void *handle, const char *source,
                     const char *destination, char *error,
                     uint32_t error_capacity)) {
    char keys[1][RUNTIMEC_HOST_KEY_CAPACITY];
    char destination[RUNTIMEC_HOST_KEY_CAPACITY];
    char message[256];
    void *handle = NULL;
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;

    if (instruction->write_arguments.count != 1U ||
        !runtimec_host_prepare_call(frame, instruction, output_exit, 1U,
                                    &handle, keys)) {
        return RUNTIMEC_STATUS_ERROR;
    }
    if (runtimec_host_static_key(frame,
                                 &instruction->write_arguments.data[0],
                                 destination, sizeof(destination)) !=
        RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_KEY_UNRESOLVED,
                                  source_program_counter);
    }
    message[0] = '\0';
    return runtimec_host_finish_call(
        output_exit,
        operation(handle, keys[0], destination, message, sizeof(message)),
        source_program_counter);
}

/*
 * `kvspaceDel` 收的是**键数组**而不是单个键（ABI 如此，支持批量删），
 * 所以它不能直接塞进单路径模板。这里包一层把单键变成单元素数组——
 * 只做形状适配，不改变语义。
 */
static int runtimec_host_delete_one(void *handle, const char *path, char *error,
                                    uint32_t error_capacity) {
    const char *keys[1];

    keys[0] = path;
    return kvspaceDel(handle, keys, 1U, error, error_capacity);
}

/* 三个单路径算子的薄壳：ABI 的函数指针类型各不相同，只能各写一层。 */
static runtimec_status_t runtimec_host_kvspace_delete(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return runtimec_host_single_path_operation(frame, instruction, output_exit,
                                               runtimec_host_delete_one);
}
static runtimec_status_t runtimec_host_kvspace_delete_tree(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return runtimec_host_single_path_operation(frame, instruction, output_exit,
                                               kvspaceDelTree);
}
static runtimec_status_t runtimec_host_kvspace_delete_extension_index(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return runtimec_host_single_path_operation(frame, instruction, output_exit,
                                               kvspaceRmindexExt);
}

static runtimec_status_t runtimec_host_kvspace_copy(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return runtimec_host_copy_operation(frame, instruction, output_exit,
                                            kvspaceCp);
}
static runtimec_status_t runtimec_host_kvspace_copy_directory(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return runtimec_host_copy_operation(frame, instruction, output_exit,
                                            kvspaceCpTree);
}
static runtimec_status_t runtimec_host_kvspace_copy_list(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return runtimec_host_copy_operation(frame, instruction, output_exit,
                                            kvspaceCpList);
}

/* `kvspace·mkindex(path, capacity)`：路径 + 容量，无写参。 */
/* `kvspace·mkindex(path, capacity)`：路径 + 容量，调 ABI。 */
static runtimec_status_t runtimec_host_kvspace_make_index(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;
    char keys[2][RUNTIMEC_HOST_KEY_CAPACITY];
    char message[256];
    uint64_t capacity = 0U;
    void *handle = NULL;

    /* 第二个读参是标量不是路径：`prepare_call` 只取第一个，第二个单独读。 */
    if (!runtimec_host_prepare_call(frame, instruction, output_exit, 2U,
                                    &handle, keys) ||
        !runtimec_host_read_uint64(frame, &instruction->read_arguments.data[1],
                                   &capacity, output_exit,
                                   source_program_counter)) {
        return RUNTIMEC_STATUS_ERROR;
    }
    message[0] = '\0';
    return runtimec_host_finish_call(
        output_exit,
        kvspaceMkindex(handle, keys[0], (uint32_t)capacity, message,
                       sizeof(message)),
        source_program_counter);
}

/*
 * `kvspace·extindex(path, ext_path)`：两个路径，**都是读参**。
 *
 * 与复制族不同：`ext_path` 不是「这次写的落点」，而是「要挂上去的另一个位置」，
 * 它不承担写入语义（`extindex` 不动任何一方的值，只登记关联）。按 §二，它保持
 * 两读参零写参。
 */
static runtimec_status_t runtimec_host_kvspace_extension_index(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    char keys[2][RUNTIMEC_HOST_KEY_CAPACITY];
    char message[256];
    void *handle = NULL;

    if (!runtimec_host_prepare_call(frame, instruction, output_exit, 2U,
                                    &handle, keys)) {
        return RUNTIMEC_STATUS_ERROR;
    }
    message[0] = '\0';
    return runtimec_host_finish_call(
        output_exit,
        kvspaceMkindexExt(handle, keys[0], keys[1], message, sizeof(message)),
        instruction->source_logical_program_counter);
}

/*
 * `kvspace·listlen(path) -> int64`：读参 1 是路径，结果写写参。
 *
 * 旧实现在路径解析不出时返回 0 而不是报错（`rwir_kv.c:586`），这里同语义：
 * 空目录与取不到路径都给出 0，不制造「有错但要靠猜」的状态。
 */
/* `kvspace·listlen(path) -> n`：前缀下直接子项数，枚举失败按 0（与旧实现一致）。 */
static runtimec_status_t runtimec_host_kvspace_list_length(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;
    char keys[1][RUNTIMEC_HOST_KEY_CAPACITY];
    int32_t count = 0;
    void *handle = NULL;

    if (instruction->write_arguments.count != 1U ||
        !runtimec_host_prepare_call(frame, instruction, output_exit, 1U,
                                    &handle, keys)) {
        return RUNTIMEC_STATUS_ERROR;
    }
    /* 枚举失败不算错：count 保持 0，与旧实现一致。 */
    if (kvspaceListLen(handle, keys[0], 0, 0, &count) != 0) {
        count = 0;
    }
    return runtimec_host_store_int64(frame, instruction, (int64_t)count,
                                     output_exit, source_program_counter);
}

/*
 * `kvspace·listat(path, index) -> char`：路径 + 下标，取第 index 个子项名。
 *
 * 取不到时旧实现写空值（`rwir_kv.c:606` 中 `kvlangXvalueZero` 后直接 emit），
 * 这里同样不写结果槽——写空字符串会把「没有这一项」变成「这一项是空名」。
 */
static runtimec_status_t runtimec_host_kvspace_list_at(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;
    char path[RUNTIMEC_HOST_KEY_CAPACITY];
    char name[RUNTIMEC_HOST_KEY_CAPACITY];
    vmbin_value_location_t index_location;
    int64_t index = 0;
    void *handle = NULL;
    runtimec_error_t error;
    runtimec_status_t status;

    runtimec_error_reset(&error);
    *output_exit = (binrwir_execution_exit_t){0};
    if (instruction->read_arguments.count != 2U ||
        instruction->write_arguments.count != 1U) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_UNSUPPORTED, source_program_counter);
    }
    status = runtimec_vthread_status_handle(frame, &handle);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_host_static_key(frame, &instruction->read_arguments.data[0],
                                          path, sizeof(path));
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = vmbin_value_access_locate_body(
            frame, &instruction->read_arguments.data[1], &index_location, &error);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, status, RUNTIMEC_HOST_DIAG_ARITY, source_program_counter);
    }
    if (index_location.read_bytes == NULL ||
        index_location.size != sizeof(index)) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_KEY_UNRESOLVED, source_program_counter);
    }
    memcpy(&index, index_location.read_bytes, sizeof(index));
    if (index >= 0) {
        uint32_t name_length = 0U;

        if (kvspaceListAt(handle, path, 0, 0, (int32_t)index,
                          (uint8_t *)name, (uint32_t)sizeof(name),
                          &name_length) == 0 &&
            name_length != 0U && (size_t)name_length < sizeof(name)) {
            status = vmbin_value_access_write_body(
                frame, &instruction->write_arguments.data[0],
                runtimec_bytes_view_make(name, (size_t)name_length), &error);
            if (status != RUNTIMEC_STATUS_SUCCESS) {
                return runtimec_host_fail(output_exit, status,
                                          RUNTIMEC_HOST_DIAG_ARITY, source_program_counter);
            }
        }
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * 把一条**已经造好的** wire 写进写参指向的 key，并完成绑定。
 *
 * 与 `vmbin_handler_write_new_value` 的分工：那个按写槽声明的 langtype **另造**一条
 * SLACK wire（适用于 `compactarray·new` 这类「给我一串字节、按槽类型包起来」）；
 * 这里收的是**成品 wire**——指针的 head 已经有 ptr=PTR 与 target kindexpr，再包一层
 * 就毁了它。
 *
 * 两个动作缺一不可：写存储、绑槽。只写不绑 → 后续读该槽 `UNBOUND_SLOT`。
 */
static runtimec_status_t runtimec_host_write_pointer_wire(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction, const uint8_t *wire,
    size_t wire_size, binrwir_execution_exit_t *output_exit) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;
    char key[RUNTIMEC_HOST_KEY_CAPACITY];
    char message[256];
    runtimec_error_t error;
    runtimec_status_t status;

    runtimec_error_reset(&error);
    status = vmbin_value_access_argument_key(
        frame, &instruction->write_arguments.data[0], key, sizeof(key), &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_KEY_UNRESOLVED,
                                  source_program_counter);
    }
    message[0] = '\0';
    if (kvspaceSetValue(frame->kvspace_handle, key, wire, (uint32_t)wire_size,
                        message, sizeof(message)) != 0) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                                  RUNTIMEC_HOST_DIAG_BACKEND_FAILED,
                                  source_program_counter);
    }
    status = vmbin_value_access_bind_write_target(
        frame, &instruction->write_arguments.data[0],
        runtimec_key_view_make(key, strlen(key)), &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, status,
                                  RUNTIMEC_HOST_DIAG_WRITE_FAILED,
                                  source_program_counter);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}


/*
 * `vthread·pc() -> (int32 vid, [2]int32 [stackid, s0])`——读当前执行位置。
 *
 * 契约见 spec `05-runtime语义/02-执行模型-续.kv:26-40`，三条要点：
 *
 *   1. **纯读**：不创建、不修改任何 key，也不改变执行位置；
 *   2. **两个结果都要**：`result0` 是 vid，`result1` 是 `[stackid, s0]`
 *      （`[0]` 帧序号、`[1]` 该帧内的**指令轴坐标**）；
 *   3. **任一结果取不到 ⇒ 具名失败，不得用 0 顶替**——0 是合法的 stackid，
 *      顶替会制造一个假位置，而假位置在崩溃恢复里比「读不到」危险得多。
 *
 * ── 数据从哪来 ────────────────────────────────────────────────────────
 *
 * 帧结构里**没有** vid 与 stackid 字段，只有 `frame_path`（`/vthread/{vid}/[d]`）——
 * 那是调用入口原样写入的，本层不重新拼接（拼了就成第二份地址）。所以这里**解析**它：
 * vid 取第二个斜杠之前的段，stackid 取方括号内的整数。
 *
 * `s0` 取 `frame->logical_program_counter`：spec 明写它要的是「当前指令在帧内的
 * 指令轴坐标」，而解释器的游标正是这个。**不用 `‥pc`**——spec 同一节写明
 * 「两者不是同一事实，不得互相推导」（`‥pc` 存的是 source pc）。
 *
 * ── 为什么分三段而不是一个大函数 ──────────────────────────────────────
 *
 * 「取 vid」「取 stackid」「写两个结果」是三件独立的事：前两件各自有一套严格判据
 * （vid 全数字、stackid 必须在方括号里），第三件只管形状。合在一起时，任一判据
 * 都要在一段长函数里读到底才知道它保护的是什么；拆开后每段的失败出口就是它自己
 * 的判据，**任一分量取不到仍然是具名失败**（`0x4606` KEY_UNRESOLVED / `0x4607`
 * WRITE_FAILED），没有一处用 0 顶替。
 */

#define RUNTIMEC_HOST_VTHREAD_PREFIX "/vthread/"

/*
 * `/vthread/{vid}/[d]` 里第二个斜杠的位置——vid 段与帧段的分界。
 *
 * 单独成函数的原因：vid 段要用它作结束、帧段要用它作起点。各写一遍扫描就是
 * **第二份同一个规则**，而这个规则的偏差（例如把开头那个斜杠也算进去）会让
 * 两段各自解出不同的边界，谁都不报错。
 */
static runtimec_status_t runtimec_host_vthread_path_root_end(
    runtimec_key_view_t frame_path, size_t *output_root_end,
    binrwir_execution_exit_t *output_exit, uint32_t source_program_counter) {
    unsigned slash_count = 0U;
    size_t ordinal;

    for (ordinal = 1U; ordinal < frame_path.size; ++ordinal) {
        if (frame_path.data[ordinal] != '/') {
            continue;
        }
        slash_count += 1U;
        if (slash_count == 2U) {
            *output_root_end = ordinal;
            return RUNTIMEC_STATUS_SUCCESS;
        }
    }
    return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                              RUNTIMEC_HOST_DIAG_KEY_UNRESOLVED,
                              source_program_counter);
}

/* 取 vid：`/vthread/` 之后到 `root_end` 之前，必须全数字且非空。 */
static runtimec_status_t runtimec_host_vthread_identifier_from_path(
    runtimec_key_view_t frame_path, size_t root_end,
    int32_t *output_identifier, binrwir_execution_exit_t *output_exit,
    uint32_t source_program_counter) {
    const size_t prefix = sizeof(RUNTIMEC_HOST_VTHREAD_PREFIX) - 1U;
    int32_t value = 0;
    size_t index;

    if (root_end <= prefix) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_KEY_UNRESOLVED,
                                  source_program_counter);
    }
    for (index = prefix; index < root_end; ++index) {
        const int digit = frame_path.data[index] - '0';

        /* 上界判据是「乘十不进位到负数」：溢出会解出一个负 vid，那是一个
         * 看起来像数字、实际指向别的 vthread 的假位置。 */
        if (digit < 0 || digit > 9 || value > (INT32_MAX - digit) / 10) {
            return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                      RUNTIMEC_HOST_DIAG_KEY_UNRESOLVED,
                                      source_program_counter);
        }
        value = value * 10 + digit;
    }
    *output_identifier = value;
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * 取 stackid：`root_end` 之后必须有一个 `[`，其后的数字是帧号。
 *
 * `[0]` 是合法的根帧号，所以**不能用「值为 0」判断没取到**——判据是「进过
 * 方括号」，进了就算取到。没进方括号即失败，不给默认帧号。
 */
static runtimec_status_t runtimec_host_vthread_stack_from_path(
    runtimec_key_view_t frame_path, size_t root_end,
    int32_t *output_stack_identifier, binrwir_execution_exit_t *output_exit,
    uint32_t source_program_counter) {
    bool have_stack = false;
    int32_t value = 0;
    size_t index;

    for (index = root_end + 1U; index < frame_path.size; ++index) {
        const char character = frame_path.data[index];

        if (character == '[') {
            have_stack = true;
            continue;
        }
        if (!have_stack) {
            return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                      RUNTIMEC_HOST_DIAG_KEY_UNRESOLVED,
                                      source_program_counter);
        }
        if (character == ']') {
            break;
        }
        /* 帧号可以是 `[2.2]` 这种带序号的形态（见 kv_runner 的
         * `g_frame_serial`）。序号不是 stackid 的一部分，遇到非数字就停。 */
        if (character < '0' || character > '9') {
            break;
        }
        value = value * 10 + (character - '0');
    }
    if (!have_stack) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_KEY_UNRESOLVED,
                                  source_program_counter);
    }
    *output_stack_identifier = value;
    return RUNTIMEC_STATUS_SUCCESS;
}

/* 两个结果的写准备：result0 = vid，result1 = `[2]int32 [stackid, s0]`。 */
static runtimec_status_t runtimec_host_vthread_write_results(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction, int32_t vthread_identifier,
    int32_t stack_identifier, binrwir_execution_exit_t *output_exit,
    uint32_t source_program_counter) {
    int32_t identifier = vthread_identifier;
    /*
     * 第二结果是 `[2]int32`（8 字节）。**按声明的形状写**：spec 说它是 `[2]int32`，
     * 而 write_body 要求与当前 body 等长——若槽还没按该形状建（局部未物化），
     * 这里会失败而不是写入一个错长度的值。
     */
    int32_t position[2];

    position[0] = stack_identifier;
    position[1] = (int32_t)frame->logical_program_counter;
    if (vmbin_value_access_write_body(
            frame, &instruction->write_arguments.data[0],
            runtimec_bytes_view_make((const uint8_t *)(const void *)&identifier,
                                     sizeof(identifier)),
            NULL) != RUNTIMEC_STATUS_SUCCESS ||
        vmbin_value_access_write_body(
            frame, &instruction->write_arguments.data[1],
            runtimec_bytes_view_make((const uint8_t *)(const void *)position,
                                     sizeof(position)),
            NULL) != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_WRITE_FAILED,
                                  source_program_counter);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/* 读两个位置分量：0 读参、2 写参，且帧路径必须存在。 */
static runtimec_status_t runtimec_host_vthread_pc_precondition(
    const binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit,
    uint32_t source_program_counter) {
    if (instruction->read_arguments.count != 0U ||
        instruction->write_arguments.count != 2U) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_ARITY,
                                  source_program_counter);
    }
    if (frame == NULL || frame->frame_path.size == 0U ||
        frame->frame_path.data == NULL) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_KEY_UNRESOLVED,
                                  source_program_counter);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t runtimec_host_vthread_pc(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;
    int32_t vthread_identifier = 0;
    int32_t stack_identifier = 0;
    size_t root_end = 0U;
    runtimec_status_t status;

    *output_exit = (binrwir_execution_exit_t){0};
    status = runtimec_host_vthread_pc_precondition(frame, instruction,
                                                   output_exit,
                                                   source_program_counter);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_host_vthread_path_root_end(
            frame->frame_path, &root_end, output_exit, source_program_counter);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_host_vthread_identifier_from_path(
            frame->frame_path, root_end, &vthread_identifier, output_exit,
            source_program_counter);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_host_vthread_stack_from_path(
            frame->frame_path, root_end, &stack_identifier, output_exit,
            source_program_counter);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_host_vthread_write_results(
            frame, instruction, vthread_identifier, stack_identifier,
            output_exit, source_program_counter);
    }
    return status;
}

/*
 * `kvspace/key·abs(x) -> *T`：取址。产一个 Ptr，body = 操作数所在的**绝对 key 路径**。
 *
 * 与 `kvspace·` 族分开的原因：本族的 base 是一次存储访问的基座，而本算子收的是
 * **key 表达式**本身——它不读写那个位置的值，只把「位置」变成一等值。
 *
 * 三条判据：
 *   1. **键从值访问层取**（`vmbin_value_access_argument_key`：LOCAL 拼帧路径、
 *      STATIC 用符号池文本）。本文件不拼 key——拼了就成第二份地址解析。
 *   2. **结果必须是指针形态**：`kvspaceNewPtr` 把 `*` 写进 **langtype**（不再有
 *      head ptr 位）、body=目标路径、
 *      attr 带 target kindexpr。不手拼 wire。
 *   3. **不可寻址即失败**：字面量、函数引用、算出来的表达式都没有位置可言，
 *      返回具名诊断，不猜一个 key 出来。
 */
/*
 * 按取址操作的读参造一条 Ptr wire：`*` + 目标类型（`kvspaceNewPtr` 自补 `*`）。
 *
 * 单独成函数的原因：handler 只负责「取 key → 造 wire → 走创建型写」这条编排，
 * 而「类型怎么取、前缀怎么剥」是另一件事，两者的失败出口也不同。
 */
static runtimec_status_t runtimec_host_build_pointer_wire(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction, const char *target,
    uint8_t **output_wire, uint32_t *output_wire_size,
    runtimec_error_t *error) {
    runtimec_string_view_t declared;
    char kindexpr[RUNTIMEC_HOST_KEY_CAPACITY];
    size_t kindexpr_size = 0U;
    runtimec_status_t status;

    /*
     * `target` 是**寻址面**的 key 文本，恒 utf8（`frame_target_key` 原样拼接
     * frame_path 与符号名，不做编码转换）——「值默认 utf32」只适用于值，
     * 指针 body 是目标 key 路径，永远 utf8（规范 `26-字符串编码` §一）。
     */
    status = vmbin_value_access_langtype(
        frame, &instruction->read_arguments.data[0], &declared, error);
    if (status != RUNTIMEC_STATUS_SUCCESS || declared.size == 0U) {
        return RUNTIMEC_STATUS_INVALID;
    }
    /* 逐层剥 `*`/`@`：读参本身可能就是一个指针（**T），只保留基类型。 */
    while (kindexpr_size < declared.size &&
           (declared.data[kindexpr_size] == '*' ||
            declared.data[kindexpr_size] == '@')) {
        kindexpr_size += 1U;
    }
    if (declared.size == kindexpr_size ||
        declared.size - kindexpr_size + 1U > sizeof(kindexpr)) {
        return RUNTIMEC_STATUS_INVALID;
    }
    memcpy(kindexpr, declared.data + kindexpr_size, declared.size - kindexpr_size);
    kindexpr[declared.size - kindexpr_size] = '\0';
    if (kvspaceNewPtr(kindexpr, target, output_wire, output_wire_size) != 0 ||
        *output_wire == NULL) {
        return RUNTIMEC_STATUS_ERROR;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * 取址 handler 的前半段：核 arity → 取读参的绝对 key → 造 Ptr wire。
 *
 * 失败一律走 `runtimec_host_fail` 并返回它的状态：调用方只判成功与否，
 * 不需要再区分「arity 错 / key 取不到 / wire 造不出」——那三类已经在 output_exit
 * 的诊断码上分开了。
 */
static runtimec_status_t runtimec_host_address_of_wire(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction, char *target,
    size_t target_capacity, uint8_t **output_wire, uint32_t *output_wire_size,
    runtimec_error_t *error, binrwir_execution_exit_t *output_exit) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;
    runtimec_status_t status;

    if (instruction->read_arguments.count != 1U ||
        instruction->write_arguments.count != 1U) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_ARITY,
                                  source_program_counter);
    }
    status = vmbin_value_access_argument_key(
        frame, &instruction->read_arguments.data[0], target, target_capacity,
        error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  RUNTIMEC_HOST_DIAG_KEY_UNRESOLVED,
                                  source_program_counter);
    }
    status = runtimec_host_build_pointer_wire(frame, instruction, target,
                                              output_wire, output_wire_size,
                                              error);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    return runtimec_host_fail(
        output_exit,
        status == RUNTIMEC_STATUS_ERROR ? RUNTIMEC_STATUS_ERROR
                                        : RUNTIMEC_STATUS_INVALID,
        status == RUNTIMEC_STATUS_ERROR ? RUNTIMEC_HOST_DIAG_BACKEND_FAILED
                                        : RUNTIMEC_HOST_DIAG_KEY_UNRESOLVED,
        source_program_counter);
}

static runtimec_status_t runtimec_host_key_address_of(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    char target[RUNTIMEC_HOST_KEY_CAPACITY];
    uint8_t *wire = NULL;
    uint32_t wire_size = 0U;
    runtimec_error_t error;
    runtimec_status_t status;

    runtimec_error_reset(&error);
    *output_exit = (binrwir_execution_exit_t){0};
    status = runtimec_host_address_of_wire(frame, instruction, target,
                                           sizeof(target), &wire, &wire_size,
                                           &error, output_exit);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    /*
     * 走**创建型**的整值写（`vmbin_handler_write_new_value`），不走 `value_access_write`。
     *
     * 后者是「原地写」：长度必须与当前 body **完全相等**（`value_access.h` 的
     * `SIZE_MISMATCH`）。而取址产出的是一条**新的** wire——长度由目标 key 决定，
     * 与写槽当前宽度无关（实测：写槽 8 字节、指针 wire 16+路径长，直接原地写
     * 报 `0x4606`）。创建型路径才是「造一条 wire 并写进目标 key」的共同语义，
     * 且它顺带完成绑定（少那一步，后续读该槽是 `UNBOUND_SLOT`）。
     *
     * `bytes` 传 NULL 是因为整条 wire 已经造好了：该函数按写参声明的 langtype
     * 另造 wire 的用法不适用于本算子（指针的 langtype 来自目标而非写槽声明）。
     */
    {
        runtimec_status_t created = runtimec_host_write_pointer_wire(
            frame, instruction, wire, (size_t)wire_size, output_exit);
        free(wire);
        return created;
    }
}

/*
 * `member_key_text` 的诊断码 → 本层 `detail_code`。
 *
 * 两个域名**不能混**（见 `handler_call.h` 的枚举注释）：contract 的 1..5 直接进
 * `detail_code` 的话，看日志的人分不出「3」是 contract 的 `UNSUPPORTED_LANGTYPE`
 * 还是执行窗口的 3。转码是这一步的职责。
 *
 * **`switch` 不带 `default`**：contract 层将来加诊断时 `-Wswitch` 会在这里报警，
 * 提醒补映射。末尾的 `return` 在 `switch` 之外，所以不破坏这个检查——它兜的是
 * 「运行期传进来一个枚举里没有的数」，不是「漏了一个 case」。
 *
 * 不认识的码报 `MALFORMED_BODY` 而不是 `NONE`：`0` 恒为「未设置」，失败路径
 * 永远不许上报 0（`contract/diagnostic_band.h` 的约束）。
 */
static vmbin_handler_call_diagnostic_t runtimec_host_key_tostr_diagnostic(
    int64_t code) {
    switch ((runtimec_member_key_text_diagnostic_t)code) {
    case RUNTIMEC_MEMBER_KEY_TEXT_DIAG_INSUFFICIENT_CAPACITY:
        return VMBIN_HANDLER_CALL_DIAG_INSUFFICIENT_CAPACITY;
    case RUNTIMEC_MEMBER_KEY_TEXT_DIAG_UNSUPPORTED_LANGTYPE:
        return VMBIN_HANDLER_CALL_DIAG_UNSUPPORTED_LANGTYPE;
    case RUNTIMEC_MEMBER_KEY_TEXT_DIAG_MALFORMED_BODY:
        return VMBIN_HANDLER_CALL_DIAG_MALFORMED_BODY;
    case RUNTIMEC_MEMBER_KEY_TEXT_DIAG_NON_FINITE:
        return VMBIN_HANDLER_CALL_DIAG_NON_FINITE;
    case RUNTIMEC_MEMBER_KEY_TEXT_DIAG_INVALID_REQUEST:
        return VMBIN_HANDLER_CALL_DIAG_WRONG_ARITY;
    }
    return VMBIN_HANDLER_CALL_DIAG_MALFORMED_BODY;
}

/*
 * `kvspace/key·tostr(value) -> 键文本`：值 → 成员键文本（逐字铁律的唯一实现）。
 *
 * 它**不碰存储**——不读 base、不拼父路径、不定位目标。这些属于定位算子
 * （`kvspace/key·resolveptr`）。本算子只回答一个问题：**这个值作成员键时，
 * 路径段是什么文本**。
 *
 * ── 为什么用上界栈缓冲，不用两段式 ────────────────────────────────────
 *
 * `contract/member_key_text` 是两段式的（缓冲不足时回报所需长度、调用方重试）。
 * **本 handler 故意不用它那一半能力**，因为这里有一个**有据可依的上界**：键文本
 * 若长于 `VMBIN_HANDLER_KEY_TEXT_MAXIMUM`（1023），拼上父路径后必然超过存储能
 * 表达的完整 key 上界，定位算子那边一定以 `KEY_TOO_LONG` 拒绝。所以按 1023 + 1
 * 开栈缓冲是「按最大可能长度开空间」，不是「拍一个数试试看」——`0x4501` 治的是
 * 后者。**不是忘了两段式**：两段式仍是 contract 层的能力，别的调用方仍该用。
 *
 * ── 为什么报 `0x4A` 段码而不是 `0x46` 段码 ────────────────────────────
 *
 * 本算子的失败全部来自「值 → 文本」这一步，与存储访问无关。`0x4A00` 是搬运层段
 * （`handler_call.h`），`0x4600` 是 host 存储段。**这不是分层错误，是失败来源
 * 不同**——把它硬塞进 host 段会让「host 段码 = 存储访问出错」这条判据失真。
 */
/*
 * 读参 → (声明 langtype, 值体视图)。两个失败各自具名，不合并成一条：
 *
 *   取不到类型 ⇒ `UNSUPPORTED_LANGTYPE`（搬运层段 `0x4A07`）——值定位得到，
 *              只是没有类型。**不是** `KEY_UNRESOLVED`（host 段 `0x4603`）。
 *   定位失败   ⇒ `OPERAND_INVALID`（host 段 `0x4605`）——这才是真的拿不到值。
 */
static runtimec_status_t runtimec_host_key_tostr_operand(
    binrwir_execution_frame_t *frame, const binrwir_argument_t *argument,
    runtimec_string_view_t *output_langtype, vmbin_value_location_t *output_value,
    binrwir_execution_exit_t *output_exit, uint32_t source_program_counter) {
    runtimec_error_t error;
    runtimec_status_t status;

    runtimec_error_reset(&error);
    /*
     * 类型取**声明** langtype，不取值上带的：槽可以是 None（空 body、wire 里
     * 没有 langtype 记录），那时只有声明类型答得出这个值该按什么种类格式化。
     */
    status = vmbin_value_access_langtype(frame, argument, output_langtype,
                                         &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(
            output_exit, RUNTIMEC_STATUS_UNSUPPORTED,
            VMBIN_HANDLER_CALL_DIAG_UNSUPPORTED_LANGTYPE,
            source_program_counter);
    }
    /* 零拷贝取 body：`locate_body` 对字面量与绑定都给只读视图。 */
    status = vmbin_value_access_locate_body(frame, argument, output_value,
                                            &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, status,
                                  RUNTIMEC_HOST_DIAG_OPERAND_INVALID,
                                  source_program_counter);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * `member_key_text` 失败 → 结构化 exit。
 *
 * 两条路径的分流判据是**可重试性**：
 *   `INSUFFICIENT_CAPACITY` 且所需长度超过存储上界 ⇒ `KEY_TOO_LONG`
 *   （搬运层段 `0x4A0A`，**不可重试**：换多大缓冲都写不进存储）；
 *   其余 ⇒ 按 contract 诊断转码。
 *
 * 合成一条会让调用方无限重试——contract 那句「给我更大的缓冲」在这里不成立，
 * 因为缓冲已经是存储极限。
 */
static runtimec_status_t runtimec_host_key_tostr_fail(
    binrwir_execution_exit_t *output_exit, runtimec_status_t status,
    int64_t diagnostic, size_t required_length, uint32_t source_program_counter) {
    if (status == RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY &&
        required_length > VMBIN_HANDLER_KEY_TEXT_MAXIMUM) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_UNSUPPORTED,
                                  VMBIN_HANDLER_CALL_DIAG_KEY_TOO_LONG,
                                  source_program_counter);
    }
    return runtimec_host_fail(output_exit, status,
                              (uint32_t)runtimec_host_key_tostr_diagnostic(
                                  diagnostic),
                              source_program_counter);
}

static runtimec_status_t runtimec_host_key_tostr(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;
    char key_text[VMBIN_HANDLER_KEY_TEXT_MAXIMUM + 1U];
    runtimec_string_view_t langtype;
    vmbin_value_location_t value;
    runtimec_error_t error;
    runtimec_status_t status;
    size_t key_length = 0U;

    runtimec_error_reset(&error);
    *output_exit = (binrwir_execution_exit_t){0};
    /*
     * arity 报**搬运层段**（`0x4A01`）而不是 host 段（`0x4601`）：本算子的全部
     * 失败都发生在「值 → 文本」这一步，与存储访问无关。
     */
    if (instruction->read_arguments.count != 1U ||
        instruction->write_arguments.count != 1U) {
        return runtimec_host_fail(
            output_exit, RUNTIMEC_STATUS_INVALID,
            VMBIN_HANDLER_CALL_DIAG_WRONG_ARITY, source_program_counter);
    }
    status = runtimec_host_key_tostr_operand(
        frame, &instruction->read_arguments.data[0], &langtype, &value,
        output_exit, source_program_counter);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    status = runtimec_member_key_text(
        langtype, runtimec_bytes_view_make(value.read_bytes, value.size),
        key_text, sizeof(key_text), &key_length, &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_key_tostr_fail(output_exit, status, error.code,
                                            key_length, source_program_counter);
    }
    /* 走创建型出口：造 char/utf8 wire、整条写进读参、并完成绑定。 */
    return vmbin_handler_write_new_value(
        frame, instruction, (const uint8_t *)key_text, key_length, output_exit);
}

static const binrwir_registry_entry_t runtimec_host_entry_table[] = {
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_VTHREAD_PC,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = runtimec_host_vthread_pc},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_KEY_ADDRESS_OF,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = runtimec_host_key_address_of},
    /* `key·tostr`：值 → 成员键文本。不碰存储，故不是 EFFECT；也不让出控制权。 */
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_KEY_TOSTRING,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = runtimec_host_key_tostr},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_KVSPACE_WATCH,
     .flags = BINRWIR_REGISTRY_FLAG_MAY_YIELD,
     .handler = runtimec_host_kvspace_watch},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_RANDOM_UNSIGNED_64,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_host_random_unsigned_64},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_RANDOM_SIGNED_63,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_host_random_signed_63},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_RANDOM_BELOW,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_host_random_below},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_VTHREAD_CREATE,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_host_vthread_not_implemented},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_VTHREAD_RUN,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_host_vthread_not_implemented},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_VTHREAD_CALL,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_host_vthread_not_implemented},
    /* sleep 会阻塞驱动线程，可能让出控制权。 */
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_VTHREAD_SLEEP,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT | BINRWIR_REGISTRY_FLAG_MAY_YIELD,
     .handler = runtimec_host_vthread_sleep},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_VTHREAD_SET_STATUS,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_host_vthread_setstatus},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_KVSPACE_DELETE,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_host_kvspace_delete},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_KVSPACE_DELETE_TREE,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_host_kvspace_delete_tree},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_KVSPACE_COPY,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_host_kvspace_copy},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_KVSPACE_COPY_DIRECTORY,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_host_kvspace_copy_directory},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_KVSPACE_COPY_LIST,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_host_kvspace_copy_list},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_KVSPACE_LIST_LENGTH,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_host_kvspace_list_length},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_KVSPACE_LIST_AT,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_host_kvspace_list_at},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_KVSPACE_MAKE_INDEX,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_host_kvspace_make_index},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_KVSPACE_EXTENSION_INDEX,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_host_kvspace_extension_index},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_KVSPACE_DELETE_EXTENSION_INDEX,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_host_kvspace_delete_extension_index},
};

const binrwir_registry_entry_t *runtimec_host_handler_entries(
    size_t *output_count) {
    if (output_count == NULL) {
        return NULL;
    }
    *output_count =
        sizeof(runtimec_host_entry_table) / sizeof(runtimec_host_entry_table[0]);
    return runtimec_host_entry_table;
}
