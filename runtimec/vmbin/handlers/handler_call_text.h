#ifndef VMBIN_HANDLERS_HANDLER_CALL_TEXT_H
#define VMBIN_HANDLERS_HANDLER_CALL_TEXT_H

/*
 * 组件：vmbin/handlers/handler_call_text
 * 作用：吃文本的 handler 变体宏——在栈上放 `string_text_t`，解码后再调逻辑函数。
 * 联系：`handler_call.h` 是通用层（只认定位与长度），本文件是它的**文本特化**；
 *       `handlers → handlers` 的引用不是跨层（跨层的是 `contract`，那条线不破）。
 * 约束：
 *   - **按编码的分岔只有一处**：`string_text_decode`（`string_text.c`）。宏自己取
 *     操作数的声明 langtype 传给它，**不把解码器做成宏参数**——那样等于把编码组合
 *     锁死在编译期，9 种组合要 9 次实例化。判据：中心化的分岔不算分叉。
 *   - 解码失败（非法 UTF-8、超长、代理区码点）一律失败封闭，不替换字符——
 *     替换会把畸形输入变成「看起来正常」的文本，调用方再也分不出它原本是坏的。
 *   - 逻辑函数的参数是 `const string_text_t *`，真正一行。
 *
 * ── 族与算子对照（用错族 = 参数形状不匹配，编译期就会红）─────────────────
 *
 *   | 族                          | 参数形状                 | 当前使用者          |
 *   |-----------------------------|--------------------------|---------------------|
 *   | `_UNARY_TEXT`               | 1 文本 → 变长结果        | （待用：format*）   |
 *   | `_UNARY_TEXT_SCALAR`        | 1 文本 → 定长标量        | `len`               |
 *   | `_BINARY_TEXT_SCALAR`       | 2 文本 → 定长标量        | `cmp`、`find`       |
 *   | `_BINARY_TEXT_CREATE`       | 2 文本 → 新建 wire       | `concat`            |
 *   | `_TEXT_INDEX_SCALAR`        | 文本 + 下标 → 定长标量   | `ord`               |
 *   | `_TEXT2_INDEX1`             | 2 文本 + 1 下标 → 新值   | （待用：slice）     |
 *   | `_TEXT1_INDEX2`（若需要）   | 1 文本 + 2 下标 → 新值   | （待用：slice）     |
 *
 * **这张表会过期**，所以 `check_handler_size.py` 会交叉核对「实际用到的族名」与
 * 本表：有人用而表里没有 → 红；表里有而无人用 → 黄（可能留给将来）。
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "contract/error.h"
#include "contract/status.h"
#include "contract/view.h"
#include "vmbin/handlers/handler_call.h"
#include "kvspace/kvspace.h"
#include "kvspace/xvalue_head.h"
#include "vmbin/frame_target_key.h"
#include "vmbin/handlers/string_text.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 调试文本失败：解码不出来即置 exit。
 *
 * 与 `handler_call.h` 的码段分开：0x4B00 段归文本层。编号只增不改。
 */
/* 创建型算子的栈缓冲尺寸：与 container/scatter 的既有约定同量级。 */
#define VMBIN_HANDLER_CREATE_BUFFER_CAPACITY 512U
#define VMBIN_HANDLER_CREATE_KEY_CAPACITY 512U

typedef enum vmbin_handler_call_text_diagnostic {
    VMBIN_HANDLER_CALL_TEXT_DIAG_NONE = 0,
    /* 值的字节不是该编码的合法序列。 */
    VMBIN_HANDLER_CALL_TEXT_DIAG_MALFORMED = 0x4B01,
    /*
     * langtype 不是三种已知编码之一。
     *
     * 与 `MALFORMED` **分开**：那个是「编码认识、字节坏了」，这个是「编码根本不
     * 认识」。两者的下一步动作不同——前者查数据，后者查类型。今晚已经有过一次
     * 「两个成因共用一个码」被拆开。
     */
    VMBIN_HANDLER_CALL_TEXT_DIAG_UNSUPPORTED_ENCODING = 0x4B02
} vmbin_handler_call_text_diagnostic_t;

