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

#include <float.h>
#include <stdio.h>
#include <string.h>

#include "contract/char_codec.h"

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
 * 参数项的声明 langtype 走 `vmbin_value_access_langtype`——那是唯一取类型的
 * 入口，本文件不再留私有副本。取不到就是空视图，调用方据此失败封闭。
 */
static runtimec_string_view_t runtimec_temp_println_langtype(
    const binrwir_execution_frame_t *frame,
    const binrwir_argument_t *argument) {
    runtimec_string_view_t langtype = runtimec_string_view_make(NULL, 0U);
    runtimec_error_t error;

    runtimec_error_reset(&error);
    if (vmbin_value_access_langtype(frame, argument, &langtype, &error) !=
        RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_string_view_make(NULL, 0U);
    }
    return langtype;
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
/*
 * 浮点值的唯一格式化实现——`float32` 与 `float64` **共用这一条**。
 *
 * 为什么共用：补 `.0` 的判据是「值的 kind 是浮点」，与宽度无关。为第二种宽度
 * 再写一份，就是同一判据的第二处真相——两边迟早漂开，而漂开的形态是
 * 「float32 打 `12`、float64 打 `12.0`」这种没人会立刻发现的不一致。
 *
 * 为什么用 `%g`：紧凑。`%f` 会把 `0.1` 打成 `0.100000`，把有效数字埋进一串零里。
 *
 * 为什么整数形态补 `.0`：**Python 阵营**，不是 C/Go/JS——
 *   C `%g` → `12`、Go → `12`、JS → `12`、**Python → `12.0`**
 * 两个理由：
 *   1. 语料期望就是 `12.0`（`tutorial/09-operators/prefix_unary.kv` 的 `√144`）；
 *   2. `12` 与 `12.0` 有歧义——前者看起来像 int64。一个浮点的打印结果若长得
 *      像整数，读者就分不出它的 kind，这在调试里是实打实的误导。
 *
 * 判据是**值本身的 kind**（调用方已按 langtype 分流进来），不是事后看字符串
 * 像不像整数；`inf`/`nan` 不补，它们不是「看起来像整数」。
 */
static int runtimec_temp_println_render_float(double value, char *scratch,
                                              size_t capacity,
                                              size_t *output_length) {
    int written = snprintf(scratch, capacity, "%g", value);

    if (written <= 0 || (size_t)written >= capacity) {
        return 0;
    }
    /* 已含小数点或指数（`3.5`、`1e+20`）就原样用。 */
    if (strpbrk(scratch, ".eE") == NULL) {
        const int is_non_finite =
            value != value || value > DBL_MAX || value < -DBL_MAX;

        if (!is_non_finite) {
            if ((size_t)written + 2U >= capacity) {
                return 0;
            }
            scratch[written] = '.';
            scratch[written + 1] = '0';
            written += 2;
            scratch[written] = '\0';
        }
    }
    *output_length = (size_t)written;
    return 1;
}

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
        /* 码点序列（4 字节小端）→ UTF-8；实现只在 contract/char_codec 一处。 */
        size_t used = 0U;

        if (!runtimec_char_utf32_to_utf8(body, size, scratch, capacity, &used)) {
            return 0;
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
    /* 浮点两种宽度走**同一个**格式化实现，只差 memcpy 的目标类型。 */
    if (runtimec_temp_println_is(langtype, "float64")) {
        double value = 0.0;

        if (size != sizeof(value)) {
            return 0;
        }
        memcpy(&value, body, sizeof(value));
        return runtimec_temp_println_render_float(value, scratch, capacity,
                                                  output_length);
    }
    if (runtimec_temp_println_is(langtype, "float32")) {
        float narrow = 0.0f;

        if (size != sizeof(narrow)) {
            return 0;
        }
        memcpy(&narrow, body, sizeof(narrow));
        /* 窄化到 double 是**保值**的（float32 的每个值都能精确表示为 double），
         * 所以这里不引入额外舍入；`%g` 对 double 打印再按需去掉尾零，
         * 结果与直接在 float 上打印一致。 */
        return runtimec_temp_println_render_float((double)narrow, scratch,
                                                  capacity, output_length);
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
        runtimec_status_t status = vmbin_value_access_read_body(frame, argument,
                                                                &buffer, &error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return runtimec_temp_println_fail(
                output_exit, status, RUNTIMEC_TEMP_PRINTLN_DIAG_VALUE_ACCESS,
                source_counter);
        }
        /* 渲染要的是 body：`read_body` 已把字面量的 wire 剥好，这里不再判种类。 */
        if (!runtimec_temp_println_render(langtype, buffer.data, buffer.size,
                                          scratch, sizeof(scratch),
                                          &text_length)) {
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
    {.operation_identifier = RUNTIMEC_TEMP_PRINTLN_OPERATION_IDENTIFIER,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_temp_println_handler},
    {.operation_identifier = RUNTIMEC_TEMP_PRINT_OPERATION_IDENTIFIER,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_temp_print_handler},
    {.operation_identifier = RUNTIMEC_TEMP_CERR_OPERATION_IDENTIFIER,
     .flags = BINRWIR_REGISTRY_FLAG_EFFECT,
     .handler = runtimec_temp_cerr_handler},
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
