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

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "binrwir/execution_exit.h"
#include "binrwir/execution_frame.h"
#include "binrwir/instruction.h"
#include "binrwir/operation_identifiers.h"
#include "binrwir/program.h"
#include "contract/status.h"
#include "kvspace/kvspace.h"
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
static runtimec_status_t runtimec_host_fail(
    binrwir_execution_exit_t *output_exit, runtimec_status_t status,
    uint32_t source_program_counter) {
    *output_exit = (binrwir_execution_exit_t){0};
    output_exit->kind = BINRWIR_EXECUTION_EXIT_ERROR;
    output_exit->status = status;
    output_exit->logical_program_counter = source_program_counter;
    output_exit->resume_logical_program_counter = source_program_counter;
    return status;
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
static runtimec_status_t runtimec_host_kvspace_get(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;
    char key[RUNTIMEC_HOST_KEY_CAPACITY];
    uint8_t *value = NULL;
    uint32_t value_size = 0U;
    void *handle = NULL;
    runtimec_error_t error;
    runtimec_status_t status;

    runtimec_error_reset(&error);
    *output_exit = (binrwir_execution_exit_t){0};
    if (instruction->read_arguments.count != 1U ||
        instruction->write_arguments.count != 1U) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  source_program_counter);
    }
    status = runtimec_vthread_status_handle(frame, &handle);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_host_static_key(frame, &instruction->read_arguments.data[0],
                                          key, sizeof(key));
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, status, source_program_counter);
    }
    if (kvspaceGet(handle, key, 1, &value, &value_size) != 0) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                                  source_program_counter);
    }
    if (value != NULL && value_size != 0U) {
        status = vmbin_value_access_write(
            frame, &instruction->write_arguments.data[0],
            runtimec_bytes_view_make(value, (size_t)value_size), &error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return runtimec_host_fail(output_exit, status, source_program_counter);
        }
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
static runtimec_status_t runtimec_host_kvspace_set(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;
    char key[RUNTIMEC_HOST_KEY_CAPACITY];
    char message[256];
    void *handle = NULL;
    runtimec_error_t error;
    runtimec_status_t status;

    runtimec_error_reset(&error);
    *output_exit = (binrwir_execution_exit_t){0};
    if (instruction->read_arguments.count != 2U) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  source_program_counter);
    }
    status = runtimec_vthread_status_handle(frame, &handle);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_host_static_key(frame, &instruction->read_arguments.data[0],
                                          key, sizeof(key));
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, status, source_program_counter);
    }
    /*
     * 整值写要的是**完整 wire**，不是 body。
     *
     * 这里曾直接 `locate` 后把 `location.read_bytes` 交给 `kvspaceSetValue`：
     * 对字面量那是对的（`locate` 对字面量给整条 wire），但对**普通槽**它给的是
     * body，于是写进去的是一条没有 head 的裸缓冲——静默失败或写成畸形值。
     *
     * 现在按 `is_whole_wire` 分流，**不自己推断该不该剥**（那是 `locate_body`
     * 的职责）。两句都在这里，因为「这一处要哪一种」只有调用点知道。
     */
    {
        vmbin_value_location_t location;
        runtimec_bytes_view_t wire;

        status = vmbin_value_access_locate(
            frame, &instruction->read_arguments.data[1], &location, &error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return runtimec_host_fail(output_exit, status, source_program_counter);
        }
        if (location.is_whole_wire) {
            wire = runtimec_bytes_view_make(location.read_bytes, location.size);
        } else {
            /*
             * body 视图 → 还原整条 wire：基址 = body - 固定头长度（经合同函数，
             * 不写偏移算术）；总长 = 固定头 + bodylen + attrslen，两者都从
             * wire 头读出。attr 区没有独立长度前缀，所以长得从 head 里拿。
             */
            const runtimec_value_body_address_t body_address =
                (runtimec_value_body_address_t)(uintptr_t)location.read_bytes;
            const runtimec_xvalue_address_t xvalue_address =
                runtimec_value_body_xvalue_address(body_address);
            const kvspaceXvalueHead *head;

            if (xvalue_address == RUNTIMEC_XVALUE_ADDRESS_NONE) {
                return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                          source_program_counter);
            }
            head = (const kvspaceXvalueHead *)(uintptr_t)xvalue_address;
            wire = runtimec_bytes_view_make(
                (const void *)(uintptr_t)xvalue_address,
                (size_t)KVSPACE_XVALUE_HEAD_LEN +
                    (size_t)kvspaceXvalueHeadLen(head) +
                    (size_t)kvspaceXvalueHeadAttrsLen(head));
        }
        message[0] = '\0';
        /* 第 4 参（resolve）语义与旧实现一致：写侧不跟随 Ptr。 */
        if (kvspaceSetValue(handle, key, wire.data, (uint32_t)wire.size, message,
                            sizeof(message)) != 0) {
            return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                                      source_program_counter);
        }
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

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
                                  source_program_counter);
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
        return runtimec_host_fail(output_exit, status, source_program_counter);
    }
    if (kvspaceWatch(handle, key, expected_location.read_bytes,
                     (uint32_t)expected_location.size, UINT64_C(1000000),
                     &observed, &observed_size) != 0) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                                  source_program_counter);
    }
    if (observed != NULL && observed_size != 0U) {
        status = vmbin_value_access_write(
            frame, &instruction->write_arguments.data[0],
            runtimec_bytes_view_make(observed, (size_t)observed_size), &error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return runtimec_host_fail(output_exit, status, source_program_counter);
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
static runtimec_status_t runtimec_host_random_unsigned_64(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;
    const uint64_t value = runtimec_host_random_u64();
    runtimec_error_t error;
    runtimec_status_t status;

    runtimec_error_reset(&error);
    *output_exit = (binrwir_execution_exit_t){0};
    if (instruction->write_arguments.count != 1U) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  source_program_counter);
    }
    status = vmbin_value_access_write_body(
        frame, &instruction->write_arguments.data[0],
        runtimec_bytes_view_make(&value, sizeof(value)), &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, status, source_program_counter);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/* `random·int63()`：取 64 位随机数的高 63 位，保证非负（旧实现 `>> 1`）。 */
static runtimec_status_t runtimec_host_random_signed_63(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;
    const int64_t value = (int64_t)(runtimec_host_random_u64() >> 1U);
    runtimec_error_t error;
    runtimec_status_t status;

    runtimec_error_reset(&error);
    *output_exit = (binrwir_execution_exit_t){0};
    if (instruction->write_arguments.count != 1U) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  source_program_counter);
    }
    status = vmbin_value_access_write_body(
        frame, &instruction->write_arguments.data[0],
        runtimec_bytes_view_make(&value, sizeof(value)), &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, status, source_program_counter);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * `random·intn(bound)`：读参 1 是 int64 上界，结果写写参 1。
 * 语义按旧实现（`rwir_random.c:30`）：`bound == 0` 返回 0，否则取模。
 */