/*
 * 解码失败 → `exit`。**唯一的映射点**：三族宏（`_UNARY_TEXT` / `_BINARY_TEXT_*`
 * / `_TEXT2_INDEX1`）都调它，不各写一份。
 *
 * 复制三份的代价不是今天的重复，而是**将来加 `0x4B03` 时会有一处漏改**——漏掉的那
 * 一条会在某个组合上给出错误的成因码，而那正是最难查的一类。
 *
 * 两步：按 status 选成因码，再交给 `vmbin_handler_fail` 写 exit。
 */
static inline void vmbin_handler_fail_decode(
    binrwir_execution_exit_t *output_exit, runtimec_status_t status,
    uint32_t source_program_counter) {
    vmbin_handler_fail(
        output_exit, status,
        status == RUNTIMEC_STATUS_UNSUPPORTED
            ? VMBIN_HANDLER_CALL_TEXT_DIAG_UNSUPPORTED_ENCODING
            : VMBIN_HANDLER_CALL_TEXT_DIAG_MALFORMED,
        source_program_counter);
}

/*
 * 二元文本 · **标量结果**：两个读参各自解码，写参是类型化标量。
 *
 * 用于 `streq`（写 bool）、`cmp` 这类「结果长度固定」的算子。
 * 逻辑函数签名：
 *     void f(const string_text_t *left, const string_text_t *right, T *out)
 */
#define VMBIN_HANDLER_BINARY_TEXT_SCALAR(handler_name, function, write_type)       \
    static runtimec_status_t handler_name(                                         \
                             binrwir_execution_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t left_position_;                                     \
        vmbin_value_location_t right_position_;                                    \
        vmbin_value_location_t write_position_;                                    \
        runtimec_string_view_t left_langtype_;                                     \
        runtimec_string_view_t right_langtype_;                                    \
        string_text_t left_text_;                                                  \
        string_text_t right_text_;                                                 \
        runtimec_error_t vmbin_handler_error_;                                     \
        runtimec_status_t vmbin_handler_status_;                                   \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 2U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &left_position_,         \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 1U, &right_position_,        \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,       \
                                   output_exit);                                   \
        VMBIN_HANDLER_REQUIRE_WRITABLE(write_position_, output_exit, instruction); \
        VMBIN_HANDLER_REQUIRE(write_position_, write_type, output_exit,            \
                              instruction);                                        \
        runtimec_error_reset(&vmbin_handler_error_);                               \
        vmbin_handler_status_ = vmbin_value_access_langtype(                       \
            frame, &instruction->read_arguments.data[0], &left_langtype_,          \
            &vmbin_handler_error_);                                                \
        if (vmbin_handler_status_ == RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_status_ = vmbin_value_access_langtype(                   \
                frame, &instruction->read_arguments.data[1], &right_langtype_,     \
                &vmbin_handler_error_);                                            \
        }                                                                          \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail(output_exit, vmbin_handler_status_,                 \
                               VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,              \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        vmbin_handler_status_ = string_text_decode(                                \
            runtimec_bytes_view_make(left_position_.read_bytes,                    \
                                     left_position_.size),                         \
            left_langtype_, &left_text_);                                          \
        if (vmbin_handler_status_ == RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_status_ = string_text_decode(                            \
                runtimec_bytes_view_make(right_position_.read_bytes,               \
                                         right_position_.size),                    \
                right_langtype_, &right_text_);                                    \
        }                                                                          \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail_decode(output_exit, vmbin_handler_status_,          \
                                      instruction->source_logical_program_counter);\
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        function(&left_text_, &right_text_,                                        \
                 (write_type *)(void *)write_position_.write_bytes);               \
        return RUNTIMEC_STATUS_SUCCESS;                                            \
    }

