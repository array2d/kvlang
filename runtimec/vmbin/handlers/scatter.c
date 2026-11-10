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
#include <string.h>

#include "binrwir/execution_exit.h"
#include "binrwir/binvthread_frame.h"
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
#define SCATTER_BODY_CAPACITY 512U

/*
 * 成员分隔符：`·`（U+00B7，UTF-8 两个字节）。**不是 `/`**——散列容器的成员是
 * `base·[i]`，`kvspaceListLen` 用这个前缀枚举。写成 `base/` 会建出一条与成员
 * 不相干的目录：`len` 恒为 0，而成员明明在。
 */
#define SCATTER_MEMBER_SEPARATOR "\xC2\xB7"

/* 前置声明：容器创建 helper 要先用到目录前缀与成员键，两者在它之后定义。 */
static bool scatter_member_prefix(const char *base, char *buffer,
                                  size_t capacity,
                                  binrwir_execution_exit_t *output_exit);
static runtimec_status_t scatter_member_key(const char *base, int64_t coordinate,
                                            char *buffer, size_t capacity);

/*
 * 建一个**空散列容器**：空 body 的 map 头 + 目录。跨容器搬运的目标都先建成它。
 *
 * 两步缺一不可：只写 marker 则 `ListLen` 数不到成员；只建目录则 base 没有类型。
 * **失败即置 `exit` 并返回 false**。
 */
static bool scatter_container_empty(const binrwir_binvthread_frame_t *frame,
                                    const char *base,
                                    binrwir_execution_exit_t *output_exit) {
    char directory[SCATTER_KEY_CAPACITY + 2U];
    char message[256];
    uint8_t *wire = NULL;
    uint64_t wire_size = 0U;

    if (kvspaceXvalueHeadNewSlack(NULL, 0U, "stringkeymap", &wire,
                                  &wire_size) != 0 ||
        wire == NULL) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                           VMBIN_HANDLER_CALL_DIAG_CREATE_FAILED,
                           output_exit->logical_program_counter);
        return false;
    }
    message[0] = '\0';
    if (kvspaceSetValue(frame->kvspace_handle, base, wire, (uint32_t)wire_size,
                        message, sizeof(message)) != 0) {
        free(wire);
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                           VMBIN_HANDLER_CALL_DIAG_CREATE_FAILED,
                           output_exit->logical_program_counter);
        return false;
    }
    free(wire);
    if (!scatter_member_prefix(base, directory, sizeof(directory),
                               output_exit)) {
        return false;
    }
    message[0] = '\0';
    if (kvspaceMkindex(frame->kvspace_handle, directory, 0U, message,
                       (uint32_t)sizeof(message)) != 0) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                           VMBIN_HANDLER_CALL_DIAG_CREATE_FAILED,
                           output_exit->logical_program_counter);
        return false;
    }
    return true;
}

/*
 * 建成一个**空散列容器**——两件事缺一不可：
 *   1. base key 写一条**空 body 的 map 头**（langtype 取自写槽声明）；
 *   2. `kvspaceMkindex` 给 base 建目录，之后成员才能被枚举。
 *
 * 只做 1 的话 `kvspaceListLen` 数不到任何成员（`len` 恒 0）；只做 2 的话 base 上
 * 就没有类型。**两步都必须成功**，否则整体失败。**成功不写哨兵，失败只写 `exit`**。
 */
static void scatter_container_create(const binrwir_binvthread_frame_t *frame,
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
    if (kvspaceXvalueHeadNewSlack(NULL, 0U, langtype_text, &wire,
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
    if (!scatter_member_prefix(key, directory, sizeof(directory),
                               output_exit)) {
        return;
    }
    message[0] = '\0';
    if (kvspaceMkindex(frame->kvspace_handle, directory, 0U, message,
                       (uint32_t)sizeof(message)) != 0) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                           VMBIN_HANDLER_CALL_DIAG_CREATE_FAILED,
                           output_exit->logical_program_counter);
    }
}

/* 逻辑函数：一句话——建一个空散列容器。 */
static inline void scatter_new_build(const binrwir_binvthread_frame_t *frame,
                                     const binrwir_instruction_view_t *instruction,
                                     binrwir_execution_exit_t *output_exit) {
    scatter_container_create(frame, instruction, output_exit);
}

