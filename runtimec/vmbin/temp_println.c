/*
 * 组件：vmbin/temp_println 的实现（**临时**）
 *
 * 正式实现归 byteseek 扩展 runtime（issue #622 决策 D）。这里只为让 tutorial 的
 * runtimec 分组能被 runtimec 自己跑通。整体由 `RUNTIMEC_TEMP_PRINTLN` 开关控制，
 * 默认 OFF；关闭时本文件不参与编译，三个算子回到「未知算子 → FOREIGN」原行为。
 *
 * 只支持最小类型集，其余**失败封闭报 UNSUPPORTED**：
 *     char/utf8 / char/utf32   文本（变长的按码点分块，不受栈缓冲限制）
 *     int8/int16/int32/int64   有符号十进制
 *     uint8/uint16/uint32/uint64 无符号十进制
 *     bool                     true / false
 *     float32 / float64        十进制
 *   **其它一律 UNSUPPORTED**——不许静默打十六进制、不许跳过。
 *   「不支持就打十六进制」是最坏的选择：它让输出看起来正常，实际是错的。
 *   完整格式化归 byteseek，这里不扩展。
 */

#ifdef RUNTIMEC_TEMP_PRINTLN

#include "temp_println.h"
#include "contract/diagnostic_band.h"

#include <float.h>
#include <stdio.h>
#include <string.h>

#include "contract/char_codec.h"
#include "contract/float_format.h"

#include "binrwir/execution_exit.h"
#include "binrwir/binvthread_frame.h"
#include "binrwir/instruction.h"
#include "binrwir/binfunc.h"
#include "contract/status.h"
#include "vmbin/value_access.h"

#define RUNTIMEC_TEMP_PRINTLN_SCRATCH_CAPACITY 64U

typedef enum runtimec_temp_println_diagnostic {
    RUNTIMEC_TEMP_PRINTLN_DIAG_VALUE_ACCESS = RUNTIMEC_DIAGNOSTIC_BAND_TEMP_PRINTLN + 1U,
    RUNTIMEC_TEMP_PRINTLN_DIAG_UNSUPPORTED_TYPE = RUNTIMEC_DIAGNOSTIC_BAND_TEMP_PRINTLN + 2U,
    RUNTIMEC_TEMP_PRINTLN_DIAG_MALFORMED_VALUE = RUNTIMEC_DIAGNOSTIC_BAND_TEMP_PRINTLN + 3U
} runtimec_temp_println_diagnostic_t;

_Static_assert(
    RUNTIMEC_DIAGNOSTIC_IN_BAND(RUNTIMEC_TEMP_PRINTLN_DIAG_VALUE_ACCESS,
                                RUNTIMEC_DIAGNOSTIC_BAND_TEMP_PRINTLN),
    "temp_println diagnostics must stay in its own band");

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
    const binrwir_binvthread_frame_t *frame,
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
/*
 * 浮点格式化**已移入 contract/float_format**——本函数只剩转发。
 *
 * 移走的理由是位置：把唯一正确的浮点格式化放进一个「计划要删」的临时组件，
 * 删它时就得把能力一起搬走，那是把一个临时决定延后成一次迁移工作。
 *
 * 这里只做一件事：把 float32 的**窄化 + 域选择**拼好交给合同。值本身以 double
 * 传入（float32 的每个值都能精确表示为 double，不引入舍入），**往返域由宽度决定**。
 */
static int runtimec_temp_println_render_float(double value, int is_float32,
                                              char *scratch, size_t capacity,
                                              size_t *output_length) {
    return runtimec_float_format_shortest(
               value,
               is_float32 != 0 ? RUNTIMEC_FLOAT_DOMAIN_FLOAT32
                               : RUNTIMEC_FLOAT_DOMAIN_FLOAT64,
               scratch, capacity, output_length)
           ? 1
           : 0;
}

/*
 * 渲染结果：要么「已全部写到 stream」，要么报一个**具体原因**。
 *
 * `render` 曾经返回 int，把「不支持的类型」「形状不符」「缓冲装不下」压成同一个
 * 假值，调用方只好一律包装成 VALUE_ACCESS（0x4501）。输出因此只说「访问值出错」，
 * 真正原因——值有 200 字节而缓冲只有 64——一个字都没有。三种原因分开之后，
 * 每条失败都指向自己那一行。
 */
typedef enum runtimec_temp_println_render_result {
    RUNTIMEC_TEMP_PRINTLN_RENDER_OK = 0,
    /* 类型不在本临时实现的支持集内（含窄整数以外的未知 kind）。 */
    RUNTIMEC_TEMP_PRINTLN_RENDER_UNSUPPORTED_TYPE,
    /* 类型认识，但 body 的字节数与该类型对不上（标量宽度不符）。 */
    RUNTIMEC_TEMP_PRINTLN_RENDER_MALFORMED,
} runtimec_temp_println_render_result_t;