/*
 * 二元文本 · **新建结果**：用于 `concat` 这类结果长度运行期决定、且**写槽通常还不
 * 存在**的算子。
 *
 * 与 SCALAR 版的根本区别：写槽不物化（容器/字符串局部不建零值槽），所以
 * `LOCATE_WRITE` 会直接失败——**不能**先定位再原地写。必须走
 * 「造 wire（带 langtype）→ target key → kvspaceSetValue」。
 *
 * 逻辑函数签名：
 *     void f(const string_text_t *left, const string_text_t *right,
 *            uint8_t *buffer, size_t capacity, size_t *length)
 * 它在栈缓冲里编码结果并给出长度；thunk 拿长度建 wire、拼 key、写存储。
 *
 * 结果 langtype **从写参的声明类型取**（Program 的参数类型表里就有），不拼串。
 */
#define VMBIN_HANDLER_BINARY_TEXT_CREATE(handler_name, function)                   \
    static runtimec_status_t handler_name(                                         \
                             binrwir_execution_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t left_position_;                                     \
        vmbin_value_location_t right_position_;                                    \
        runtimec_string_view_t left_langtype_;                                     \
        runtimec_string_view_t right_langtype_;                                    \
        uint8_t result_bytes_[VMBIN_HANDLER_CREATE_BUFFER_CAPACITY];               \
        size_t result_length_ = 0U;                                                \
        string_text_t left_text_;                                                  \
        string_text_t right_text_;                                                 \
        runtimec_error_t vmbin_handler_error_;                                     \
        runtimec_status_t vmbin_handler_status_;                                   \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 2U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &left_position_,         \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 1U, &right_position_,        \
                                  output_exit);                                    \
        runtimec_error_reset(&vmbin_handler_error_);                               \
        vmbin_handler_status_ = vmbin_value_access_langtype(                       \
            frame, &instruction->read_arguments.data[0], &left_langtype_,          \
            &vmbin_handler_error_);                                                \
        if (vmbin_handler_status_ == RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_status_ = vmbin_value_access_langtype(                   \
                frame, &instruction->read_arguments.data[1], &right_langtype_,     \
                &vmbin_handler_error_);                                            \
        }                                                                          \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail(output_exit, vmbin_handler_status_,                 \
                               VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,              \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        /* 唯一的按编码三选一在 `string_text_decode` 里，宏与编码无关。 */         \
        vmbin_handler_status_ = string_text_decode(                                \
            runtimec_bytes_view_make(left_position_.read_bytes,                    \
                                     left_position_.size),                         \
            left_langtype_, &left_text_);                                          \
        if (vmbin_handler_status_ == RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_status_ = string_text_decode(                            \
                runtimec_bytes_view_make(right_position_.read_bytes,               \
                                         right_position_.size),                    \
                right_langtype_, &right_text_);                                    \
        }                                                                          \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail_decode(                                             \
                output_exit, vmbin_handler_status_,                                \
                instruction->source_logical_program_counter);                      \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        function(&left_text_, &right_text_, result_bytes_,                         \
                 sizeof(result_bytes_), &result_length_, output_exit);             \
        return vmbin_handler_write_new_value(frame, instruction, result_bytes_,    \
                                             result_length_, output_exit);         \
    }

/* 一元文本：一个读参解码，结果同上。 */
#define VMBIN_HANDLER_UNARY_TEXT(handler_name, function)                           \
    static runtimec_status_t handler_name(                                         \
                             binrwir_execution_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t read_position_;                                     \
        vmbin_value_location_t write_position_;                                    \
        runtimec_string_view_t read_langtype_;                                     \
        string_text_t read_text_;                                                  \
        runtimec_error_t vmbin_handler_error_;                                     \
        runtimec_status_t vmbin_handler_status_;                                   \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 1U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &read_position_,         \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,       \
                                   output_exit);                                   \
        runtimec_error_reset(&vmbin_handler_error_);                               \
        vmbin_handler_status_ = vmbin_value_access_langtype(                       \
            frame, &instruction->read_arguments.data[0], &read_langtype_,          \
            &vmbin_handler_error_);                                                \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail(output_exit, vmbin_handler_status_,                 \
                               VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,              \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        vmbin_handler_status_ = string_text_decode(                                \
            runtimec_bytes_view_make(read_position_.read_bytes,                    \
                                     read_position_.size),                         \
            read_langtype_, &read_text_);                                          \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail_decode(output_exit, vmbin_handler_status_,          \
                                      instruction->source_logical_program_counter);\
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        function(&read_text_, write_position_.write_bytes,                         \
                 write_position_.size, output_exit);                               \
        /* 逻辑函数自己置了 exit 就必须按它回值——无条件 SUCCESS 会把失败吞掉。 */  \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }

/*
 * 三元文本：两个文本读参 + 一个整数读参（下标/边界），结果同上。
 *
 * 下标那种参数是定长的，用 `read_type` 转型并核长——这正是 `index` 与 `text`
 * 两种参数在同一条指令里共存时的形状。
 */
#define VMBIN_HANDLER_TEXT2_INDEX1(handler_name, function, index_type)             \
    static runtimec_status_t handler_name(                                         \
                             binrwir_execution_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t first_position_;                                    \
        vmbin_value_location_t second_position_;                                   \
        vmbin_value_location_t index_position_;                                    \
        vmbin_value_location_t write_position_;                                    \
        runtimec_string_view_t first_langtype_;                                    \
        runtimec_string_view_t second_langtype_;                                   \
        string_text_t first_text_;                                                 \
        string_text_t second_text_;                                                \
        runtimec_error_t vmbin_handler_error_;                                     \
        runtimec_status_t vmbin_handler_status_;                                   \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 3U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &first_position_,        \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 1U, &second_position_,       \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 2U, &index_position_,        \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,       \
                                   output_exit);                                   \
        VMBIN_HANDLER_REQUIRE(index_position_, index_type, output_exit,            \
                              instruction);                                        \
        runtimec_error_reset(&vmbin_handler_error_);                               \
        vmbin_handler_status_ = vmbin_value_access_langtype(                       \
            frame, &instruction->read_arguments.data[0], &first_langtype_,         \
            &vmbin_handler_error_);                                                \
        if (vmbin_handler_status_ == RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_status_ = vmbin_value_access_langtype(                   \
                frame, &instruction->read_arguments.data[1], &second_langtype_,    \
                &vmbin_handler_error_);                                            \
        }                                                                          \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail(output_exit, vmbin_handler_status_,                 \
                               VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,              \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        vmbin_handler_status_ = string_text_decode(                                \
            runtimec_bytes_view_make(first_position_.read_bytes,                   \
                                     first_position_.size),                        \
            first_langtype_, &first_text_);                                        \
        if (vmbin_handler_status_ == RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_status_ = string_text_decode(                            \
                runtimec_bytes_view_make(second_position_.read_bytes,              \
                                         second_position_.size),                   \
                second_langtype_, &second_text_);                                  \
        }                                                                          \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail_decode(output_exit, vmbin_handler_status_,          \
                                      instruction->source_logical_program_counter);\
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        function(&first_text_, &second_text_,                                      \
                 (const index_type *)(const void *)index_position_.read_bytes,     \
                 write_position_.write_bytes, write_position_.size);               \
        /* 逻辑函数不置 exit（该形态无失败路径），此处恒成功。 */                  \
        return RUNTIMEC_STATUS_SUCCESS;                                            \
    }


/*
 * 文本 + 下标 → **标量**：`ord(s, i)` 这类「读第 i 个码点的数值」。
 *
 * 与 `_TEXT2_INDEX1` 的区别：那个写变长结果，这个写定长标量。
 * 下标宽度在这里核，编码差异由 `string_text_decode` 承载。
 *
 * 逻辑函数签名：
 *     void f(const string_text_t *text, const index_type *index, write_type *out,
 *            binrwir_execution_exit_t *output_exit)
 */
