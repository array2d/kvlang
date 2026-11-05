/*
 * 组件：vmbin/handlers/scatter 的实现
 * 作用：scatterarray（stringkeymap 散列形态）——空容器创建、成员计数。
 * 联系：成员在独立子 key 下（`base·[i]`），所以读写走 key 而不是 body 偏移；
 *       本模块直接调 kvspace C ABI，不经 `vmbin/value_access` 的 body 路径。
 * 约束：
 *   - **不按 langtype 分叉**：容器声明的 langtype 原样写进 marker，成员值原样搬运；
 *   - 容器 = 「空 body 的 map 头」+「目录（memindex）」，两件事都做才算建成；
 *   - 句柄为空即失败封闭（不接存储的 session 不该跑到这里）。
 *
 * 本批实现（opid / 名字 / catalog 行）：
 *   0x0110 scatterarray·new    rwir_catalog.def:450  scatterarray-next-index
 *   0x0111 scatterarray·len    rwir_catalog.def:457  scatterarray-next-index
 */

#include "vmbin/handlers/scatter.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>
#include <string.h>

#include "binrwir/execution_exit.h"
#include "binrwir/execution_frame.h"
#include "binrwir/instruction.h"
#include "binrwir/operation_identifiers.h"
#include "contract/error.h"
#include "contract/status.h"
#include "contract/view.h"
#include "kvspace/kvspace.h"
#include "kvspace/xvalue_head.h"
#include "vmbin/handlers/container.h"
#include "vmbin/frame_target_key.h"
#include "vmbin/handlers/handler_call.h"
#include "vmbin/value_access.h"

#define SCATTER_KEY_CAPACITY 512U

/*
 * 成员分隔符：`·`（U+00B7，UTF-8 两个字节）。
 *
 * **不是 `/`**：散列容器的成员是 `base·[i]`，`kvspaceListLen` 用这个前缀枚举。
 * 写成 `base/` 会建出一条与成员不相干的目录——`len` 恒为 0，而成员明明在。
 */
#define SCATTER_MEMBER_SEPARATOR "\xC2\xB7"
#define SCATTER_BODY_CAPACITY 512U

static runtimec_status_t scatter_fail(binrwir_execution_exit_t *output_exit,
                                      runtimec_status_t status,
                                      uint32_t source_program_counter) {
    *output_exit = (binrwir_execution_exit_t){0};
    output_exit->kind = BINRWIR_EXECUTION_EXIT_ERROR;
    output_exit->status = status;
    output_exit->logical_program_counter = source_program_counter;
    output_exit->resume_logical_program_counter = source_program_counter;
    return status;
}

/*
 * 建成一个**空散列容器**——两件事缺一不可：
 *   1. base key 写一条**空 body 的 map 头**（langtype 取自写槽声明）；
 *   2. `kvspaceMkindex` 给 base 建目录，之后成员才能被枚举。
 *
 * 只做 1 的话 `kvspaceListLen` 数不到任何成员（`len` 恒 0）；只做 2 的话 base 上
 * 就没有类型。**两步都必须成功**，否则整体失败。**成功不写哨兵，失败只写 `exit`**。
 */