VMBIN_HANDLER_SCATTER_CREATE(scatter_new_handler, scatter_new_build)


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
    const binrwir_binvthread_frame_t *frame,
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
static bool scatter_base_key(const binrwir_binvthread_frame_t *frame,
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

/*
 * 取第 ordinal 个读参的**坐标**（非负整数）。`at`/`set`/`append`/`slice` 都要，
 * 七处重复的第二步。
 */
static runtimec_status_t scatter_member_coordinate(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, size_t ordinal,
    int64_t *output_coordinate, binrwir_execution_exit_t *output_exit) {
    vmbin_value_location_t position;
    runtimec_error_t error;

    runtimec_error_reset(&error);
    if (vmbin_value_access_locate_body(
            frame, &instruction->read_arguments.data[ordinal], &position,
            &error) != RUNTIMEC_STATUS_SUCCESS ||
        position.size != sizeof(int64_t)) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return RUNTIMEC_STATUS_INVALID;
    }
    memcpy(output_coordinate, position.read_bytes, sizeof(int64_t));
    if (*output_coordinate < 0) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return RUNTIMEC_STATUS_INVALID;
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

/*
 * 把一个成员从源坐标搬到目标坐标：**整条 wire 原样搬**，不重新编码。
 * 源成员缺失时跳过（洞不是元素）。**失败即置 `exit` 并返回 false**。
 */
static bool scatter_member_copy(const binrwir_binvthread_frame_t *frame,
                                const char *source_base, int64_t source_coordinate,
                                const char *target_base,
                                int64_t target_coordinate, char *source_member,
                                char *target_member, size_t member_capacity,
                                binrwir_execution_exit_t *output_exit) {
    uint8_t *stored = NULL;
    uint32_t stored_size = 0U;
    char message[256];

    if (scatter_member_key(source_base, source_coordinate, source_member,
                           member_capacity) != RUNTIMEC_STATUS_SUCCESS ||
        scatter_member_key(target_base, target_coordinate, target_member,
                           member_capacity) != RUNTIMEC_STATUS_SUCCESS) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return false;
    }
    if (kvspaceGet(frame->kvspace_handle, source_member, 0, &stored,
                   &stored_size) != 0 ||
        stored == NULL) {
        return true; /* 洞：跳过，不是错误。 */
    }
    message[0] = '\0';
    if (kvspaceSetValue(frame->kvspace_handle, target_member, stored,
                        stored_size, message, sizeof(message)) != 0) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                           VMBIN_HANDLER_CALL_DIAG_CREATE_FAILED,
                           output_exit->logical_program_counter);
        return false;
    }
    return true;
}

/* 注册用的薄封装：主体 static inline，注册表要地址，两件事各自独立。 */




/*
 * 读坐标 `i` 下的成员。**成员缺失（不是 0 长度，而是根本没有这个键）即失败**——
 * 洞不是元素。**成功才写结果，失败只写 `exit`**。
 */
static void scatter_member_read(const binrwir_binvthread_frame_t *frame,
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
static inline void scatter_at_build(const binrwir_binvthread_frame_t *frame,
                                    const binrwir_instruction_view_t *instruction,
                                    uint8_t *output, size_t output_capacity,
                                    binrwir_execution_exit_t *output_exit) {
    scatter_member_read(frame, instruction, output, output_capacity, output_exit);
}


/*
 * 写一个成员：值写成**完整 wire**（head + body + attr）——`kvspaceSetValue` 收的
 * 就是它，喂 body 会得到 invalid XValue。成员值原样搬，不重新编码。
 *
 * **成功不写哨兵，失败只写 `exit`**。
 */
static void scatter_member_write(const binrwir_binvthread_frame_t *frame,
                                 const char *member,
                                 const runtimec_bytes_view_t *value,
                                 binrwir_execution_exit_t *output_exit) {
    char message[256];

    message[0] = '\0';
    if (kvspaceSetValue(frame->kvspace_handle, member, value->data,
                        (uint32_t)value->size, message, sizeof(message)) != 0) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                           VMBIN_HANDLER_CALL_DIAG_CREATE_FAILED,
                           output_exit->logical_program_counter);
    }
}