#define VMBIN_HANDLER_TEXT_INDEX_SCALAR(handler_name, function, index_type,        \
                                        write_type)                                \
    static runtimec_status_t handler_name(                                         \
                             binrwir_execution_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t text_position_;                                     \
        vmbin_value_location_t index_position_;                                    \
        vmbin_value_location_t write_position_;                                    \
        runtimec_string_view_t text_langtype_;                                     \
        string_text_t text_;                                                       \
        runtimec_error_t vmbin_handler_error_;                                     \
        runtimec_status_t vmbin_handler_status_;                                   \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 2U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &text_position_,         \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 1U, &index_position_,        \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,       \
                                   output_exit);                                   \
        VMBIN_HANDLER_REQUIRE_WRITABLE(write_position_, output_exit, instruction); \
        VMBIN_HANDLER_REQUIRE(index_position_, index_type, output_exit,            \
                              instruction);                                        \
        VMBIN_HANDLER_REQUIRE(write_position_, write_type, output_exit,            \
                              instruction);                                        \
        runtimec_error_reset(&vmbin_handler_error_);                               \
        vmbin_handler_status_ = vmbin_value_access_langtype(                       \
            frame, &instruction->read_arguments.data[0], &text_langtype_,          \
            &vmbin_handler_error_);                                                \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail(output_exit, vmbin_handler_status_,                 \
                               VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,              \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        vmbin_handler_status_ = string_text_decode(                                \
            runtimec_bytes_view_make(text_position_.read_bytes,                    \
                                     text_position_.size),                         \
            text_langtype_, &text_);                                               \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail_decode(output_exit, vmbin_handler_status_,          \
                                      instruction->source_logical_program_counter);\
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        function(&text_,                                                          \
                 (const index_type *)(const void *)index_position_.read_bytes,     \
                 (write_type *)(void *)write_position_.write_bytes, output_exit);  \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }


/*
 * 一元文本 → **标量**：`len(s)` 这类。与 `_UNARY_TEXT` 的区别只在写参是定长标量。
 *
 * 逻辑函数多拿一个 `runtimec_string_view_t langtype`——`len` 是唯一结果依赖编码的
 * 字符串算子（utf8 返字节数、utf32 返码点数），它不解码，只看 body 长度与编码。
 */
#define VMBIN_HANDLER_UNARY_TEXT_SCALAR(handler_name, function, write_type)        \
    static runtimec_status_t handler_name(                                         \
                             binrwir_execution_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t read_position_;                                     \
        vmbin_value_location_t write_position_;                                    \
        runtimec_string_view_t read_langtype_;                                     \
        runtimec_error_t vmbin_handler_error_;                                     \
        runtimec_status_t vmbin_handler_status_;                                   \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 1U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &read_position_,         \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,       \
                                   output_exit);                                   \
        VMBIN_HANDLER_REQUIRE_WRITABLE(write_position_, output_exit, instruction); \
        VMBIN_HANDLER_REQUIRE(write_position_, write_type, output_exit,            \
                              instruction);                                        \
        runtimec_error_reset(&vmbin_handler_error_);                               \
        vmbin_handler_status_ = vmbin_value_access_langtype(                       \
            frame, &instruction->read_arguments.data[0], &read_langtype_,          \
            &vmbin_handler_error_);                                                \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail(output_exit, vmbin_handler_status_,                 \
                               VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,              \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        function(&read_position_, read_langtype_,                                  \
                 (write_type *)(void *)write_position_.write_bytes, output_exit);  \
        return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                    \
                   ? output_exit->status                                           \
                   : RUNTIMEC_STATUS_SUCCESS;                                      \
    }


/*
 * 文本 + 下标 → **新建 wire**：`char(s, i)` 这类「取出一个字符」。
 *
 * `char` 的结果是**一字符字符串**（不是整数），长度 1~4 字节取决于码点，
 * 所以走 CREATE 而不是标量写。
 *
 * 逻辑函数签名：
 *     void f(const string_text_t *text, const index_type *index,
 *            uint8_t *buffer, size_t capacity, size_t *length,
 *            binrwir_execution_exit_t *output_exit)
 */