static runtimec_status_t runtimec_host_random_below(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;
    vmbin_value_location_t bound_location;
    uint64_t bound = 0U;
    uint64_t value;
    runtimec_error_t error;
    runtimec_status_t status;

    runtimec_error_reset(&error);
    *output_exit = (binrwir_execution_exit_t){0};
    if (instruction->read_arguments.count != 1U ||
        instruction->write_arguments.count != 1U) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  source_program_counter);
    }
    status = vmbin_value_access_locate_body(frame, &instruction->read_arguments.data[0],
                                       &bound_location, &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, status, source_program_counter);
    }
    if (bound_location.read_bytes == NULL ||
        bound_location.size != sizeof(bound)) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  source_program_counter);
    }
    memcpy(&bound, bound_location.read_bytes, sizeof(bound));
    value = bound == 0U ? 0U : runtimec_host_random_u64() % bound;
    status = vmbin_value_access_write_body(
        frame, &instruction->write_arguments.data[0],
        runtimec_bytes_view_make(&value, sizeof(value)), &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, status, source_program_counter);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * `vthread·setstatus(status)`：把状态写进 `/vthread/{vid}/‥status`。
 *
 * 第一版只写状态键，**不推进 PC**。旧实现（`runtime/src/rwir_vthread.c:100`）
 * 经 `kvlangVthreadNextPc` 同时写 pc 与 status，那需要 runtimec 侧尚未具备的
 * PC 后继计算；只写 status 不会让程序停在错的位置，而写错 pc 会。
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
    runtimec_status_t status;

    runtimec_error_reset(&error);
    *output_exit = (binrwir_execution_exit_t){0};
    if (instruction->read_arguments.count != 1U) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  source_program_counter);
    }
    status = runtimec_vthread_status_handle(frame, &handle);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_vthread_status_key(
            frame->frame_path, runtimec_string_view_from_c_string("/‥status"),
            status_key, sizeof(status_key));
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = vmbin_value_access_locate_body(frame, &instruction->read_arguments.data[0],
                                           &status_location, &error);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, status, source_program_counter);
    }
    status = runtimec_vthread_status_write(
        handle, status_key,
        runtimec_bytes_view_make(status_location.read_bytes,
                                 status_location.size));
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, status, source_program_counter);
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
static runtimec_status_t runtimec_host_vthread_sleep(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;
    vmbin_value_location_t duration_location;
    int64_t duration_nanoseconds = 0;
    runtimec_error_t error;
    runtimec_status_t status;

    runtimec_error_reset(&error);
    *output_exit = (binrwir_execution_exit_t){0};
    if (instruction->read_arguments.count != 1U) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  source_program_counter);
    }
    status = vmbin_value_access_locate_body(frame, &instruction->read_arguments.data[0],
                                       &duration_location, &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, status, source_program_counter);
    }
    if (duration_location.read_bytes == NULL ||
        duration_location.size != sizeof(duration_nanoseconds)) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  source_program_counter);
    }
    memcpy(&duration_nanoseconds, duration_location.read_bytes,
           sizeof(duration_nanoseconds));
    if (duration_nanoseconds > 0) {
        const struct timespec interval = {
            .tv_sec = (time_t)(duration_nanoseconds / INT64_C(1000000000)),
            .tv_nsec = (long)(duration_nanoseconds % INT64_C(1000000000)),
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
                              instruction->source_logical_program_counter);
}

