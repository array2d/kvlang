/*
 * 组件：vmbin/temp_println 的实现（**临时**）
 *
 * 正式实现归 byteseek 扩展 runtime（issue #622 决策 D）。这里只为让 tutorial 的
 * runtimec 分组能被 runtimec 自己跑通。整体由 `RUNTIMEC_TEMP_PRINTLN` 开关控制，
 * 默认 OFF；关闭时本文件不参与编译，三个算子回到「未知算子 → FOREIGN」原行为。
 *
 * 只支持最小类型集，其余**失败封闭报 UNSUPPORTED**：
 *     char/utf8          原样输出
 *     char/utf32         转 UTF-8
 *     int64 / uint64     十进制
 *     bool               true / false
 *     float64            十进制
 *   **其它一律 UNSUPPORTED**——不许静默打十六进制、不许跳过。
 *   「不支持就打十六进制」是最坏的选择：它让输出看起来正常，实际是错的。
 *   完整格式化归 byteseek，这里不扩展。
 */

#ifdef RUNTIMEC_TEMP_PRINTLN

#include "temp_println.h"

#include <stdio.h>
#include <string.h>

#include "binrwir/execution_exit.h"
#include "binrwir/execution_frame.h"
#include "binrwir/instruction.h"
#include "binrwir/program.h"
#include "contract/status.h"
#include "vmbin/value_access.h"

#define RUNTIMEC_TEMP_PRINTLN_SCRATCH_CAPACITY 64U

typedef enum runtimec_temp_println_diagnostic {
    RUNTIMEC_TEMP_PRINTLN_DIAG_VALUE_ACCESS = 1,
    RUNTIMEC_TEMP_PRINTLN_DIAG_UNSUPPORTED_TYPE = 2,
    RUNTIMEC_TEMP_PRINTLN_DIAG_MALFORMED_VALUE = 3
} runtimec_temp_println_diagnostic_t;

static runtimec_status_t runtimec_temp_println_fail(
    binrwir_execution_exit_t *output_exit, runtimec_status_t status,
    runtimec_temp_println_diagnostic_t detail, uint32_t source_counter) {
    *output_exit = (binrwir_execution_exit_t){0};
    output_exit->kind = BINRWIR_EXECUTION_EXIT_ERROR;
    output_exit->status = status;
    output_exit->detail_code = (uint32_t)detail;
    output_exit->logical_program_counter = source_counter;
    output_exit->resume_logical_program_counter = source_counter;
    return status;
}

/*
 * 参数项的具体 langtype。三个来源按引用种类取，取不到返回空视图：
 *   LITERAL          → 字面量池的 langtype
 *   LOCAL            → 符号池的 langtype（物化局部时用同一份）
 *   PARAMETER_*      → 签名的形参类型表
 *   STATIC           → 符号池（静态 key 也登记在那里）
 * 取不到类型不是「不约束」，是「本临时实现答不出」——调用方据此失败封闭。
 */
static runtimec_string_view_t runtimec_temp_println_langtype(
    const binrwir_execution_frame_t *frame,
    const binrwir_argument_t *argument) {
    runtimec_string_view_t empty = runtimec_string_view_make(NULL, 0U);

    if (argument->reference_kind == BINRWIR_REFERENCE_LITERAL) {
        runtimec_bytes_view_t wire;
        runtimec_string_view_t langtype;

        if (binrwir_program_get_literal(frame->program,
                                        (uint32_t)argument->payload, &wire,
                                        &langtype) == RUNTIMEC_STATUS_SUCCESS) {
            return langtype;
        }
        return empty;
    }
    if (argument->reference_kind == BINRWIR_REFERENCE_LOCAL ||
        argument->reference_kind == BINRWIR_REFERENCE_STATIC) {
        runtimec_string_view_t name;
        runtimec_string_view_t langtype;

        if (binrwir_program_get_symbol(frame->program, argument->slot, &name,
                                       &langtype) == RUNTIMEC_STATUS_SUCCESS) {
            return langtype;
        }
        return empty;
    }
    if (argument->reference_kind == BINRWIR_REFERENCE_PARAMETER_READ ||
        argument->reference_kind == BINRWIR_REFERENCE_PARAMETER_WRITE) {
        binrwir_program_view_t view;
        const runtimec_string_view_t *types = NULL;
        size_t count = 0U;

        if (binrwir_program_get_view(frame->program, &view) !=
            RUNTIMEC_STATUS_SUCCESS) {
            return empty;
        }
        if (argument->reference_kind == BINRWIR_REFERENCE_PARAMETER_READ) {
            types = view.signature.read_parameter_types;
            count = view.signature.read_parameter_count;
        } else {
            types = view.signature.write_parameter_types;
            count = view.signature.write_parameter_count;
        }
        if (types != NULL && (size_t)argument->slot < count) {
            return types[argument->slot];
        }
        return empty;
    }
    return empty;
}