/*
 * 把一个读参的值写进容器的第 i 个成员。四步（取 base、取坐标、取 wire、写成员）
 * 合成一层——目的只有一个：**把值放到那个坐标上**。
 */
static void scatter_store_at(const binrwir_binvthread_frame_t *frame,
                             const binrwir_instruction_view_t *instruction,
                             binrwir_execution_exit_t *output_exit) {
    char base[SCATTER_KEY_CAPACITY];
    char member[SCATTER_KEY_CAPACITY];
    runtimec_bytes_view_t value;
    int64_t coordinate = 0;
    runtimec_error_t error;

    runtimec_error_reset(&error);
    if (!scatter_base_key(frame, &instruction->write_arguments.data[0], base,
                          sizeof(base), output_exit) ||
        scatter_member_coordinate(frame, instruction, 1U, &coordinate,
                                  output_exit) != RUNTIMEC_STATUS_SUCCESS ||
        scatter_whole_wire(frame, &instruction->read_arguments.data[2], &value,
                           &error) != RUNTIMEC_STATUS_SUCCESS ||
        scatter_member_key(base, coordinate, member, sizeof(member)) !=
            RUNTIMEC_STATUS_SUCCESS) {
        if (output_exit->kind != BINRWIR_EXECUTION_EXIT_ERROR) {
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                               VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                               output_exit->logical_program_counter);
        }
        return;
    }
    scatter_member_write(frame, member, &value, output_exit);
}

/* 逻辑函数：一句话——把值写进坐标 i 那个成员键。 */
static inline void scatter_set_build(const binrwir_binvthread_frame_t *frame,
                                     const binrwir_instruction_view_t *instruction,
                                     binrwir_execution_exit_t *output_exit) {
    scatter_store_at(frame, instruction, output_exit);
}


/*
 * 目录里**最大的坐标**；空容器返回 -1（所以 next = 0）。
 *
 * 目录里的名字形如 `[i]`：逐个解析取最大值。**洞不是问题**——用户直写 `[100]`
 * 之后 append 就该接 101，这正是行为矩阵 `scatterarray-next-index` 的判据。
 *
 * **成功才写 `*output_largest`，失败只写 `exit`**。
 *
 * `-1` **只表示「成功且容器为空」**（所以 next = 0），不是错误哨兵——坐标域是
 * `0..n-1`，`-1` 在域外。失败一律走 `exit` 且**不写 out**，所以调用方永远不会
 * 把「该从 0 开始」与「该报错」混为一谈。
 */
static void scatter_largest_coordinate(const binrwir_binvthread_frame_t *frame,
                                       const char *base, int64_t *output_largest,
                                       binrwir_execution_exit_t *output_exit) {
    char directory[SCATTER_KEY_CAPACITY + 2U];
    char name[SCATTER_KEY_CAPACITY];
    int32_t count = 0;

    *output_largest = -1;
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
    for (int32_t ordinal = 0; ordinal < count; ++ordinal) {
        uint32_t name_size = 0U;

        if (kvspaceListAt(frame->kvspace_handle, directory, 0, 0, ordinal,
                          (uint8_t *)name, (uint32_t)sizeof(name),
                          &name_size) != 0) {
            continue;
        }
        name[name_size] = '\0';
        if (name[0] == '[') {
            char *end_of_number = NULL;
            const long long parsed = strtoll(name + 1, &end_of_number, 10);

            if (end_of_number != NULL && *end_of_number == ']' &&
                end_of_number[1] == '\0' && parsed > *output_largest) {
                *output_largest = (int64_t)parsed;
            }
        }
    }
}

/*
 * 追加：坐标 = 最大坐标 + 1。**先把目录数完，再取值的 wire**——
 * 值的地址来自绑定表（shm 的 mmap 视图），而枚举会触碰后端，可能让之前的视图失效。
 * 先 locate 再枚举，等于拿一个不保证有效的地址去写。
 */