/*
 * 以下 10 条是 kvspace 的目录操作，形状与已做的三条一致：
 * 读参里的路径走 `runtimec_host_static_key`（STATIC only），其余参数按引用
 * 定位后取值，结果按各算子的既定语义写入写参或直接生效。
 *
 * 取参顺序全部按旧运行时 `runtime/src/rwir_kv.c` 核过，不是按「自然顺序」猜的：
 *   单路径（del / deltree / rmindexext）→ `run_path_op`，第 1 读参是路径；
 *   双路径（cp / cpdir / cplist）→ `run_two_path_op`，src、dst 依次；
 *   mkindex → 路径 + capacity；extindex → 路径 + ext 路径；
 *   listlen → 路径，写 int64；listat → 路径 + 下标，写 char。
 */

/* 单路径操作：读参 1 是路径，无写参。 */
static runtimec_status_t runtimec_host_single_path_operation(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit,
    int (*operation)(void *handle, const char *path, char *error,
                     uint32_t error_capacity)) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;
    char path[RUNTIMEC_HOST_KEY_CAPACITY];
    char message[256];
    void *handle = NULL;
    runtimec_status_t status;

    *output_exit = (binrwir_execution_exit_t){0};
    if (instruction->read_arguments.count != 1U) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  source_program_counter);
    }
    status = runtimec_vthread_status_handle(frame, &handle);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_host_static_key(frame, &instruction->read_arguments.data[0],
                                          path, sizeof(path));
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, status, source_program_counter);
    }
    message[0] = '\0';
    if (operation(handle, path, message, sizeof(message)) != 0) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                                  source_program_counter);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/* 双路径操作：读参 1 是源、读参 2 是目标，无写参。 */
static runtimec_status_t runtimec_host_two_path_operation(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit,
    int (*operation)(void *handle, const char *source,
                     const char *destination, char *error,
                     uint32_t error_capacity)) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;
    char source[RUNTIMEC_HOST_KEY_CAPACITY];
    char destination[RUNTIMEC_HOST_KEY_CAPACITY];
    char message[256];
    void *handle = NULL;
    runtimec_status_t status;

    *output_exit = (binrwir_execution_exit_t){0};
    if (instruction->read_arguments.count != 2U) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  source_program_counter);
    }
    status = runtimec_vthread_status_handle(frame, &handle);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_host_static_key(frame, &instruction->read_arguments.data[0],
                                          source, sizeof(source));
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_host_static_key(frame, &instruction->read_arguments.data[1],
                                          destination, sizeof(destination));
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, status, source_program_counter);
    }
    message[0] = '\0';
    if (operation(handle, source, destination, message, sizeof(message)) != 0) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                                  source_program_counter);
    }
    return RUNTIMEC_STATUS_SUCCESS;
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
static runtimec_status_t runtimec_host_kvspace_remove_index_extension(
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
    return runtimec_host_two_path_operation(frame, instruction, output_exit,
                                            kvspaceCp);
}
static runtimec_status_t runtimec_host_kvspace_copy_directory(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return runtimec_host_two_path_operation(frame, instruction, output_exit,
                                            kvspaceCpTree);
}
static runtimec_status_t runtimec_host_kvspace_copy_list(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return runtimec_host_two_path_operation(frame, instruction, output_exit,
                                            kvspaceCpList);
}