/*
 * langtype 的**基种类**是否就是 text。
 *
 * 值槽的 langtype 带维度前缀（`[2]char/utf32`、`[4]int64`），而符号池/签名里的
 * 是裸种类（`char/utf32`、`int64`）。这里剥掉前导 `[dims]` 再比，**不做其它
 * 归一化**——`[]char/utf8` 这类变长形态的 dims 在方括号里，剥完后同样是基种类。
 */
static int runtimec_temp_println_is(runtimec_string_view_t langtype,
                                    const char *text) {
    const size_t size = strlen(text);
    size_t offset = 0U;

    if (langtype.data != NULL && langtype.size != 0U && langtype.data[0] == '[') {
        while (offset < langtype.size && langtype.data[offset] != ']') {
            ++offset;
        }
        if (offset < langtype.size) {
            ++offset; /* 跳过 ']' */
        }
    }
    return langtype.size - offset == size &&
           (size == 0U ||
            memcmp(langtype.data + offset, text, size) == 0);
}

/*
 * 单个读参 → 输出文本。text 借用 scratch（足够放下本临时实现支持的定长标量），
 * 返回文本长度；不支持的类型返回 false 由调用方失败封闭。
 */
static int runtimec_temp_println_render(runtimec_string_view_t langtype,
                                        const uint8_t *body, size_t size,
                                        char *scratch, size_t capacity,
                                        size_t *output_length) {
    *output_length = 0U;
    if (runtimec_temp_println_is(langtype, "char/utf8")) {
        /* 原样输出：长度必须装得下，不截断。 */
        if (size >= capacity) {
            return 0;
        }
        if (size != 0U) {
            memcpy(scratch, body, size);
        }
        *output_length = size;
        return 1;
    }
    if (runtimec_temp_println_is(langtype, "char/utf32")) {
        /* 码点序列（4 字节小端）→ UTF-8。 */
        size_t used = 0U;

        if (size % 4U != 0U) {
            return 0;
        }
        for (size_t offset = 0U; offset < size; offset += 4U) {
            uint32_t codepoint = (uint32_t)body[offset] |
                                 ((uint32_t)body[offset + 1U] << 8U) |
                                 ((uint32_t)body[offset + 2U] << 16U) |
                                 ((uint32_t)body[offset + 3U] << 24U);
            uint8_t encoded[4];
            size_t encoded_size;

            if (codepoint <= 0x7FU) {
                encoded[0] = (uint8_t)codepoint;
                encoded_size = 1U;
            } else if (codepoint <= 0x7FFU) {
                encoded[0] = (uint8_t)(0xC0U | (codepoint >> 6U));
                encoded[1] = (uint8_t)(0x80U | (codepoint & 0x3FU));
                encoded_size = 2U;
            } else if (codepoint <= 0xFFFFU) {
                encoded[0] = (uint8_t)(0xE0U | (codepoint >> 12U));
                encoded[1] = (uint8_t)(0x80U | ((codepoint >> 6U) & 0x3FU));
                encoded[2] = (uint8_t)(0x80U | (codepoint & 0x3FU));
                encoded_size = 3U;
            } else if (codepoint <= 0x10FFFFU) {
                encoded[0] = (uint8_t)(0xF0U | (codepoint >> 18U));
                encoded[1] = (uint8_t)(0x80U | ((codepoint >> 12U) & 0x3FU));
                encoded[2] = (uint8_t)(0x80U | ((codepoint >> 6U) & 0x3FU));
                encoded[3] = (uint8_t)(0x80U | (codepoint & 0x3FU));
                encoded_size = 4U;
            } else {
                return 0;
            }
            if (used + encoded_size >= capacity) {
                return 0;
            }
            memcpy(scratch + used, encoded, encoded_size);
            used += encoded_size;
        }
        *output_length = used;
        return 1;
    }
    if (runtimec_temp_println_is(langtype, "int64")) {
        int64_t value = 0;
        int written;

        if (size != sizeof(value)) {
            return 0;
        }
        memcpy(&value, body, sizeof(value));
        written = snprintf(scratch, capacity, "%lld", (long long)value);
        if (written <= 0 || (size_t)written >= capacity) {
            return 0;
        }
        *output_length = (size_t)written;
        return 1;
    }
    if (runtimec_temp_println_is(langtype, "uint64")) {
        uint64_t value = 0;
        int written;

        if (size != sizeof(value)) {
            return 0;
        }
        memcpy(&value, body, sizeof(value));
        written = snprintf(scratch, capacity, "%llu",
                           (unsigned long long)value);
        if (written <= 0 || (size_t)written >= capacity) {
            return 0;
        }
        *output_length = (size_t)written;
        return 1;
    }
    if (runtimec_temp_println_is(langtype, "bool")) {
        if (size != 1U) {
            return 0;
        }
        const char *text = body[0] != 0U ? "true" : "false";
        const size_t length = strlen(text);

        if (length >= capacity) {
            return 0;
        }
        memcpy(scratch, text, length);
        *output_length = length;
        return 1;
    }
    if (runtimec_temp_println_is(langtype, "float64")) {
        double value = 0.0;
        int written;

        if (size != sizeof(value)) {
            return 0;
        }
        memcpy(&value, body, sizeof(value));
        /* 与 byteseek 的显示格式一致：%g 风格，避免 "3.000000"。 */
        written = snprintf(scratch, capacity, "%g", value);
        if (written <= 0 || (size_t)written >= capacity) {
            return 0;
        }
        *output_length = (size_t)written;
        return 1;
    }
    return 0;
}