/*
 * 窄整数：langtype → 宽度与符号性。
 *
 * 曾经 `render` 只认 `int64`/`uint64`，`int8`/`int16`/`int32`/`uint8`/`uint16`/
 * `uint32` 全部落到末尾的 `return 0`，于是「int8(127)+int8(1) 打印」直接
 * MALFORMED_VALUE。**判据是 name，不是宽度**：`uint8` 与 `int8` 同宽，符号性
 * 只存在于名字里。
 */
typedef struct runtimec_temp_println_integer {
    size_t width;
    int is_signed;
} runtimec_temp_println_integer_t;

static int runtimec_temp_println_integer_of(runtimec_string_view_t langtype,
                                            runtimec_temp_println_integer_t *output) {
    static const struct {
        const char *name;
        size_t width;
        int is_signed;
    } table[] = {
        {"int8", 1U, 1},   {"int16", 2U, 1},  {"int32", 4U, 1},
        {"int64", 8U, 1},  {"uint8", 1U, 0},  {"uint16", 2U, 0},
        {"uint32", 4U, 0}, {"uint64", 8U, 0},
    };
    for (size_t ordinal = 0U; ordinal < sizeof(table) / sizeof(table[0]);
         ++ordinal) {
        if (runtimec_temp_println_is(langtype, table[ordinal].name)) {
            output->width = table[ordinal].width;
            output->is_signed = table[ordinal].is_signed;
            return 1;
        }
    }
    return 0;
}

/* 小端读一个至多 8 字节的无符号整数；拓宽到 uint64_t 后按符号位还原。 */
static uint64_t runtimec_temp_println_load_unsigned(const uint8_t *body,
                                                    size_t width) {
    uint64_t value = 0U;

    for (size_t index = 0U; index < width; ++index) {
        value |= (uint64_t)body[index] << (8U * index);
    }
    return value;
}

static int runtimec_temp_println_render_integer(
    runtimec_string_view_t langtype, const uint8_t *body, size_t size,
    char *scratch, size_t capacity, size_t *output_length) {
    runtimec_temp_println_integer_t integer;
    int written;

    if (!runtimec_temp_println_integer_of(langtype, &integer) ||
        size != integer.width) {
        return 0;
    }
    const uint64_t raw = runtimec_temp_println_load_unsigned(body, integer.width);
    if (integer.is_signed != 0) {
        /* 按宽度符号扩展：`int8` 的 0xFF 是 -1，不是 255。 */
        const unsigned shift = (unsigned)(64U - 8U * integer.width);
        const int64_t value = (int64_t)(raw << shift) >> shift;

        written = snprintf(scratch, capacity, "%lld", (long long)value);
    } else {
        written = snprintf(scratch, capacity, "%llu",
                           (unsigned long long)raw);
    }
    if (written <= 0 || (size_t)written >= capacity) {
        return 0;
    }
    *output_length = (size_t)written;
    return 1;
}

/*
 * 定长文本（`char/utf8`）的超长处理：**分块写**，不要求整串装进栈缓冲。
 *
 * 曾经整串先 memcpy 进 64 字节 scratch，于是任何超过 64 字节的字符串都是
 * `INSUFFICIENT_CAPACITY`——而那个 status 又被包装成 VALUE_ACCESS，报出来的
 * 是「访问值出错」，与真实原因（值太长）毫无关系。
 */
static int runtimec_temp_println_write_direct(const uint8_t *body, size_t size,
                                              FILE *stream) {
    if (size != 0U) {
        fwrite(body, 1U, size, stream);
    }
    return 1;
}

/*
 * 变长文本（`char/utf32`）的超长处理：按**码点**分块解码。
 *
 * 不能按字节切——切在码点中间会产出半个序列。所以以 `capacity - 1` 为上限
 * 尽量多装完整码点，编码一块、写一块、清一块。零分配：视图直接借用值体。
 */
static int runtimec_temp_println_write_utf32(const uint8_t *body, size_t size,
                                             char *scratch, size_t capacity,
                                             FILE *stream) {
    size_t offset = 0U;

    if (size % 4U != 0U) {
        return 0;
    }
    while (offset < size) {
        /* 每块取「装得下多少完整码点」：一个码点最多 4 字节 UTF-8，
         * 故块长上限是 (capacity - 1) / 4 * 4 字节的输入。 */
        size_t chunk = size - offset;
        size_t produced = 0U;

        const size_t maximum = (capacity > 1U ? capacity - 1U : 0U) / 4U * 4U;
        if (chunk > maximum) {
            chunk = maximum;
        }
        if (chunk == 0U) {
            return 0;
        }
        if (!runtimec_char_utf32_to_utf8(body + offset, chunk, scratch, capacity,
                                         &produced)) {
            return 0;
        }
        if (produced != 0U) {
            fwrite(scratch, 1U, produced, stream);
        }
        offset += chunk;
    }
    return 1;
}