static void scatter_append_value(const binrwir_binvthread_frame_t *frame,
                                 const binrwir_instruction_view_t *instruction,
                                 binrwir_execution_exit_t *output_exit) {
    char base[SCATTER_KEY_CAPACITY];
    char member[SCATTER_KEY_CAPACITY];
    runtimec_bytes_view_t wire;
    int64_t largest = -1;
    runtimec_error_t error;

    runtimec_error_reset(&error);
    if (!scatter_base_key(frame, &instruction->write_arguments.data[0], base,
                          sizeof(base), output_exit)) {
        return;
    }
    scatter_largest_coordinate(frame, base, &largest, output_exit);
    if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) {
        return;
    }
    if (scatter_member_key(base, largest + 1, member, sizeof(member)) !=
            RUNTIMEC_STATUS_SUCCESS ||
        scatter_whole_wire(frame, &instruction->read_arguments.data[1], &wire,
                           &error) != RUNTIMEC_STATUS_SUCCESS) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    scatter_member_write(frame, member, &wire, output_exit);
}

/* 逻辑函数：一句话——追加到最大坐标 + 1。 */
static inline void scatter_append_build(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    scatter_append_value(frame, instruction, output_exit);
}


/*
 * **展开**：把 compact 数组的每个元素变成散列容器的一个成员（`b·[i]`）。
 *
 * 名字带方向是有意的——压缩走 `scatter_pack_to_compact`，两者成员键构造规则相反，
 * 硬合并会变成按形态分支的巨函数。
 *
 * 跨形态是**显式**转换：没有任何算子会隐式跨形态（spec 的铁律）。
 */
static void scatter_expand_from_compact(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    runtimec_string_view_t source_langtype;
    vmbin_value_location_t source;
    runtimec_container_langtype_t source_type;
    char base[SCATTER_KEY_CAPACITY];
    char member[SCATTER_KEY_CAPACITY];
    char element_langtype[64];
    runtimec_error_t error;

    runtimec_error_reset(&error);
    if (vmbin_value_access_langtype(
            frame, &instruction->read_arguments.data[0], &source_langtype,
            &error) != RUNTIMEC_STATUS_SUCCESS ||
        runtimec_container_langtype_parse(source_langtype, &source_type) !=
            RUNTIMEC_STATUS_SUCCESS ||
        source_type.element_size == 0U ||
        vmbin_value_access_locate_body(
            frame, &instruction->read_arguments.data[0], &source,
            &error) != RUNTIMEC_STATUS_SUCCESS) {
        if (output_exit->kind != BINRWIR_EXECUTION_EXIT_ERROR) {
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                               VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                               output_exit->logical_program_counter);
        }
        return;
    }
    /*
     * **目标容器本身必须先存在**：写 `base·[i]` 成员之前，`base` 要有
     * empty-body 的容器 marker、要有成员索引、还要**进绑定表**。
     *
     * 这一步走 `runtimec_container_open_marker`——容器值创建的唯一入口，与
     * `map·new` 同一份实现。**不在这里自己拼 key**：key 的拼法只有一处。
     *
     * 少了它的后果实测过（`04-ndarray/ndarray_numel_scattered`）：成员
     * `b·[0..2]` 都写进去了，但 `b` 本身不存在、绑定表里也没有它，随后
     * `compactarray·numel(b)` 读它就是 `UNBOUND_SLOT`。
     */
    if (!runtimec_container_open_marker(frame, instruction, base, sizeof(base),
                                        output_exit)) {
        return;
    }
    if (source_type.base.size + 1U > sizeof(element_langtype)) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    memcpy(element_langtype, source_type.base.data, source_type.base.size);
    element_langtype[source_type.base.size] = '\0';
    if (!scatter_container_empty(frame, base, output_exit)) {
        return;
    }
    /* 逐元素：源 body 的第 i 段 → `base·[i]`，每段用元素类型组 wire。 */
    {
        const size_t element_count = source.size / source_type.element_size;

        for (size_t ordinal = 0U; ordinal < element_count; ++ordinal) {
            uint8_t *wire = NULL;
            uint64_t wire_size = 0U;
            char message[256];

            if (scatter_member_key(base, (int64_t)ordinal, member,
                                   sizeof(member)) != RUNTIMEC_STATUS_SUCCESS ||
                kvspaceXvalueHeadNewSlack(
                    source.read_bytes + ordinal * source_type.element_size,
                    source_type.element_size,
                    element_langtype, &wire, &wire_size) != 0) {
                vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                                   VMBIN_HANDLER_CALL_DIAG_CREATE_FAILED,
                                   output_exit->logical_program_counter);
                return;
            }
            message[0] = '\0';
            if (kvspaceSetValue(frame->kvspace_handle, member, wire,
                                (uint32_t)wire_size, message,
                                sizeof(message)) != 0) {
                free(wire);
                vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                                   VMBIN_HANDLER_CALL_DIAG_CREATE_FAILED,
                                   output_exit->logical_program_counter);
                return;
            }
            free(wire);
        }
    }
}