/*
 * 三条算子的共同实现：读参按分隔符拼接后写到 stdout / stderr。
 * 分隔符与换行照 `byteseek/src/rwir/term.rs:18`：
 *     println  " " 分隔、写完换行
 *     print    ""  分隔、不换行
 *     cerr     " " 分隔、写 stderr、写完换行
 */
static runtimec_status_t runtimec_temp_println_run(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction, const char *separator,
    int use_stderr, int trailing_newline,
    binrwir_execution_exit_t *output_exit) {
    const uint32_t source_counter = instruction->source_logical_program_counter;
    FILE *stream = use_stderr != 0 ? stderr : stdout;
    runtimec_error_t error;

    runtimec_error_reset(&error);
    for (size_t ordinal = 0U; ordinal < instruction->read_arguments.count;
         ++ordinal) {
        const binrwir_argument_t *argument =
            &instruction->read_arguments.data[ordinal];
        const runtimec_string_view_t langtype =
            runtimec_temp_println_langtype(frame, argument);
        uint8_t body[RUNTIMEC_TEMP_PRINTLN_SCRATCH_CAPACITY];
        runtimec_bytes_buffer_t buffer = {body, 0U, sizeof(body)};
        char scratch[RUNTIMEC_TEMP_PRINTLN_SCRATCH_CAPACITY];
        size_t text_length = 0U;

        if (langtype.size == 0U) {
            /* 取不到类型 = 本临时实现答不出，失败封闭而不是猜。 */
            return runtimec_temp_println_fail(
                output_exit, RUNTIMEC_STATUS_UNSUPPORTED,
                RUNTIMEC_TEMP_PRINTLN_DIAG_UNSUPPORTED_TYPE, source_counter);
        }
        runtimec_status_t status = vmbin_value_access_read(frame, argument,
                                                           &buffer, &error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return runtimec_temp_println_fail(
                output_exit, status, RUNTIMEC_TEMP_PRINTLN_DIAG_VALUE_ACCESS,
                source_counter);
        }
        /*
         * 字面量读出来是**完整 wire**（16 字节 head + body + attr），
         * 而渲染要的是 body。跳过头得经合同的推导函数，不手写偏移。
         * 其它引用种类读出的已经是值体本身。
         */
        const uint8_t *value_body = buffer.data;
        size_t value_body_size = buffer.size;
        if (argument->reference_kind == BINRWIR_REFERENCE_LITERAL) {
            const runtimec_value_body_address_t body_address =
                runtimec_xvalue_body_address(
                    (runtimec_xvalue_address_t)(uintptr_t)buffer.data);
            if (body_address == RUNTIMEC_VALUE_BODY_ADDRESS_NONE) {
                return runtimec_temp_println_fail(
                    output_exit, RUNTIMEC_STATUS_INVALID,
                    RUNTIMEC_TEMP_PRINTLN_DIAG_MALFORMED_VALUE, source_counter);
            }
            value_body_size = runtimec_value_body_length(body_address);
            value_body = (const uint8_t *)(uintptr_t)body_address;
        }
        if (!runtimec_temp_println_render(langtype, value_body,
                                          value_body_size, scratch,
                                          sizeof(scratch), &text_length)) {
            /* 支持的类型但形状不符（长度不对、非 UTF-32 边界）也算无法渲染。 */
            return runtimec_temp_println_fail(
                output_exit,
                langtype.size == 0U ? RUNTIMEC_STATUS_UNSUPPORTED
                                    : RUNTIMEC_STATUS_ERROR,
                RUNTIMEC_TEMP_PRINTLN_DIAG_MALFORMED_VALUE, source_counter);
        }
        if (ordinal != 0U && separator[0] != '\0') {
            fputs(separator, stream);
        }
        if (text_length != 0U) {
            fwrite(scratch, 1U, text_length, stream);
        }
    }
    if (trailing_newline != 0) {
        fputc('\n', stream);
    }
    fflush(stream);
    *output_exit = (binrwir_execution_exit_t){0};
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t runtimec_temp_println_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return runtimec_temp_println_run(frame, instruction, " ", 0, 1,
                                     output_exit);
}