#define VMBIN_HANDLER_TEXT_INDEX_CREATE(handler_name, function, index_type)        \
    static runtimec_status_t handler_name(                                         \
                             binrwir_execution_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t text_position_;                                     \
        vmbin_value_location_t index_position_;                                    \
        runtimec_string_view_t text_langtype_;                                     \
        uint8_t result_bytes_[VMBIN_HANDLER_CREATE_BUFFER_CAPACITY];               \
        size_t result_length_ = 0U;                                                \
        string_text_t text_;                                                       \
        runtimec_error_t vmbin_handler_error_;                                     \
        runtimec_status_t vmbin_handler_status_;                                   \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 2U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &text_position_,         \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 1U, &index_position_,        \
                                  output_exit);                                    \
        VMBIN_HANDLER_REQUIRE(index_position_, index_type, output_exit,            \
                              instruction);                                        \
        runtimec_error_reset(&vmbin_handler_error_);                               \
        vmbin_handler_status_ = vmbin_value_access_langtype(                       \
            frame, &instruction->read_arguments.data[0], &text_langtype_,          \
            &vmbin_handler_error_);                                                \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail(output_exit, vmbin_handler_status_,                 \
                               VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,              \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        vmbin_handler_status_ = string_text_decode(                                \
            runtimec_bytes_view_make(text_position_.read_bytes,                    \
                                     text_position_.size),                         \
            text_langtype_, &text_);                                               \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail_decode(output_exit, vmbin_handler_status_,          \
                                      instruction->source_logical_program_counter);\
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        function(&text_,                                                          \
                 (const index_type *)(const void *)index_position_.read_bytes,     \
                 result_bytes_, sizeof(result_bytes_), &result_length_,            \
                 output_exit);                                                     \
        return vmbin_handler_write_new_value(frame, instruction, result_bytes_,    \
                                             result_length_, output_exit);         \
    }


/*
 * 整数 + 可选基数 → **新建 wire**：`formatint(v[, base])` / `formatuint`。
 *
 * 为什么单列一族：读参个数是 1 或 2（基数可省），不是固定形状。基数缺省 10。
 * 写参走 CREATE（结果长度运行期决定）。
 *
 * 逻辑函数签名：
 *     void f(const vmbin_value_location_t *value, int64_t base, int is_signed,
 *            uint8_t *buffer, size_t capacity, size_t *length,
 *            binrwir_execution_exit_t *output_exit)
 * `is_signed` 由两个不同的薄封装给出（有符号/无符号各一条）。
 */
#define VMBIN_HANDLER_FORMAT_INTEGER(handler_name, function, is_signed_value)      \
    static runtimec_status_t handler_name(                                         \
                             binrwir_execution_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t value_position_;                                    \
        vmbin_value_location_t base_position_;                                     \
        int64_t base_ = 10;                                                        \
        uint8_t result_bytes_[VMBIN_HANDLER_CREATE_BUFFER_CAPACITY];               \
        size_t result_length_ = 0U;                                                \
                                                                                   \
        if (instruction->read_arguments.count < 1U ||                              \
            instruction->read_arguments.count > 2U ||                              \
            instruction->write_arguments.count != 1U) {                            \
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,               \
                               VMBIN_HANDLER_CALL_DIAG_WRONG_ARITY,                \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &value_position_,        \
                                  output_exit);                                    \
        if (instruction->read_arguments.count == 2U) {                             \
            VMBIN_HANDLER_LOCATE_READ(frame, instruction, 1U, &base_position_,     \
                                      output_exit);                                \
            VMBIN_HANDLER_REQUIRE(base_position_, int64_t, output_exit,            \
                                  instruction);                                    \
            memcpy(&base_, base_position_.read_bytes, sizeof(base_));              \
        }                                                                          \
        function(&value_position_, base_, (is_signed_value), result_bytes_,        \
                 sizeof(result_bytes_), &result_length_, output_exit);             \
        return vmbin_handler_write_new_value(frame, instruction, result_bytes_,    \
                                             result_length_, output_exit);         \
    }