static void scatter_container_create(const binrwir_execution_frame_t *frame,
                                     const binrwir_instruction_view_t *instruction,
                                     binrwir_execution_exit_t *output_exit) {
    runtimec_string_view_t declared;
    char key[SCATTER_KEY_CAPACITY];
    char langtype_text[SCATTER_KEY_CAPACITY];
    char directory[SCATTER_KEY_CAPACITY + 2U];
    char message[256];
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    runtimec_error_t error;

    runtimec_error_reset(&error);
    if (vmbin_value_access_langtype(
            frame, &instruction->write_arguments.data[0], &declared, &error) !=
            RUNTIMEC_STATUS_SUCCESS ||
        declared.size + 1U > sizeof(langtype_text)) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    memcpy(langtype_text, declared.data, declared.size);
    langtype_text[declared.size] = '\0';
    /* 空 body 的 map 头：body 长度为 0，类型在 attr 里。 */
    if (kvspaceXvalueHeadNewSlack(NULL, 0U, 0U, langtype_text, &wire,
                                  &wire_size) != 0 ||
        wire == NULL || wire_size > UINT32_MAX) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                           VMBIN_HANDLER_CALL_DIAG_CREATE_FAILED,
                           output_exit->logical_program_counter);
        return;
    }
    if (vmbin_frame_target_key(frame, &instruction->write_arguments.data[0], key,
                               sizeof(key)) != RUNTIMEC_STATUS_SUCCESS) {
        free(wire);
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,
                           output_exit->logical_program_counter);
        return;
    }
    message[0] = '\0';
    if (strlen(key) + 2U > sizeof(directory)) {
        free(wire);
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    if (kvspaceSetValue(frame->kvspace_handle, key, wire, (uint32_t)wire_size,
                        message, sizeof(message)) != 0) {
        free(wire);
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                           VMBIN_HANDLER_CALL_DIAG_CREATE_FAILED,
                           output_exit->logical_program_counter);
        return;
    }
    free(wire);
    /* 目录必须单独建：成员按这个前缀枚举。mkindex 自身幂等。 */
    snprintf(directory, sizeof(directory), "%s%s", key,
             SCATTER_MEMBER_SEPARATOR);
    message[0] = '\0';
    if (kvspaceMkindex(frame->kvspace_handle, directory, 0U, message,
                       (uint32_t)sizeof(message)) != 0) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                           VMBIN_HANDLER_CALL_DIAG_CREATE_FAILED,
                           output_exit->logical_program_counter);
    }
}

/* 逻辑函数：一句话——建一个空散列容器。 */
static inline void scatter_new_build(const binrwir_execution_frame_t *frame,
                                     const binrwir_instruction_view_t *instruction,
                                     binrwir_execution_exit_t *output_exit) {
    scatter_container_create(frame, instruction, output_exit);
}

VMBIN_HANDLER_SCATTER_CREATE(scatter_new_handler, scatter_new_build)


/* 容器的**成员目录前缀**（`base·`）。同样是七处重复的一步。 */
static bool scatter_member_prefix(const char *base, char *buffer,
                                  size_t capacity,
                                  binrwir_execution_exit_t *output_exit) {
    if (strlen(base) + 2U > capacity) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return false;
    }
    snprintf(buffer, capacity, "%s%s", base, SCATTER_MEMBER_SEPARATOR);
    return true;
}

/*
 * 散列容器的**实存成员数**（memindex 权威，不是坐标空间容量）。
 *
 * 数的是 base 目录下的直接子项，`resolve = 0`：成员是独立键，不沿 Ptr 跳转。
 * **成功才写 `*out`，失败只写 `exit`**。
 */
static void scatter_member_count(const binrwir_execution_frame_t *frame,
                                 const binrwir_instruction_view_t *instruction,
                                 int64_t *out,
                                 binrwir_execution_exit_t *output_exit) {
    char base[SCATTER_KEY_CAPACITY];
    char directory[SCATTER_KEY_CAPACITY + 2U];
    int32_t count = 0;

    if (vmbin_frame_target_key(frame, &instruction->read_arguments.data[0], base,
                               sizeof(base)) != RUNTIMEC_STATUS_SUCCESS) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,
                           output_exit->logical_program_counter);
        return;
    }
    if (!scatter_member_prefix(base, directory, sizeof(directory),
                               output_exit)) {
        return;
    }
    if (kvspaceListLen(frame->kvspace_handle, directory, 0, 0, &count) != 0) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                           VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,
                           output_exit->logical_program_counter);
        return;
    }
    *out = (int64_t)count;
}

/* 逻辑函数：一句话——数出容器有几个成员。 */
static inline void scatter_length_build(const binrwir_execution_frame_t *frame,
                                        const binrwir_instruction_view_t *instruction,
                                        int64_t *out,
                                        binrwir_execution_exit_t *output_exit) {
    scatter_member_count(frame, instruction, out, output_exit);
}

VMBIN_HANDLER_SCATTER_READ_SCALAR(scatter_length_handler, scatter_length_build,
                                  int64_t)


/* 注册用的薄封装：主体 static inline，注册表要地址，两件事各自独立。 */