/* 逻辑函数：一句话——把 compact 数组展开成散列容器。 */
static inline void scatter_newscatter_build(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    scatter_expand_from_compact(frame, instruction, output_exit);
}


/*
 * **压缩**：把散列容器的成员按坐标顺序压成连续 body（`b` 是 compact）。
 *
 * 两条拒绝（细节码分开，因为修法不同）：
 *   - **有洞** → `0x4306`。坐标 0..n-1 必须齐全。填零是**发明数据**，跳过是
 *     **自洽的谎言**（头说 `[N]T`、body 只有 k 个元素），两者都比报错坏；
 *   - **成员数 ≠ 目标声明的 N** → `0x4307`。`N` 是契约，源必须恰好填满它。
 *
 * 名字带方向：展开走 `scatter_expand_from_compact`，两者成员键构造规则相反。
 */
static void scatter_pack_to_compact(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    char base[SCATTER_KEY_CAPACITY];
    char directory[SCATTER_KEY_CAPACITY + 2U];
    char member[SCATTER_KEY_CAPACITY];
    runtimec_string_view_t target_langtype;
    int32_t member_count = 0;
    int64_t declared_count;
    uint8_t body[SCATTER_BODY_CAPACITY];
    size_t body_size = 0U;
    runtimec_error_t error;

    runtimec_error_reset(&error);
    if (!scatter_base_key(frame, &instruction->read_arguments.data[0], base,
                          sizeof(base), output_exit) ||
        vmbin_value_access_langtype(
            frame, &instruction->write_arguments.data[0], &target_langtype,
            &error) != RUNTIMEC_STATUS_SUCCESS) {
        if (output_exit->kind != BINRWIR_EXECUTION_EXIT_ERROR) {
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                               VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,
                               output_exit->logical_program_counter);
        }
        return;
    }
    /* 目标声明的 N 是契约：源必须恰好填满它。 */
    declared_count = runtimec_container_shape_numel(target_langtype);
    if (declared_count < 0 ||
        !scatter_member_prefix(base, directory, sizeof(directory),
                               output_exit)) {
        if (output_exit->kind != BINRWIR_EXECUTION_EXIT_ERROR) {
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                               RUNTIMEC_CONTAINER_DIAG_UNPARSEABLE_LANGTYPE,
                               output_exit->logical_program_counter);
        }
        return;
    }
    if (kvspaceListLen(frame->kvspace_handle, directory, 0, 0,
                       &member_count) != 0) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                           VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,
                           output_exit->logical_program_counter);
        return;
    }
    if ((int64_t)member_count != declared_count) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           RUNTIMEC_CONTAINER_DIAG_ELEMENT_COUNT_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    /* 逐个坐标读：缺一个就是洞——不是「跳过」，是拒绝。 */
    for (int64_t ordinal = 0; ordinal < declared_count; ++ordinal) {
        uint8_t *stored = NULL;
        uint32_t stored_size = 0U;
        uint64_t element_length;

        if (scatter_member_key(base, ordinal, member, sizeof(member)) !=
            RUNTIMEC_STATUS_SUCCESS) {
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                               VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                               output_exit->logical_program_counter);
            return;
        }
        if (kvspaceGet(frame->kvspace_handle, member, 0, &stored,
                       &stored_size) != 0 ||
            stored == NULL || stored_size < KVSPACE_XVALUE_HEAD_LEN) {
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                               RUNTIMEC_CONTAINER_DIAG_SCATTER_HAS_HOLE,
                               output_exit->logical_program_counter);
            return;
        }
        element_length = kvspaceXvalueHeadLen(
            (const kvspaceXvalueHead *)(const void *)stored);
        if (body_size + element_length > sizeof(body)) {
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_OUT_OF_MEMORY,
                               RUNTIMEC_CONTAINER_DIAG_NEW_BODY_EXCEEDS_BUFFER,
                               output_exit->logical_program_counter);
            return;
        }
        memcpy(body + body_size, stored + KVSPACE_XVALUE_HEAD_LEN,
               (size_t)element_length);
        body_size += (size_t)element_length;
    }
    /* 结果 langtype 从写参声明取——`vmbin_handler_write_new_value` 自己取。 */
    (void)vmbin_handler_write_new_value(frame, instruction, body, body_size,
                                        output_exit);
}