/*
 * 单个读参 → 写出到 stream。返回**具体原因**，不压成一个假值。
 *
 * 支持集：`char/utf8`、`char/utf32`、八个整数宽度、`bool`、`float32`、`float64`。
 * 其余一律 UNSUPPORTED（不许静默打十六进制——那让输出看起来正常、实际是错的）。
 */
static runtimec_temp_println_render_result_t runtimec_temp_println_render(
    runtimec_string_view_t langtype, const uint8_t *body, size_t size,
    char *scratch, size_t capacity, FILE *stream) {
    if (runtimec_temp_println_is(langtype, "char/utf8")) {
        if (!runtimec_temp_println_write_direct(body, size, stream)) {
            return RUNTIMEC_TEMP_PRINTLN_RENDER_MALFORMED;
        }
        return RUNTIMEC_TEMP_PRINTLN_RENDER_OK;
    }
    if (runtimec_temp_println_is(langtype, "char/utf32")) {
        if (!runtimec_temp_println_write_utf32(body, size, scratch, capacity,
                                               stream)) {
            return RUNTIMEC_TEMP_PRINTLN_RENDER_MALFORMED;
        }
        return RUNTIMEC_TEMP_PRINTLN_RENDER_OK;
    }
    if (runtimec_temp_println_is(langtype, "bool")) {
        static const char true_text[] = "true";
        static const char false_text[] = "false";
        const char *text;
        size_t length;

        if (size != 1U) {
            return RUNTIMEC_TEMP_PRINTLN_RENDER_MALFORMED;
        }
        text = body[0] != 0U ? true_text : false_text;
        length = body[0] != 0U ? sizeof(true_text) - 1U : sizeof(false_text) - 1U;
        if (length >= capacity) {
            return RUNTIMEC_TEMP_PRINTLN_RENDER_MALFORMED;
        }
        memcpy(scratch, text, length);
        fwrite(scratch, 1U, length, stream);
        return RUNTIMEC_TEMP_PRINTLN_RENDER_OK;
    }
    /* 浮点两种宽度走**同一个**格式化实现，只差 memcpy 的目标类型。 */
    if (runtimec_temp_println_is(langtype, "float64")) {
        double value = 0.0;
        size_t length = 0U;

        if (size != sizeof(value)) {
            return RUNTIMEC_TEMP_PRINTLN_RENDER_MALFORMED;
        }
        memcpy(&value, body, sizeof(value));
        if (!runtimec_temp_println_render_float(value, 0, scratch, capacity,
                                                &length)) {
            return RUNTIMEC_TEMP_PRINTLN_RENDER_MALFORMED;
        }
        fwrite(scratch, 1U, length, stream);
        return RUNTIMEC_TEMP_PRINTLN_RENDER_OK;
    }
    if (runtimec_temp_println_is(langtype, "float32")) {
        float narrow = 0.0f;
        size_t length = 0U;

        if (size != sizeof(narrow)) {
            return RUNTIMEC_TEMP_PRINTLN_RENDER_MALFORMED;
        }
        memcpy(&narrow, body, sizeof(narrow));
        /* 窄化到 double 是**保值**的（float32 的每个值都能精确表示为 double），
         * 所以这里不引入额外舍入。 */
        if (!runtimec_temp_println_render_float((double)narrow, 1, scratch,
                                                capacity, &length)) {
            return RUNTIMEC_TEMP_PRINTLN_RENDER_MALFORMED;
        }
        fwrite(scratch, 1U, length, stream);
        return RUNTIMEC_TEMP_PRINTLN_RENDER_OK;
    }
    {
        size_t length = 0U;

        if (runtimec_temp_println_integer_of(langtype, &(runtimec_temp_println_integer_t){0, 0})) {
            if (!runtimec_temp_println_render_integer(langtype, body, size,
                                                      scratch, capacity,
                                                      &length)) {
                return RUNTIMEC_TEMP_PRINTLN_RENDER_MALFORMED;
            }
            fwrite(scratch, 1U, length, stream);
            return RUNTIMEC_TEMP_PRINTLN_RENDER_OK;
        }
    }
    return RUNTIMEC_TEMP_PRINTLN_RENDER_UNSUPPORTED_TYPE;
}