/*
 * 1 文本 + 2 下标 → **新建 wire**：`slice(s, lo, hi)`。
 *
 * 与 `_TEXT2_INDEX1` 正好相反（那个是 2 文本 + 1 下标）。**族按参数形状切，
 * 不按语义接近切**——形状不同就得是不同族，否则宏里的类型转换会错位。
 *
 * 逻辑函数签名：
 *     void f(const string_text_t *text,
 *            const vmbin_value_location_t *bounds,   // bounds[0]=lo, bounds[1]=hi
 *            uint8_t *buffer, size_t capacity, size_t *length,
 *            binrwir_execution_exit_t *output_exit)
 */
#define VMBIN_HANDLER_TEXT_INDEX2(handler_name, function, index_type)              \
    static runtimec_status_t handler_name(                                         \
                             binrwir_execution_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t text_position_;                                     \
        vmbin_value_location_t bounds_[2];                                         \
        runtimec_string_view_t text_langtype_;                                     \
        uint8_t result_bytes_[VMBIN_HANDLER_CREATE_BUFFER_CAPACITY];               \
        size_t result_length_ = 0U;                                                \
        string_text_t text_;                                                       \
        runtimec_error_t vmbin_handler_error_;                                     \
        runtimec_status_t vmbin_handler_status_;                                   \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 3U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &text_position_,         \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 1U, &bounds_[0],             \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 2U, &bounds_[1],             \
                                  output_exit);                                    \
        runtimec_error_reset(&vmbin_handler_error_);                               \
        vmbin_handler_status_ = vmbin_value_access_langtype(                       \
            frame, &instruction->read_arguments.data[0], &text_langtype_,          \
            &vmbin_handler_error_);                                                \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail(output_exit, vmbin_handler_status_,                 \
                               VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,              \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        vmbin_handler_status_ = string_text_decode(                                \
            runtimec_bytes_view_make(text_position_.read_bytes,                    \
                                     text_position_.size),                         \
            text_langtype_, &text_);                                               \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail_decode(output_exit, vmbin_handler_status_,          \
                                      instruction->source_logical_program_counter);\
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        function(&text_, bounds_, result_bytes_, sizeof(result_bytes_),            \
                 &result_length_, output_exit);                                    \
        return vmbin_handler_write_new_value(frame, instruction, result_bytes_,    \
                                             result_length_, output_exit);         \
    }


/*
 * 文本 + 下标 + 新码点 → **按编码选写入路径**：`set(s, i, c)`。
 *
 * 这是唯一一个「同一个语义、两条写入路径」的算子，路径由**声明类型**决定：
 *   char/utf32  定宽 4 字节 → 换码点不改 body 长度 → **同槽 patch**
 *   char/utf8   码点字节数 1~4 → 换码点**改** body 长度 → **重建整串**
 *   char/ascii  同上（换非 ASCII 码点还会失败，语义上仍按变长处理）
 *
 * 若按「新旧码点字节宽是否恰好相同」来选，`'a'->'z'` 走 patch、`'a'->'é'` 走重建，
 * 同一个算子的行为取决于**数据**——那是把随机性写进了语义。
 *
 * 分岔只有一处（本宏），与 `string_text_decode` 的 `switch` 同性质：
 * **中心化的分岔不算分叉**。要改这条规则时只改这里，不用翻九个 handler。
 */