/* 逻辑函数：一句话——把散列容器压成连续 body。 */
static inline void scatter_newcompact_build(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    scatter_pack_to_compact(frame, instruction, output_exit);
}


/*
 * 把源容器的一段成员**重排到**目标容器：新坐标 = 源坐标 - lo。
 *
 * 判据（比规则更根本）：**坐标是成员序列上的下标，不是键空间里的绝对位置。**
 * `len` 返回成员数、`append` 允许洞、重排本身要把序列压紧——三者都指向同一个
 * 结论：`hi` 与**成员数**比，不与「最大坐标 + 1」比。所以 `[0]=10`、`[5]=30`
 * 的容器上切 `[0,6)` 是**合法**的，得到两个成员。
 *
 * 目标必须先建成空容器（marker + 目录）再逐成员写——跨形态/跨容器的搬运都是
 * **新建**，不是同槽 patch。
 */
static void scatter_slice_between(const binrwir_binvthread_frame_t *frame,
                                  const binrwir_instruction_view_t *instruction,
                                  binrwir_execution_exit_t *output_exit) {
    char source_base[SCATTER_KEY_CAPACITY];
    char target_base[SCATTER_KEY_CAPACITY];
    char directory[SCATTER_KEY_CAPACITY + 2U];
    char source_member[SCATTER_KEY_CAPACITY];
    char target_member[SCATTER_KEY_CAPACITY];
    int64_t lo = 0;
    int64_t hi = 0;
    int32_t count = 0;

    if (!scatter_base_key(frame, &instruction->read_arguments.data[0],
                          source_base, sizeof(source_base), output_exit) ||
        scatter_member_coordinate(frame, instruction, 1U, &lo, output_exit) !=
            RUNTIMEC_STATUS_SUCCESS ||
        scatter_member_coordinate(frame, instruction, 2U, &hi, output_exit) !=
            RUNTIMEC_STATUS_SUCCESS ||
        !scatter_base_key(frame, &instruction->write_arguments.data[0],
                          target_base, sizeof(target_base), output_exit)) {
        return;
    }
    if (hi < lo || !scatter_member_prefix(source_base, directory,
                                          sizeof(directory), output_exit)) {
        if (output_exit->kind != BINRWIR_EXECUTION_EXIT_ERROR) {
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                               VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                               output_exit->logical_program_counter);
        }
        return;
    }
    /* 成员数才是边界：`hi` 不能超过实存成员数（与「最大坐标+1」无关）。 */
    if (kvspaceListLen(frame->kvspace_handle, directory, 0, 0, &count) != 0) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_ERROR,
                           VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,
                           output_exit->logical_program_counter);
        return;
    }
    if (hi > count) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    if (!scatter_container_empty(frame, target_base, output_exit)) {
        return;
    }
    for (int64_t ordinal = lo; ordinal < hi; ++ordinal) {
        if (!scatter_member_copy(frame, source_base, ordinal, target_base,
                                 ordinal - lo, source_member, target_member,
                                 sizeof(source_member), output_exit)) {
            return;
        }
    }
}

/* 逻辑函数：一句话——把源的一段成员重排到目标容器。 */
static inline void scatter_slice_build(
    const binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    scatter_slice_between(frame, instruction, output_exit);
}