/* `kvspace·mkindex(path, capacity)`：路径 + 容量，无写参。 */
static runtimec_status_t runtimec_host_kvspace_make_index(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;
    char path[RUNTIMEC_HOST_KEY_CAPACITY];
    char message[256];
    vmbin_value_location_t capacity_location;
    uint64_t capacity = 0U;
    void *handle = NULL;
    runtimec_error_t error;
    runtimec_status_t status;

    runtimec_error_reset(&error);
    *output_exit = (binrwir_execution_exit_t){0};
    if (instruction->read_arguments.count != 2U) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  source_program_counter);
    }
    status = runtimec_vthread_status_handle(frame, &handle);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_host_static_key(frame, &instruction->read_arguments.data[0],
                                          path, sizeof(path));
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = vmbin_value_access_locate_body(
            frame, &instruction->read_arguments.data[1], &capacity_location,
            &error);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, status, source_program_counter);
    }
    if (capacity_location.read_bytes == NULL ||
        capacity_location.size != sizeof(capacity)) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  source_program_counter);
    }
    memcpy(&capacity, capacity_location.read_bytes, sizeof(capacity));
    message[0] = '\0';
    if (kvspaceMkindex(handle, path, (uint32_t)capacity, message,
                       sizeof(message)) != 0) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                                  source_program_counter);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * `kvspace·extindex(path, ext_path)`：两个路径，都是 STATIC。
 * 注意第二个是**字符串参数**而非存储操作对象，但它同样是编码期确定的路径。
 */
static runtimec_status_t runtimec_host_kvspace_extension_index(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;
    char path[RUNTIMEC_HOST_KEY_CAPACITY];
    char extension_path[RUNTIMEC_HOST_KEY_CAPACITY];
    char message[256];
    void *handle = NULL;
    runtimec_status_t status;

    *output_exit = (binrwir_execution_exit_t){0};
    if (instruction->read_arguments.count != 2U) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  source_program_counter);
    }
    status = runtimec_vthread_status_handle(frame, &handle);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_host_static_key(frame, &instruction->read_arguments.data[0],
                                          path, sizeof(path));
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_host_static_key(frame, &instruction->read_arguments.data[1],
                                          extension_path,
                                          sizeof(extension_path));
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, status, source_program_counter);
    }
    message[0] = '\0';
    if (kvspaceMkindexExt(handle, path, extension_path, message,
                          sizeof(message)) != 0) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                                  source_program_counter);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * `kvspace·listlen(path) -> int64`：读参 1 是路径，结果写写参。
 *
 * 旧实现在路径解析不出时返回 0 而不是报错（`rwir_kv.c:586`），这里同语义：
 * 空目录与取不到路径都给出 0，不制造「有错但要靠猜」的状态。
 */