static runtimec_status_t runtimec_temp_println_run(
    binrwir_binvthread_frame_t *frame,
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
        /*
         * **零拷贝取 body**：`vmbin_value_access_locate_body` 给只读视图（字面量
         * 指向池内、局部指向存储），不像 `read_body` 那样要求调用方先备一块装得下
         * 整值的缓冲。曾经的 64 字节 scratch 让任何长字符串都报
         * INSUFFICIENT_CAPACITY——一个与「值访问」毫无关系的成因，却被包装成
         * VALUE_ACCESS 报出来。
         */
        vmbin_value_location_t location;
        char scratch[RUNTIMEC_TEMP_PRINTLN_SCRATCH_CAPACITY];

        if (langtype.size == 0U) {
            /* 取不到类型 = 本临时实现答不出，失败封闭而不是猜。 */
            return runtimec_temp_println_fail(
                output_exit, RUNTIMEC_STATUS_UNSUPPORTED,
                RUNTIMEC_TEMP_PRINTLN_DIAG_UNSUPPORTED_TYPE, source_counter);
        }
        const runtimec_status_t status =
            vmbin_value_access_locate_body(frame, argument, &location, &error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            /* **原样上报**：容量、形状、解析各有各的 status，不许一律抹成
             * VALUE_ACCESS——那样报出来的原因与真实成因无关。 */
            return runtimec_temp_println_fail(
                output_exit, status, RUNTIMEC_TEMP_PRINTLN_DIAG_VALUE_ACCESS,
                source_counter);
        }
        if (ordinal != 0U && separator[0] != '\0') {
            fputs(separator, stream);
        }
        /*
         * **打印一个指针值 = 打印它指向的 key 路径**，不查类型渲染表。
         *
         * `println(p)`（p 是 `*int64`）要的是**指针自己的值**，而指针的值就是
         * 目标路径文本（spec 03-类型系统/03-种类：Ptr 的 body 是目标键）。
         * 按 langtype 查渲染表必然查不到 `*int64`——那不是缺陷，是问错了表：
         * 指针不是标量，它没有「数值形态」可言。
         *
         * 判据用 `vmbin_value_access_form`（形态由 langtype 的 `*`/`@` 前缀派生），
         * 与解引用、ptreq 用的是同一条；我**不另判首字符**，那会是第二份规则。
         *
         * `argument->dereference != 0` 的不进这支：那种参数要的是跟过一跳之后的
         * 值，不是指针本身。实测 `println(*p)` 根本走不到这里——layout 先发一条
         * copy 把 `*p` 落进临时槽，println 拿到的是那个槽（所以 println 上的
         * dereference 位恒为 0，这是正常形态，不是断裂）。留着这个条件是因为
         * 「跟一跳后仍打印指针」在别的路径上仍可能成立，那时该走解引用语义。
         *
         * 空 body（空引用即 None）输出空串：一行空，不是空指针地址，也不是
         * 一个编造的 `None` 字面。
         */
        {
            uint8_t form = VMBIN_VALUE_ACCESS_FORM_VALUE;

            if (vmbin_value_access_form(frame, argument, &form, &error) ==
                    RUNTIMEC_STATUS_SUCCESS &&
                form == VMBIN_VALUE_ACCESS_FORM_POINTER &&
                argument->dereference == 0U) {
                if (location.size != 0U && location.read_bytes != NULL) {
                    fwrite(location.read_bytes, 1U, location.size, stream);
                }
                continue;
            }
        }
        switch (runtimec_temp_println_render(langtype, location.read_bytes,
                                             location.size, scratch,
                                             sizeof(scratch), stream)) {
            case RUNTIMEC_TEMP_PRINTLN_RENDER_OK:
                break;
            case RUNTIMEC_TEMP_PRINTLN_RENDER_UNSUPPORTED_TYPE:
                return runtimec_temp_println_fail(
                    output_exit, RUNTIMEC_STATUS_UNSUPPORTED,
                    RUNTIMEC_TEMP_PRINTLN_DIAG_UNSUPPORTED_TYPE, source_counter);
            default:
                /* 支持的类型但形状不符（标量宽度不对、非 UTF-32 边界）。 */
                return runtimec_temp_println_fail(
                    output_exit, RUNTIMEC_STATUS_ERROR,
                    RUNTIMEC_TEMP_PRINTLN_DIAG_MALFORMED_VALUE, source_counter);
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
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return runtimec_temp_println_run(frame, instruction, " ", 0, 1,
                                     output_exit);
}

static runtimec_status_t runtimec_temp_print_handler(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return runtimec_temp_println_run(frame, instruction, "", 0, 0,
                                     output_exit);
}

static runtimec_status_t runtimec_temp_cerr_handler(
    binrwir_binvthread_frame_t *frame,
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