/*
 * 取一个参数项的**整条 wire**（head + body + attr）。
 *
 * 为什么不能直接用 `locate` 的 `size`：它对 LOCAL/STATIC 给的是 **body 长度**
 * （wire 语义只对字面量与内联绑定成立）。而 `kvspaceSetValue` 收的是完整
 * XValue，喂它 body 会得到「invalid XValue」。
 *
 * 取法：body 地址经合同的逆几何入口退回值头，总长由值头自己算——**本文件不写
 * 偏移算术**。
 */
static runtimec_status_t scatter_whole_wire(
    const binrwir_execution_frame_t *frame,
    const binrwir_argument_t *argument, runtimec_bytes_view_t *output,
    runtimec_error_t *error) {
    vmbin_value_location_t location;
    runtimec_status_t status =
        vmbin_value_access_locate(frame, argument, &location, error);

    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (location.is_whole_wire) {
        *output = runtimec_bytes_view_make(location.read_bytes, location.size);
        return RUNTIMEC_STATUS_SUCCESS;
    }
    {
        const runtimec_xvalue_address_t base = runtimec_value_body_xvalue_address(
            (runtimec_value_body_address_t)(uintptr_t)location.read_bytes);
        const kvspaceXvalueHead *head;

        if (base == RUNTIMEC_XVALUE_ADDRESS_NONE) {
            return RUNTIMEC_STATUS_INVALID;
        }
        head = (const kvspaceXvalueHead *)(const void *)(uintptr_t)base;
        {
            const uint64_t total = kvspaceXvalueHeadTotalLen(head);

            if (total == 0U || total > UINT32_MAX) {
                return RUNTIMEC_STATUS_INVALID;
            }
            *output = runtimec_bytes_view_make(head, (size_t)total);
        }
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * 成员 key：`<base>·[i]`。坐标段是**物理 key 的一部分**，不是 body 里的偏移——
 * 这是 scatterarray 与 compactarray 的根本区别。
 */
/*
 * 取容器的 **base key**（`<frame_path>/<符号名>`）。第 0 个读参或写参都走它。
 *
 * 抽出来的理由不是行数，是**每一个 scatter 算子都要这一句**：`at`/`set`/`append`/
 * `len`/`newcompact`/`newscatter`/`slice` 全要。一个步骤在七处出现，它就该有名字。
 *
 * 失败即置 `exit` 并返回 false——调用方 `return`。
 */
static bool scatter_base_key(const binrwir_execution_frame_t *frame,
                             const binrwir_argument_t *argument, char *buffer,
                             size_t capacity,
                             binrwir_execution_exit_t *output_exit) {
    if (vmbin_frame_target_key(frame, argument, buffer, capacity) !=
        RUNTIMEC_STATUS_SUCCESS) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,
                           output_exit->logical_program_counter);
        return false;
    }
    return true;
}

static runtimec_status_t scatter_member_key(const char *base, int64_t coordinate,
                                            char *buffer, size_t capacity) {
    const int written = snprintf(buffer, capacity, "%s%s[%lld]", base,
                                 SCATTER_MEMBER_SEPARATOR,
                                 (long long)coordinate);

    if (written < 0 || (size_t)written >= capacity) {
        return RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * 读坐标 `i` 下的成员。**成员缺失（不是 0 长度，而是根本没有这个键）即失败**——
 * 洞不是元素。**成功才写结果，失败只写 `exit`**。
 */
static void scatter_member_read(const binrwir_execution_frame_t *frame,
                                const binrwir_instruction_view_t *instruction,
                                uint8_t *output, size_t output_capacity,
                                binrwir_execution_exit_t *output_exit) {
    char base[SCATTER_KEY_CAPACITY];
    char member[SCATTER_KEY_CAPACITY];
    vmbin_value_location_t coordinate_position;
    uint8_t *stored = NULL;
    uint32_t stored_size = 0U;
    int64_t coordinate = 0;
    runtimec_error_t error;

    runtimec_error_reset(&error);
    if (!scatter_base_key(frame, &instruction->read_arguments.data[0], base,
                          sizeof(base), output_exit)) {
        return;
    }
    if (vmbin_value_access_locate_body(
            frame, &instruction->read_arguments.data[1], &coordinate_position,
            &error) != RUNTIMEC_STATUS_SUCCESS ||
        coordinate_position.size != sizeof(coordinate)) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    memcpy(&coordinate, coordinate_position.read_bytes, sizeof(coordinate));
    if (coordinate < 0 ||
        scatter_member_key(base, coordinate, member, sizeof(member)) !=
            RUNTIMEC_STATUS_SUCCESS) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    /* resolve = 0：成员是独立键，读它就得到它，不沿 Ptr 跳。 */
    if (kvspaceGet(frame->kvspace_handle, member, 0, &stored, &stored_size) != 0 ||
        stored == NULL || stored_size < KVSPACE_XVALUE_HEAD_LEN) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,
                           output_exit->logical_program_counter);
        return;
    }
    /* 成员整条 wire 写进结果槽：两者同形，直接搬。 */
    if (stored_size > output_capacity) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    memcpy(output, stored, stored_size);
}