static runtimec_status_t runtimec_host_kvspace_list_length(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    const uint32_t source_program_counter =
        instruction->source_logical_program_counter;
    char path[RUNTIMEC_HOST_KEY_CAPACITY];
    int32_t count = 0;
    int64_t result = 0;
    void *handle = NULL;
    runtimec_error_t error;
    runtimec_status_t status;

    runtimec_error_reset(&error);
    *output_exit = (binrwir_execution_exit_t){0};
    if (instruction->read_arguments.count != 1U ||
        instruction->write_arguments.count != 1U) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  source_program_counter);
    }
    status = runtimec_vthread_status_handle(frame, &handle);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_host_static_key(frame, &instruction->read_arguments.data[0],
                                          path, sizeof(path));
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, status, source_program_counter);
    }
    /* 枚举失败不算错：count 保持 0，与旧实现一致。 */
    if (kvspaceListLen(handle, path, 0, 0, &count) == 0) {
        result = (int64_t)count;
    }
    status = vmbin_value_access_write_body(
        frame, &instruction->write_arguments.data[0],
        runtimec_bytes_view_make(&result, sizeof(result)), &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_host_fail(output_exit, status, source_program_counter);
    }
    return RUNTIMEC_STATUS_SUCCESS;
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
                                  source_program_counter);
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
        return runtimec_host_fail(output_exit, status, source_program_counter);
    }
    if (index_location.read_bytes == NULL ||
        index_location.size != sizeof(index)) {
        return runtimec_host_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                                  source_program_counter);
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
                                          source_program_counter);
            }
        }
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static const binrwir_registry_entry_t runtimec_host_entry_table[] = {
    {BINRWIR_OPERATION_IDENTIFIER_KVSPACE_GET, BINRWIR_REGISTRY_FLAG_EFFECT,
     runtimec_host_kvspace_get},
    {BINRWIR_OPERATION_IDENTIFIER_KVSPACE_SET, BINRWIR_REGISTRY_FLAG_EFFECT,
     runtimec_host_kvspace_set},
    {BINRWIR_OPERATION_IDENTIFIER_KVSPACE_WATCH, BINRWIR_REGISTRY_FLAG_MAY_YIELD,
     runtimec_host_kvspace_watch},
    {BINRWIR_OPERATION_IDENTIFIER_RANDOM_UNSIGNED_64,
     BINRWIR_REGISTRY_FLAG_EFFECT, runtimec_host_random_unsigned_64},
    {BINRWIR_OPERATION_IDENTIFIER_RANDOM_SIGNED_63,
     BINRWIR_REGISTRY_FLAG_EFFECT, runtimec_host_random_signed_63},
    {BINRWIR_OPERATION_IDENTIFIER_RANDOM_BELOW, BINRWIR_REGISTRY_FLAG_EFFECT,
     runtimec_host_random_below},
    {BINRWIR_OPERATION_IDENTIFIER_VTHREAD_CREATE, BINRWIR_REGISTRY_FLAG_EFFECT,
     runtimec_host_vthread_not_implemented},
    {BINRWIR_OPERATION_IDENTIFIER_VTHREAD_RUN, BINRWIR_REGISTRY_FLAG_EFFECT,
     runtimec_host_vthread_not_implemented},
    {BINRWIR_OPERATION_IDENTIFIER_VTHREAD_CALL, BINRWIR_REGISTRY_FLAG_EFFECT,
     runtimec_host_vthread_not_implemented},
    /* sleep 会阻塞驱动线程，可能让出控制权。 */
    {BINRWIR_OPERATION_IDENTIFIER_VTHREAD_SLEEP,
     BINRWIR_REGISTRY_FLAG_EFFECT | BINRWIR_REGISTRY_FLAG_MAY_YIELD,
     runtimec_host_vthread_sleep},
    {BINRWIR_OPERATION_IDENTIFIER_VTHREAD_SET_STATUS,
     BINRWIR_REGISTRY_FLAG_EFFECT, runtimec_host_vthread_setstatus},
    {BINRWIR_OPERATION_IDENTIFIER_KVSPACE_DELETE, BINRWIR_REGISTRY_FLAG_EFFECT,
     runtimec_host_kvspace_delete},
    {BINRWIR_OPERATION_IDENTIFIER_KVSPACE_DELETE_TREE,
     BINRWIR_REGISTRY_FLAG_EFFECT, runtimec_host_kvspace_delete_tree},
    {BINRWIR_OPERATION_IDENTIFIER_KVSPACE_COPY, BINRWIR_REGISTRY_FLAG_EFFECT,
     runtimec_host_kvspace_copy},
    {BINRWIR_OPERATION_IDENTIFIER_KVSPACE_COPY_DIRECTORY,
     BINRWIR_REGISTRY_FLAG_EFFECT, runtimec_host_kvspace_copy_directory},
    {BINRWIR_OPERATION_IDENTIFIER_KVSPACE_COPY_LIST,
     BINRWIR_REGISTRY_FLAG_EFFECT, runtimec_host_kvspace_copy_list},
    {BINRWIR_OPERATION_IDENTIFIER_KVSPACE_LIST_LENGTH,
     BINRWIR_REGISTRY_FLAG_EFFECT, runtimec_host_kvspace_list_length},
    {BINRWIR_OPERATION_IDENTIFIER_KVSPACE_LIST_AT, BINRWIR_REGISTRY_FLAG_EFFECT,
     runtimec_host_kvspace_list_at},
    {BINRWIR_OPERATION_IDENTIFIER_KVSPACE_MAKE_INDEX,
     BINRWIR_REGISTRY_FLAG_EFFECT, runtimec_host_kvspace_make_index},
    {BINRWIR_OPERATION_IDENTIFIER_KVSPACE_EXTENSION_INDEX,
     BINRWIR_REGISTRY_FLAG_EFFECT, runtimec_host_kvspace_extension_index},
    {BINRWIR_OPERATION_IDENTIFIER_KVSPACE_REMOVE_INDEX_EXTENSION,
     BINRWIR_REGISTRY_FLAG_EFFECT, runtimec_host_kvspace_remove_index_extension},
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