VMBIN_HANDLER_SCATTER_READ_WRITE(scatter_at_handler, scatter_at_build)
VMBIN_HANDLER_SCATTER_WRITE_READS(scatter_set_handler, scatter_set_build)
VMBIN_HANDLER_SCATTER_WRITE_READS(scatter_append_handler, scatter_append_build)
VMBIN_HANDLER_SCATTER_EXPAND(scatter_newscatter_handler,
                            scatter_newscatter_build)
VMBIN_HANDLER_SCATTER_EXPAND(scatter_newcompact_handler,
                            scatter_newcompact_build)
VMBIN_HANDLER_SCATTER_CROSS(scatter_slice_handler, scatter_slice_build)
/*
 * 容器的**成员目录前缀**（`base·`）。七处重复的一步。
 *
 * **判据是 `snprintf` 的返回值，不是手算的长度预检查**：返回值是「想写的长度」，
 * 那是真实的截断判据。预检查既挡不住 `-Wformat-truncation=2`（编译器不认运行期
 * `strlen` 的推论），又容易差一——分隔符 2 字节、NUL 又 1 字节，`+2` 就少一个。
 */
static bool scatter_member_prefix(const char *base, char *buffer,
                                  size_t capacity,
                                  binrwir_execution_exit_t *output_exit) {
    const int written =
        snprintf(buffer, capacity, "%s%s", base, SCATTER_MEMBER_SEPARATOR);

    if (written < 0 || (size_t)written >= capacity) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return false;
    }
    return true;
}

/*
 * 散列容器的**实存成员数**（memindex 权威，不是坐标空间容量）。
 * **成功才写 `*out`，失败只写 `exit`**。
 */
static void scatter_member_count(const binrwir_binvthread_frame_t *frame,
                                 const binrwir_instruction_view_t *instruction,
                                 int64_t *out,
                                 binrwir_execution_exit_t *output_exit) {
    char base[SCATTER_KEY_CAPACITY];
    char directory[SCATTER_KEY_CAPACITY + 2U];
    int32_t count = 0;

    if (!scatter_base_key(frame, &instruction->read_arguments.data[0], base,
                          sizeof(base), output_exit)) {
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
static inline void scatter_length_build(const binrwir_binvthread_frame_t *frame,
                                        const binrwir_instruction_view_t *instruction,
                                        int64_t *out,
                                        binrwir_execution_exit_t *output_exit) {
    scatter_member_count(frame, instruction, out, output_exit);
}

VMBIN_HANDLER_SCATTER_READ_SCALAR(scatter_length_handler, scatter_length_build,
                                  int64_t)

/*
 * 成员 key：`base·[i]`。**用 `memcpy` 拼接、不用 `snprintf`**：`base` 长度运行期
 * 才知，任何 `%s` 参与的 `snprintf` 都会被 `-Wformat-truncation=2` 判成可能截断，
 * 而截断的后果是**成员键少一截**（静默错值）。手工拼接让每段长度检查都是真的。
 */
static runtimec_status_t scatter_member_key(const char *base, int64_t coordinate,
                                            char *buffer, size_t capacity) {
    char coordinate_text[32];
    const int coordinate_written =
        snprintf(coordinate_text, sizeof(coordinate_text), "[%lld]",
                 (long long)coordinate);
    const size_t base_length = strlen(base);
    const size_t separator_length = sizeof(SCATTER_MEMBER_SEPARATOR) - 1U;
    size_t total_length;

    if (coordinate_written < 0 ||
        (size_t)coordinate_written >= sizeof(coordinate_text)) {
        return RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
    }
    total_length = base_length + separator_length + (size_t)coordinate_written;
    if (total_length + 1U > capacity) {
        return RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
    }
    memcpy(buffer, base, base_length);
    memcpy(buffer + base_length, SCATTER_MEMBER_SEPARATOR, separator_length);
    memcpy(buffer + base_length + separator_length, coordinate_text,
           (size_t)coordinate_written);
    buffer[total_length] = '\0';
    return RUNTIMEC_STATUS_SUCCESS;
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