/* 逻辑函数：一句话——读出坐标 i 下的成员。 */
static inline void scatter_at_build(const binrwir_execution_frame_t *frame,
                                    const binrwir_instruction_view_t *instruction,
                                    uint8_t *output, size_t output_capacity,
                                    binrwir_execution_exit_t *output_exit) {
    scatter_member_read(frame, instruction, output, output_capacity, output_exit);
}


/*
 * `scatterarray·set(a, i, v) -> a`：写坐标 `i` 下的成员。成员是**独立键**，
 * 所以走 `kvspaceSetValue` 整条写，不碰容器的 body。
 */
static inline runtimec_status_t scatter_set(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    char base[SCATTER_KEY_CAPACITY];
    char member[SCATTER_KEY_CAPACITY];
    char message[256];
    vmbin_value_location_t coordinate_value;
    runtimec_bytes_view_t wire_value;
    int64_t coordinate = 0;
    runtimec_error_t error;
    runtimec_status_t status;
    const uint32_t counter = instruction->source_logical_program_counter;

    runtimec_error_reset(&error);
    if (instruction->read_arguments.count != 3U || frame->kvspace_handle == NULL) {
        return scatter_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    status = vmbin_frame_target_key(frame, &instruction->write_arguments.data[0],
                                         base, sizeof(base));
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = vmbin_value_access_locate_body(
            frame, &instruction->read_arguments.data[1], &coordinate_value,
            &error);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return scatter_fail(output_exit, status, counter);
    }
    if (coordinate_value.size != sizeof(coordinate)) {
        return scatter_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    memcpy(&coordinate, coordinate_value.read_bytes, sizeof(coordinate));
    if (coordinate < 0) {
        return scatter_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    status = scatter_member_key(base, coordinate, member, sizeof(member));
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return scatter_fail(output_exit, status, counter);
    }
    /* 值写成**完整 wire**（head + body + attr）：`kvspaceSetValue` 收的就是它，
     * 喂 body 会得到 invalid XValue。成员值原样搬，不重新编码。 */
    status = scatter_whole_wire(frame, &instruction->read_arguments.data[2],
                                &wire_value, &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return scatter_fail(output_exit, status, counter);
    }
    message[0] = '\0';
    if (kvspaceSetValue(frame->kvspace_handle, member, wire_value.data,
                        (uint32_t)wire_value.size, message, sizeof(message)) != 0) {
        return scatter_fail(output_exit, RUNTIMEC_STATUS_ERROR, counter);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * `scatterarray·append(a, v) -> a`：追加到**最大坐标 + 1**（行为矩阵
 * scatterarray-next-index）。坐标从目录里现数，不缓存。
 */
static inline runtimec_status_t scatter_append(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    char base[SCATTER_KEY_CAPACITY];
    char directory[SCATTER_KEY_CAPACITY + 2U];
    char member[SCATTER_KEY_CAPACITY];
    char name[SCATTER_KEY_CAPACITY];
    char message[256];
    runtimec_bytes_view_t wire;
    int32_t count = 0;
    int64_t largest = -1;
    runtimec_error_t error;
    runtimec_status_t status;
    const uint32_t counter = instruction->source_logical_program_counter;

    runtimec_error_reset(&error);
    if (instruction->read_arguments.count != 2U || frame->kvspace_handle == NULL) {
        return scatter_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    status = vmbin_frame_target_key(frame, &instruction->write_arguments.data[0],
                                         base, sizeof(base));
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return scatter_fail(output_exit, status, counter);
    }
    if (strlen(base) + 2U > sizeof(directory)) {
        return scatter_fail(output_exit, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY,
                            counter);
    }
    snprintf(directory, sizeof(directory), "%s%s", base,
             SCATTER_MEMBER_SEPARATOR);
    /*
     * 顺序要紧：**先把目录数完，再取值的地址**。
     *
     * 值的地址来自绑定表（shm 的 mmap 视图）；`kvspaceListLen`/`ListAt` 是会
     * 触碰后端的调用，可能让之前的视图失效。先 locate 再枚举，等于拿一个已经
     * 不保证有效的地址去写——这正是本算子最初 status=7 的成因。
     */
    if (kvspaceListLen(frame->kvspace_handle, directory, 0, 0, &count) != 0) {
        return scatter_fail(output_exit, RUNTIMEC_STATUS_ERROR, counter);
    }
    /* 目录里的名字形如 `[i]`：逐个解析取最大坐标。 */
    for (int32_t ordinal = 0; ordinal < count; ++ordinal) {
        uint32_t name_size = 0U;

        if (kvspaceListAt(frame->kvspace_handle, directory, 0, 0, ordinal,
                          (uint8_t *)name, (uint32_t)sizeof(name),
                          &name_size) != 0) {
            continue;
        }
        name[name_size] = '\0';
        if (name[0] == '[') {
            char *end = NULL;
            const long long parsed = strtoll(name + 1, &end, 10);

            if (end != NULL && *end == ']' && end[1] == '\0' &&
                parsed > largest) {
                largest = (int64_t)parsed;
            }
        }
    }
    status = scatter_member_key(base, largest + 1, member, sizeof(member));
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return scatter_fail(output_exit, status, counter);
    }
    /* 枚举完再取值：此时视图是新读的，写它安全。取的是**整条 wire**——
     * `kvspaceSetValue` 收完整 XValue，喂 body 会得到 invalid XValue。 */
    status = scatter_whole_wire(frame, &instruction->read_arguments.data[1],
                                &wire, &error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return scatter_fail(output_exit, status, counter);
    }
    message[0] = '\0';
    if (kvspaceSetValue(frame->kvspace_handle, member, wire.data,
                        (uint32_t)wire.size, message, sizeof(message)) != 0) {
        return scatter_fail(output_exit, RUNTIMEC_STATUS_ERROR, counter);
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * `scatterarray·newscatter(a) -> b`：compact → 散列。把 body 里每个元素搬到
 * `b·[i]`，再把 `b` 建成容器（marker + 目录）。
 *
 * compact → 散列是**显式**转换：没有任何算子会隐式跨形态（spec 的铁律）。
 */
static inline runtimec_status_t scatter_newscatter(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    runtimec_string_view_t source_langtype;
    vmbin_value_location_t source;
    runtimec_container_langtype_t source_type;
    char base[SCATTER_KEY_CAPACITY];
    char member[SCATTER_KEY_CAPACITY];
    char message[256];
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;
    runtimec_error_t error;
    runtimec_status_t status;
    const uint32_t counter = instruction->source_logical_program_counter;

    runtimec_error_reset(&error);
    if (instruction->read_arguments.count != 1U || frame->kvspace_handle == NULL) {
        return scatter_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    status = vmbin_value_access_langtype(
        frame, &instruction->read_arguments.data[0], &source_langtype, &error);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = runtimec_container_langtype_parse(source_langtype, &source_type);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = vmbin_value_access_locate_body(
            frame, &instruction->read_arguments.data[0], &source, &error);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return scatter_fail(output_exit, status, counter);
    }
    status = vmbin_frame_target_key(frame, &instruction->write_arguments.data[0],
                                         base, sizeof(base));
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return scatter_fail(output_exit, status, counter);
    }
    /* 先把目标建成容器，再逐元素搬进成员键。 */
    {
        char directory[SCATTER_KEY_CAPACITY + 2U];

        if (kvspaceXvalueHeadNewSlack(NULL, 0U, 0U, "stringkeymap", &wire,
                                      &wire_size) == 0) {
            message[0] = '\0';
            status = kvspaceSetValue(frame->kvspace_handle, base, wire,
                                     (uint32_t)wire_size, message,
                                     sizeof(message)) == 0
                         ? RUNTIMEC_STATUS_SUCCESS
                         : RUNTIMEC_STATUS_ERROR;
            free(wire);
        } else {
            status = RUNTIMEC_STATUS_ERROR;
        }
        if (status == RUNTIMEC_STATUS_SUCCESS) {
            snprintf(directory, sizeof(directory), "%s%s", base,
                 SCATTER_MEMBER_SEPARATOR);
            message[0] = '\0';
            if (kvspaceMkindex(frame->kvspace_handle, directory, 0U, message,
                               (uint32_t)sizeof(message)) != 0) {
                status = RUNTIMEC_STATUS_ERROR;
            }
        }
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return scatter_fail(output_exit, status, counter);
    }
    /* 逐元素：源 body 的第 i 段 → `base·[i]`，每段用元素的 langtype 组 wire。 */
    {
        const size_t element_count =
            source.size / (source_type.element_size == 0U
                               ? 1U
                               : source_type.element_size);
        char element_langtype[64];

        if (source_type.base.size + 1U > sizeof(element_langtype)) {
            return scatter_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
        }
        memcpy(element_langtype, source_type.base.data, source_type.base.size);
        element_langtype[source_type.base.size] = '\0';
        for (size_t ordinal = 0U; ordinal < element_count; ++ordinal) {
            status = scatter_member_key(base, (int64_t)ordinal, member,
                                        sizeof(member));
            if (status != RUNTIMEC_STATUS_SUCCESS) {
                return scatter_fail(output_exit, status, counter);
            }
            if (kvspaceXvalueHeadNewSlack(
                    source.read_bytes + ordinal * source_type.element_size,
                    source_type.element_size, source_type.element_size,
                    element_langtype, &wire, &wire_size) != 0) {
                return scatter_fail(output_exit, RUNTIMEC_STATUS_ERROR, counter);
            }
            message[0] = '\0';
            if (kvspaceSetValue(frame->kvspace_handle, member, wire,
                                (uint32_t)wire_size, message,
                                sizeof(message)) != 0) {
                free(wire);
                return scatter_fail(output_exit, RUNTIMEC_STATUS_ERROR, counter);
            }
            free(wire);
        }
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * `scatterarray·newcompact(a) -> b`：散列 → compact。按坐标顺序读成员，拼成
 * 一个连续 body，langtype 由写槽声明给出（维度来自目标声明，不抄成员类型）。
 */
static inline runtimec_status_t scatter_newcompact(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    char base[SCATTER_KEY_CAPACITY];
    char directory[SCATTER_KEY_CAPACITY + 2U];
    char member[SCATTER_KEY_CAPACITY];
    char langtype_text[SCATTER_KEY_CAPACITY];
    uint8_t body[SCATTER_BODY_CAPACITY];
    size_t body_size = 0U;
    runtimec_string_view_t declared;
    int32_t count = 0;
    runtimec_error_t error;
    runtimec_status_t status;
    const uint32_t counter = instruction->source_logical_program_counter;

    runtimec_error_reset(&error);
    if (instruction->read_arguments.count != 1U || frame->kvspace_handle == NULL) {
        return scatter_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    status = vmbin_frame_target_key(frame, &instruction->read_arguments.data[0],
                                         base, sizeof(base));
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = vmbin_value_access_langtype(
            frame, &instruction->write_arguments.data[0], &declared, &error);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS ||
        declared.size + 1U > sizeof(langtype_text)) {
        return scatter_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    memcpy(langtype_text, declared.data, declared.size);
    langtype_text[declared.size] = '\0';
    if (strlen(base) + 2U > sizeof(directory)) {
        return scatter_fail(output_exit, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY,
                            counter);
    }
    snprintf(directory, sizeof(directory), "%s%s", base,
                 SCATTER_MEMBER_SEPARATOR);
    if (kvspaceListLen(frame->kvspace_handle, directory, 0, 0, &count) != 0) {
        return scatter_fail(output_exit, RUNTIMEC_STATUS_ERROR, counter);
    }
    for (int32_t ordinal = 0; ordinal < count; ++ordinal) {
        char name[SCATTER_KEY_CAPACITY];
        uint32_t name_size = 0U;
        uint8_t *stored = NULL;
        uint32_t stored_size = 0U;

        if (kvspaceListAt(frame->kvspace_handle, directory, 0, 0, ordinal,
                          (uint8_t *)name, (uint32_t)sizeof(name),
                          &name_size) != 0) {
            continue;
        }
        name[name_size] = '\0';
        if (name[0] != '[') {
            continue;
        }
        if (snprintf(member, sizeof(member), "%s%s%s", base,
                     SCATTER_MEMBER_SEPARATOR, name) >=
            (int)sizeof(member)) {
            return scatter_fail(output_exit, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY,
                                counter);
        }
        if (kvspaceGet(frame->kvspace_handle, member, 0, &stored, &stored_size) !=
                0 ||
            stored == NULL || stored_size < KVSPACE_XVALUE_HEAD_LEN) {
            continue;
        }
        {
            const kvspaceXvalueHead *head =
                (const kvspaceXvalueHead *)(const void *)stored;

            if (body_size + kvspaceXvalueHeadLen(head) > sizeof(body)) {
                return scatter_fail(output_exit, RUNTIMEC_STATUS_OUT_OF_MEMORY,
                                    counter);
            }
            memcpy(body + body_size, stored + KVSPACE_XVALUE_HEAD_LEN,
                   (size_t)kvspaceXvalueHeadLen(head));
            body_size += (size_t)kvspaceXvalueHeadLen(head);
        }
    }
    {
        char key[SCATTER_KEY_CAPACITY];
        char message[256];
        uint8_t *wire = NULL;
        uint64_t wire_size = 0U;

        if (kvspaceXvalueHeadNewSlack(body, (uint64_t)body_size,
                                      (uint64_t)body_size, langtype_text, &wire,
                                      &wire_size) != 0) {
            return scatter_fail(output_exit, RUNTIMEC_STATUS_ERROR, counter);
        }
        status = vmbin_frame_target_key(
            frame, &instruction->write_arguments.data[0], key, sizeof(key));
        if (status == RUNTIMEC_STATUS_SUCCESS) {
            message[0] = '\0';
            if (kvspaceSetValue(frame->kvspace_handle, key, wire,
                                (uint32_t)wire_size, message,
                                sizeof(message)) != 0) {
                status = RUNTIMEC_STATUS_ERROR;
            }
        }
        free(wire);
    }
    return status == RUNTIMEC_STATUS_SUCCESS
               ? RUNTIMEC_STATUS_SUCCESS
               : scatter_fail(output_exit, status, counter);
}

/*
 * `scatterarray·slice(a, lo, hi) -> b`：按坐标段重排成员到新容器。
 * 位置越界或 lo > hi 返回 INVALID（string-slice-bounds 的同一判据）。
 */
static inline runtimec_status_t scatter_slice(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    char source_base[SCATTER_KEY_CAPACITY];
    char target_base[SCATTER_KEY_CAPACITY];
    char directory[SCATTER_KEY_CAPACITY + 2U];
    char member[SCATTER_KEY_CAPACITY];
    char message[256];
    vmbin_value_location_t bounds[2];
    int64_t lo = 0;
    int64_t hi = 0;
    int32_t count = 0;
    runtimec_error_t error;
    runtimec_status_t status;
    const uint32_t counter = instruction->source_logical_program_counter;

    runtimec_error_reset(&error);
    if (instruction->read_arguments.count != 3U || frame->kvspace_handle == NULL) {
        return scatter_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    status = vmbin_frame_target_key(frame, &instruction->read_arguments.data[0],
                                         source_base, sizeof(source_base));
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        for (size_t ordinal = 0U; ordinal < 2U && status == RUNTIMEC_STATUS_SUCCESS;
             ++ordinal) {
            status = vmbin_value_access_locate_body(
                frame, &instruction->read_arguments.data[ordinal + 1U],
                &bounds[ordinal], &error);
        }
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return scatter_fail(output_exit, status, counter);
    }
    if (bounds[0].size != sizeof(lo) || bounds[1].size != sizeof(hi)) {
        return scatter_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    memcpy(&lo, bounds[0].read_bytes, sizeof(lo));
    memcpy(&hi, bounds[1].read_bytes, sizeof(hi));
    if (lo < 0 || hi < lo) {
        return scatter_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    status = vmbin_frame_target_key(frame, &instruction->write_arguments.data[0],
                                         target_base, sizeof(target_base));
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return scatter_fail(output_exit, status, counter);
    }
    /* 目标容器：marker + 目录。 */
    {
        uint8_t *wire = NULL;
        uint64_t wire_size = 0U;

        if (kvspaceXvalueHeadNewSlack(NULL, 0U, 0U, "stringkeymap", &wire,
                                      &wire_size) == 0) {
            message[0] = '\0';
            status = kvspaceSetValue(frame->kvspace_handle, target_base, wire,
                                     (uint32_t)wire_size, message,
                                     sizeof(message)) == 0
                         ? RUNTIMEC_STATUS_SUCCESS
                         : RUNTIMEC_STATUS_ERROR;
            free(wire);
        } else {
            status = RUNTIMEC_STATUS_ERROR;
        }
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return scatter_fail(output_exit, status, counter);
    }
    snprintf(directory, sizeof(directory), "%s%s", target_base,
                 SCATTER_MEMBER_SEPARATOR);
    message[0] = '\0';
    if (kvspaceMkindex(frame->kvspace_handle, directory, 0U, message,
                       (uint32_t)sizeof(message)) != 0) {
        return scatter_fail(output_exit, RUNTIMEC_STATUS_ERROR, counter);
    }
    snprintf(directory, sizeof(directory), "%s%s", source_base,
                 SCATTER_MEMBER_SEPARATOR);
    if (kvspaceListLen(frame->kvspace_handle, directory, 0, 0, &count) != 0) {
        return scatter_fail(output_exit, RUNTIMEC_STATUS_ERROR, counter);
    }
    if (hi > count) {
        return scatter_fail(output_exit, RUNTIMEC_STATUS_INVALID, counter);
    }
    for (int64_t ordinal = lo; ordinal < hi; ++ordinal) {
        uint8_t *stored = NULL;
        uint32_t stored_size = 0U;

        status = scatter_member_key(source_base, ordinal, member,
                                    sizeof(member));
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return scatter_fail(output_exit, status, counter);
        }
        if (kvspaceGet(frame->kvspace_handle, member, 0, &stored, &stored_size) !=
                0 ||
            stored == NULL) {
            continue;
        }
        status = scatter_member_key(target_base, ordinal - lo, member,
                                    sizeof(member));
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return scatter_fail(output_exit, status, counter);
        }
        message[0] = '\0';
        if (kvspaceSetValue(frame->kvspace_handle, member, stored, stored_size,
                            message, sizeof(message)) != 0) {
            return scatter_fail(output_exit, RUNTIMEC_STATUS_ERROR, counter);
        }
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

VMBIN_HANDLER_SCATTER_READ_WRITE(scatter_at_handler, scatter_at_build)
static runtimec_status_t scatter_set_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return scatter_set(frame, instruction, output_exit);
}

static runtimec_status_t scatter_append_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return scatter_append(frame, instruction, output_exit);
}

static runtimec_status_t scatter_newscatter_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return scatter_newscatter(frame, instruction, output_exit);
}

static runtimec_status_t scatter_newcompact_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return scatter_newcompact(frame, instruction, output_exit);
}

static runtimec_status_t scatter_slice_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return scatter_slice(frame, instruction, output_exit);
}

static const binrwir_registry_entry_t runtimec_scatter_entry_table[] = {
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_SCATTER_ARRAY_NEW,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scatter_new_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_SCATTER_ARRAY_LENGTH,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scatter_length_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_SCATTER_ARRAY_AT,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scatter_at_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_SCATTER_ARRAY_SET,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scatter_set_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_SCATTER_ARRAY_APPEND,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scatter_append_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_SCATTER_ARRAY_NEW_SCATTER,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scatter_newscatter_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_SCATTER_ARRAY_NEW_COMPACT,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scatter_newcompact_handler},
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_SCATTER_ARRAY_SLICE,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = scatter_slice_handler},
};

const binrwir_registry_entry_t *runtimec_scatter_handler_entries(
    size_t *output_count) {
    if (output_count == NULL) {
        return NULL;
    }
    *output_count = sizeof(runtimec_scatter_entry_table) /
                    sizeof(runtimec_scatter_entry_table[0]);
    return runtimec_scatter_entry_table;
}