#define VMBIN_HANDLER_SET_BY_ENCODING(handler_name, patch_function,                \
                                      rebuild_function)                            \
    static runtimec_status_t handler_name(                                         \
                             binrwir_execution_frame_t *frame,                     \
                             const binrwir_instruction_view_t *instruction,        \
                             binrwir_execution_exit_t *output_exit) {              \
        vmbin_value_location_t text_position_;                                     \
        vmbin_value_location_t write_position_;                                    \
        vmbin_value_location_t position_location_;                                 \
        vmbin_value_location_t replacement_position_;                              \
        runtimec_string_view_t replacement_langtype_;                              \
        runtimec_string_view_t text_langtype_;                                     \
        string_text_t text_;                                                       \
        string_text_t replacement_;                                                \
        uint8_t rebuilt_bytes_[VMBIN_HANDLER_CREATE_BUFFER_CAPACITY];               \
        size_t rebuilt_length_ = 0U;                                               \
        int64_t position_ = 0;                                                     \
        runtimec_error_t vmbin_handler_error_;                                     \
        runtimec_status_t vmbin_handler_status_;                                   \
                                                                                   \
        VMBIN_HANDLER_REQUIRE_ARITY(instruction, 3U, 1U, output_exit);             \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 0U, &text_position_,         \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 1U, &position_location_,     \
                                  output_exit);                                    \
        VMBIN_HANDLER_LOCATE_READ(frame, instruction, 2U,                         \
                                  &replacement_position_, output_exit);            \
        VMBIN_HANDLER_REQUIRE(position_location_, int64_t, output_exit,            \
                              instruction);                                        \
        memcpy(&position_, position_location_.read_bytes, sizeof(position_));      \
        runtimec_error_reset(&vmbin_handler_error_);                               \
        vmbin_handler_status_ = vmbin_value_access_langtype(                       \
            frame, &instruction->read_arguments.data[0], &text_langtype_,          \
            &vmbin_handler_error_);                                                \
        if (vmbin_handler_status_ == RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_status_ = vmbin_value_access_langtype(                   \
                frame, &instruction->read_arguments.data[2],                       \
                &replacement_langtype_, &vmbin_handler_error_);                   \
        }                                                                          \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail(output_exit, vmbin_handler_status_,                 \
                               VMBIN_HANDLER_CALL_DIAG_LOCATE_FAILED,              \
                               instruction->source_logical_program_counter);       \
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        vmbin_handler_status_ = string_text_decode(                                \
            runtimec_bytes_view_make(text_position_.read_bytes,                    \
                                     text_position_.size),                         \
            text_langtype_, &text_);                                               \
        if (vmbin_handler_status_ == RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_status_ = string_text_decode(                            \
                runtimec_bytes_view_make(replacement_position_.read_bytes,         \
                                         replacement_position_.size),              \
                replacement_langtype_, &replacement_);                             \
        }                                                                          \
        if (vmbin_handler_status_ != RUNTIMEC_STATUS_SUCCESS) {                    \
            vmbin_handler_fail_decode(output_exit, vmbin_handler_status_,          \
                                      instruction->source_logical_program_counter);\
            return RUNTIMEC_STATUS_ERROR;                                          \
        }                                                                          \
        /* 分岔只此一处：定宽的走 patch，变长的重建整串。 */                       \
        if (string_text_encoding_is_fixed_width(text_langtype_)) {                 \
            VMBIN_HANDLER_LOCATE_WRITE(frame, instruction, 0U, &write_position_,   \
                                       output_exit);                               \
            patch_function(&text_, &write_position_, &position_, &replacement_,    \
                           output_exit);                                           \
            return output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR                \
                       ? output_exit->status                                       \
                       : RUNTIMEC_STATUS_SUCCESS;                                  \
        }                                                                          \
        rebuild_function(&text_, &position_, &replacement_, rebuilt_bytes_,        \
                         sizeof(rebuilt_bytes_), &rebuilt_length_, output_exit);   \
        if (output_exit->kind == BINRWIR_EXECUTION_EXIT_ERROR) {                   \
            return output_exit->status;                                            \
        }                                                                          \
        return vmbin_handler_write_new_value(frame, instruction, rebuilt_bytes_,   \
                                             rebuilt_length_, output_exit);        \
    }

#ifdef __cplusplus
}
#endif

#endif