static runtimec_status_t runtimec_temp_print_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return runtimec_temp_println_run(frame, instruction, "", 0, 0,
                                     output_exit);
}

static runtimec_status_t runtimec_temp_cerr_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return runtimec_temp_println_run(frame, instruction, " ", 1, 1,
                                     output_exit);
}

/* 三条条目的唯一定义点；开关关闭时整个文件不参与编译。 */
static const binrwir_registry_entry_t runtimec_temp_println_entry_table[] = {
    {RUNTIMEC_TEMP_PRINTLN_OPERATION_IDENTIFIER, BINRWIR_REGISTRY_FLAG_EFFECT,
     runtimec_temp_println_handler},
    {RUNTIMEC_TEMP_PRINT_OPERATION_IDENTIFIER, BINRWIR_REGISTRY_FLAG_EFFECT,
     runtimec_temp_print_handler},
    {RUNTIMEC_TEMP_CERR_OPERATION_IDENTIFIER, BINRWIR_REGISTRY_FLAG_EFFECT,
     runtimec_temp_cerr_handler},
};

const binrwir_registry_entry_t *runtimec_temp_println_entries(
    size_t *output_count) {
    if (output_count != NULL) {
        *output_count = sizeof(runtimec_temp_println_entry_table) /
                        sizeof(runtimec_temp_println_entry_table[0]);
    }
    return runtimec_temp_println_entry_table;
}

#else
/*
 * 开关关闭时留一个内部链接的占位声明：ISO C 禁止空翻译单元（-Wpedantic 会报错），
 * 而这里确实什么也不该产出。static 符号不出现在任何产物符号表里。
 */
typedef int runtimec_temp_println_disabled_t;
/* 只用类型、不建对象：类型声明不产生符号，也满足「非空翻译单元」。 */
typedef runtimec_temp_println_disabled_t
    runtimec_temp_println_disabled_unused_t;
#endif /* RUNTIMEC_TEMP_PRINTLN */
