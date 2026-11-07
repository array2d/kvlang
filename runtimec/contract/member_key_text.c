/*
 * 组件：contract/member_key_text 的实现
 * 作用：值视图 → 成员键文本。完整契约见头注释。
 * 联系：`contract/langtype` 判形态，`contract/float_format` 供核心算法，
 *       `contract/char_codec` 供编码转换。
 * 约束：纯函数、不分配；缓冲不足报所需长度、绝不截断。
 *
 * ── 分派形状 ──────────────────────────────────────────────────────────
 *
 * `runtimec_langtype_form_t` 上的 `switch` **不带 `default`**：`-Wswitch` 因此
 * 替我们抓漏。将来新增形态时，这里编译报警——那正是我们要的。带 `default`
 * 会把「新形态」变成运行期静默，落进某个旧桶里。
 */

#include "contract/member_key_text.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "contract/char_codec.h"
#include "contract/float_format.h"
#include "contract/langtype.h"

/*
 * 键层的指数阈值，**独立于显示层**。
 *
 * 数值与 `float_format` 的 `FIXED_EXPONENT_{MIN,MAX}` 今天相同，但判据不同源：
 * 这里是 spec `05-runtime语义/03-成员访问.kv` 第 4 条「十进制指数 `< -4` 或
 * `>= 16` 时用指数」，那里是显示层的可读性选择。
 *
 * 不共享的理由是**方向**：共享常量意味着显示层改阈值会连带改键文本，
 * 而键文本是落在存储里的结构，用户能直接看到 `m·1e-05` 这样的键名。
 */
#define RUNTIMEC_MEMBER_KEY_TEXT_EXPONENT_FIXED_MIN (-4)
#define RUNTIMEC_MEMBER_KEY_TEXT_EXPONENT_FIXED_MAX 16

/* 整数文本的最坏情形：`-9223372036854775808` = 20 字符 + NUL。 */
#define RUNTIMEC_MEMBER_KEY_TEXT_INTEGER_CAPACITY 24U
/* 浮点指数形态最坏：`-1.2345678901234567e-308`，远小于此。 */
#define RUNTIMEC_MEMBER_KEY_TEXT_FLOAT_CAPACITY 40U

static runtimec_status_t runtimec_member_key_text_fail(
    runtimec_error_t *error, runtimec_status_t status,
    runtimec_member_key_text_diagnostic_t diagnostic, const char *message,
    char *output, size_t *output_length) {
    /* 失败不写 `output`：缓冲里可能还有旧内容，抹一半比不抹更难查。 */
    if (output_length != NULL) {
        *output_length = 0U;
    }
    if (error != NULL) {
        runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                           (int64_t)diagnostic,
                           runtimec_string_view_from_c_string(message),
                           RUNTIMEC_PROGRAM_COUNTER_NONE);
    }
    (void)output;
    return status;
}

/*
 * 把 NUL 结尾的文本送进调用方缓冲，遵守 `snprintf` 单位约定。
 *
 * `text_length` 不含 NUL；`capacity` 含 NUL ⇒ 需要 `text_length + 1`。
 * 放不下时报**所需文本长度**（不是所需容量），调用方据此 `+ 1` 重试。
 */
static runtimec_status_t runtimec_member_key_text_emit(
    const char *text, size_t text_length, char *output, size_t capacity,
    size_t *output_length, runtimec_error_t *error) {
    if (text_length + 1U > capacity) {
        if (output_length != NULL) {
            *output_length = text_length;
        }
        if (error != NULL) {
            runtimec_error_set(error, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY,
                               RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                               (int64_t)RUNTIMEC_MEMBER_KEY_TEXT_DIAG_INSUFFICIENT_CAPACITY,
                               runtimec_string_view_from_c_string(
                                   "member key buffer is too small"),
                               RUNTIMEC_PROGRAM_COUNTER_NONE);
        }
        return RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
    }
    memcpy(output, text, text_length);
    output[text_length] = '\0';
    *output_length = text_length;
    return RUNTIMEC_STATUS_SUCCESS;
}

/* 小端读一个至多 8 字节的无符号整数。 */
static uint64_t runtimec_member_key_text_load_unsigned(const uint8_t *body,
                                                        size_t width) {
    uint64_t value = 0U;

    for (size_t index = 0U; index < width; ++index) {
        value |= (uint64_t)body[index] << (8U * index);
    }
    return value;
}

/* 整数值的十进制文本。符号扩展按**声明宽度**做：`int8` 的 0xFF 是 -1。 */
static runtimec_status_t runtimec_member_key_text_integer(
    const runtimec_langtype_t *type, runtimec_bytes_view_t body,
    char *output, size_t capacity, size_t *output_length,
    runtimec_error_t *error) {
    char text[RUNTIMEC_MEMBER_KEY_TEXT_INTEGER_CAPACITY];
    const uint64_t raw = runtimec_member_key_text_load_unsigned(body.data,
                                                               body.size);
    int written;

    if (type->scalar == RUNTIMEC_LANGTYPE_SCALAR_SIGNED_INTEGER) {
        const unsigned shift = (unsigned)(64U - 8U * type->scalar_width);
        const int64_t value = (int64_t)(raw << shift) >> shift;

        written = snprintf(text, sizeof(text), "%lld", (long long)value);
    } else {
        written = snprintf(text, sizeof(text), "%llu",
                           (unsigned long long)raw);
    }
    if (written <= 0 || (size_t)written >= sizeof(text)) {
        return runtimec_member_key_text_fail(
            error, RUNTIMEC_STATUS_ERROR,
            RUNTIMEC_MEMBER_KEY_TEXT_DIAG_MALFORMED_BODY,
            "integer member key could not be formatted", output, output_length);
    }
    return runtimec_member_key_text_emit(text, (size_t)written, output, capacity,
                                         output_length, error);
}

/* 布尔：`true` / `false`（Go/Rust/JS/C 小写；Python 的 `True` 不取）。 */
static runtimec_status_t runtimec_member_key_text_boolean(
    runtimec_bytes_view_t body, char *output, size_t capacity,
    size_t *output_length, runtimec_error_t *error) {
    const char *text = body.data[0] != 0U ? "true" : "false";

    return runtimec_member_key_text_emit(text, strlen(text), output, capacity,
                                         output_length, error);
}

/*
 * 浮点：键层呈现规则。
 *
 * 共享核心算法（`runtimec_float_format_core`）拿到「有效数字 + 十进制指数 + 符号」，
 * 之后全部由**键层**决定：
 *
 *   - `-0.0` 归一成 `"0"`（键相等性不变量；显示层保留 `-0.0`）；
 *   - 整数值**不带 `.0`**（`1.0 → "1"`；显示层带 `.0`）；
 *   - 指数阈值 `< -4` 或 `>= 16`、补两位（两层同）；
 *   - ±Inf → `"inf"` / `"-inf"`，**调 `non_finite_text` 取串**，不自己拼字面量；
 *   - NaN → 具名错误（显示层输出 `nan`，这是三层里唯一「报错 vs 输出」的分叉）。
 */
static runtimec_status_t runtimec_member_key_text_float(
    const runtimec_langtype_t *type, runtimec_bytes_view_t body,
    char *output, size_t capacity, size_t *output_length,
    runtimec_error_t *error) {
    char digits[32];
    char text[RUNTIMEC_MEMBER_KEY_TEXT_FLOAT_CAPACITY];
    int exponent = 0;
    bool negative = false;
    double value = 0.0;
    size_t cursor = 0U;
    const runtimec_float_domain_t domain =
        type->scalar_width == 4U ? RUNTIMEC_FLOAT_DOMAIN_FLOAT32
                                 : RUNTIMEC_FLOAT_DOMAIN_FLOAT64;

    if (type->scalar_width == 4U) {
        float narrow = 0.0f;

        memcpy(&narrow, body.data, sizeof(narrow));
        /* float32 → double 是**保值**的（每个 float32 都能精确表示为 double），
         * 所以这里不引入额外舍入；往返域仍由 domain 决定。 */
        value = (double)narrow;
    } else {
        memcpy(&value, body.data, sizeof(value));
    }
    if (isnan(value)) {
        return runtimec_member_key_text_fail(
            error, RUNTIMEC_STATUS_INVALID,
            RUNTIMEC_MEMBER_KEY_TEXT_DIAG_NON_FINITE,
            "floating member key must not be NaN: NaN has no key text",
            output, output_length);
    }
    if (isinf(value)) {
        char infinite[RUNTIMEC_MEMBER_KEY_TEXT_FLOAT_CAPACITY];
        size_t length = 0U;

        if (!runtimec_float_format_non_finite_text(value, infinite,
                                                   sizeof(infinite), &length)) {
            return runtimec_member_key_text_fail(
                error, RUNTIMEC_STATUS_ERROR,
                RUNTIMEC_MEMBER_KEY_TEXT_DIAG_MALFORMED_BODY,
                "infinite member key could not be formatted", output,
                output_length);
        }
        return runtimec_member_key_text_emit(infinite, length, output, capacity,
                                             output_length, error);
    }
    if (!runtimec_float_format_core(value, domain, digits, sizeof(digits),
                                    &exponent, &negative)) {
        return runtimec_member_key_text_fail(
            error, RUNTIMEC_STATUS_ERROR,
            RUNTIMEC_MEMBER_KEY_TEXT_DIAG_MALFORMED_BODY,
            "floating member key shortest round-trip failed", output,
            output_length);
    }
    /*
     * 负零归一：尾数只有 `0` 且符号位为负 ⇒ 键文本按 `+0.0` 处理。
     *
     * 判据是**尾数串**（`digits` 全零），不是 `value == 0.0`——后者对 `-0.0`
     * 也为真，用它判会连带把 `-0.0` 的符号位丢掉，看起来对、实际是碰巧。
     */
    if (negative && strcmp(digits, "0") == 0) {
        negative = false;
    }
    if (negative) {
        text[cursor++] = '-';
    }
    if (exponent >= RUNTIMEC_MEMBER_KEY_TEXT_EXPONENT_FIXED_MIN &&
        exponent < RUNTIMEC_MEMBER_KEY_TEXT_EXPONENT_FIXED_MAX) {
        const size_t digit_count = strlen(digits);

        if (exponent >= 0) {
            const size_t integral = (size_t)exponent + 1U;

            for (size_t ordinal = 0U; ordinal < integral; ++ordinal) {
                text[cursor++] = ordinal < digit_count ? digits[ordinal] : '0';
            }
            if (digit_count > integral) {
                text[cursor++] = '.';
                memcpy(text + cursor, digits + integral, digit_count - integral);
                cursor += digit_count - integral;
            }
            /* 整数形态**不补 `.0`**：键层与显示层在这里分叉。 */
        } else {
            const size_t leading_zeros = (size_t)(-exponent) - 1U;

            text[cursor++] = '0';
            text[cursor++] = '.';
            for (size_t pad = 0U; pad < leading_zeros; ++pad) {
                text[cursor++] = '0';
            }
            memcpy(text + cursor, digits, digit_count);
            cursor += digit_count;
        }
    } else {
        const size_t digit_count = strlen(digits);
        int written;

        if (digit_count > 1U) {
            text[cursor++] = digits[0];
            text[cursor++] = '.';
            memcpy(text + cursor, digits + 1U, digit_count - 1U);
            cursor += digit_count - 1U;
        } else {
            text[cursor++] = digits[0];
        }
        written = snprintf(text + cursor, sizeof(text) - cursor, "e%c%02d",
                           exponent < 0 ? '-' : '+',
                           exponent < 0 ? -exponent : exponent);
        if (written <= 0 || (size_t)written >= sizeof(text) - cursor) {
            return runtimec_member_key_text_fail(
                error, RUNTIMEC_STATUS_ERROR,
                RUNTIMEC_MEMBER_KEY_TEXT_DIAG_MALFORMED_BODY,
                "floating member key exponent could not be formatted", output,
                output_length);
        }
        cursor += (size_t)written;
    }
    return runtimec_member_key_text_emit(text, cursor, output, capacity,
                                         output_length, error);
}

/*
 * 字符与字符串：utf8/ascii 原样，utf32 转 UTF-8（唯一转换点）。
 *
 * utf32 用**分块两遍**，不设任何定长上限：
 *
 *   第一遍：按 4 字节码点分块解码，只累加输出字节数，同时验证码点合法性；
 *   第二遍：缓冲够就再走一遍，直接写进调用方缓冲。
 *
 * 为什么不用一个大探针缓冲：`0x4501` 的教训就是定长缓冲把「太长」变成静默错。
 * 为什么不用「试写失败再判因」：`runtimec_char_utf32_to_utf8` 对「缓冲不足」与
 * 「码点非法」都返回 false，只看返回值分不出两者——而修法完全不同。
 */
#define RUNTIMEC_MEMBER_KEY_TEXT_CHUNK_BYTES 256U

static bool runtimec_member_key_text_utf32_walk(runtimec_bytes_view_t body,
                                                char *output,
                                                size_t output_capacity,
                                                size_t *output_length) {
    size_t offset = 0U;
    size_t total = 0U;

    if (body.size % 4U != 0U) {
        return false;
    }
    while (offset < body.size) {
        char chunk[RUNTIMEC_MEMBER_KEY_TEXT_CHUNK_BYTES + 1U];
        size_t take = body.size - offset;
        size_t produced = 0U;

        if (take > RUNTIMEC_MEMBER_KEY_TEXT_CHUNK_BYTES) {
            take = RUNTIMEC_MEMBER_KEY_TEXT_CHUNK_BYTES;
        }
        take -= take % 4U;
        /* `char_codec` 约定 `capacity > 输出长度`，故传 `sizeof(chunk)`（多留一字节）。 */
        if (!runtimec_char_utf32_to_utf8(body.data + offset, take, chunk,
                                         sizeof(chunk), &produced)) {
            return false;
        }
        if (output != NULL) {
            if (total + produced > output_capacity) {
                return false;
            }
            memcpy(output + total, chunk, produced);
        }
        total += produced;
        offset += take;
    }
    *output_length = total;
    return true;
}

static runtimec_status_t runtimec_member_key_text_text(
    const runtimec_langtype_t *type, runtimec_bytes_view_t body,
    char *output, size_t capacity, size_t *output_length,
    runtimec_error_t *error) {
    if (type->char_encoding == RUNTIMEC_LANGTYPE_CHAR_ENCODING_UTF32) {
        size_t required = 0U;

        if (!runtimec_member_key_text_utf32_walk(body, NULL, 0U, &required)) {
            return runtimec_member_key_text_fail(
                error, RUNTIMEC_STATUS_INVALID,
                RUNTIMEC_MEMBER_KEY_TEXT_DIAG_MALFORMED_BODY,
                "char/utf32 body holds an invalid code point or a partial "
                "code point",
                output, output_length);
        }
        if (required + 1U > capacity) {
            if (output_length != NULL) {
                *output_length = required;
            }
            if (error != NULL) {
                runtimec_error_set(
                    error, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY,
                    RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                    (int64_t)RUNTIMEC_MEMBER_KEY_TEXT_DIAG_INSUFFICIENT_CAPACITY,
                    runtimec_string_view_from_c_string(
                        "member key buffer is too small"),
                    RUNTIMEC_PROGRAM_COUNTER_NONE);
            }
            return RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY;
        }
        {
            size_t written = 0U;

            if (!runtimec_member_key_text_utf32_walk(body, output, capacity,
                                                     &written)) {
                return runtimec_member_key_text_fail(
                    error, RUNTIMEC_STATUS_ERROR,
                    RUNTIMEC_MEMBER_KEY_TEXT_DIAG_MALFORMED_BODY,
                    "char/utf32 body changed between the two conversion "
                    "passes",
                    output, output_length);
            }
            output[written] = '\0';
            *output_length = written;
        }
        return RUNTIMEC_STATUS_SUCCESS;
    }
    /* utf8 / ascii：body 就是键文本，**含空 body**（空串是合法键，不开特例分支）。 */
    return runtimec_member_key_text_emit((const char *)body.data, body.size,
                                         output, capacity, output_length, error);
}

runtimec_status_t runtimec_member_key_text(runtimec_string_view_t langtype,
                                           runtimec_bytes_view_t body,
                                           char *output, size_t capacity,
                                           size_t *output_length,
                                           runtimec_error_t *error) {
    runtimec_langtype_t type;
    runtimec_status_t status;

    if (output_length != NULL) {
        *output_length = 0U;
    }
    if (error != NULL) {
        runtimec_error_reset(error);
    }
    if (output == NULL || output_length == NULL || !runtimec_string_view_is_valid(langtype) ||
        !runtimec_bytes_view_is_valid(body)) {
        return runtimec_member_key_text_fail(
            error, RUNTIMEC_STATUS_INVALID,
            RUNTIMEC_MEMBER_KEY_TEXT_DIAG_INVALID_REQUEST,
            "member key request is malformed", output, output_length);
    }
    /*
     * **旧 map 语法 `[K]·V` 单独指路。**
     *
     * `runtimec_langtype_parse` 对含 `·` 的串返回 INVALID（它认为那是畸形），
     * 但「畸形」与「旧语法」是两件事：前者说不出怎么改，后者能。所以在调
     * parse 之前先判一次，给出带写法的消息。
     *
     * 判据按完整 UTF-8 序列比（`·` = `0xC2 0xB7`）：单字节找会误命中别的字符
     * 的尾字节。
     */
    {
        for (size_t scan = 0U; scan + 1U < langtype.size; ++scan) {
            if ((uint8_t)langtype.data[scan] == 0xC2U &&
                (uint8_t)langtype.data[scan + 1U] == 0xB7U) {
                return runtimec_member_key_text_fail(
                    error, RUNTIMEC_STATUS_UNSUPPORTED,
                    RUNTIMEC_MEMBER_KEY_TEXT_DIAG_UNSUPPORTED_LANGTYPE,
                    "legacy map syntax '[K]·V' is not accepted; write 'K'V "
                    "instead (issue #631)",
                    output, output_length);
            }
        }
    }
    status = runtimec_langtype_parse(langtype, &type);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return runtimec_member_key_text_fail(
            error, RUNTIMEC_STATUS_INVALID,
            RUNTIMEC_MEMBER_KEY_TEXT_DIAG_INVALID_REQUEST,
            "member key langtype is malformed", output, output_length);
    }
    /*
     * 四条拒绝路径的**消息各自不同**：它们指向四种不同的修法，合成一条会让人
     * 按错方向查。switch 不带 `default`（见文件头）。
     */
    switch (type.form) {
    case RUNTIMEC_LANGTYPE_FORM_TUPLE:
        return runtimec_member_key_text_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED,
            RUNTIMEC_MEMBER_KEY_TEXT_DIAG_UNSUPPORTED_LANGTYPE,
            "tuple keys have no value form: their text is produced by the "
            "parser from source and never converted from a body",
            output, output_length);
    case RUNTIMEC_LANGTYPE_FORM_ARRAY: {
        /*
         * `[N]char/X` 是**字符序列的定长存储形态**：字符串字面量的声明类型就是它
         * （`"键"` → `[1]char/utf32`）。能不能作成员键，判据是**基种类**，不是 form
         * ——form 说的是存储形状，不是值的语义种类。
         *
         * 只接受**单维**字符序列：`[][]char/X` 是「字符序列的序列」，不是字符串。
         * `[N]int32` / `[]int32` 仍然拒绝——那段 body 会被静默读成单个标量，
         * 长度恰好匹配时产出的错键不会有任何报错。
         */
        runtimec_langtype_t base_type;
        if (type.dimension_count == 1 &&
            runtimec_langtype_parse(type.base, &base_type) ==
                RUNTIMEC_STATUS_SUCCESS &&
            base_type.form == RUNTIMEC_LANGTYPE_FORM_CHARACTER) {
            return runtimec_member_key_text_text(&base_type, body, output,
                                                 capacity, output_length,
                                                 error);
        }
        return runtimec_member_key_text_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED,
            RUNTIMEC_MEMBER_KEY_TEXT_DIAG_UNSUPPORTED_LANGTYPE,
            "a shaped langtype is a value form, not a member key form",
            output, output_length);
    }
    case RUNTIMEC_LANGTYPE_FORM_MAPPING:
        /*
         * **map 类型不是成员键的形态。**
         *
         * 与 ARRAY 那条不同，修法也不同：ARRAY 是「有值形态但没有可作键的文本」，
         * 而 map **整个类型就不描述一个可格式化的值**——它的 formatter 在引号里，
         * 值侧是另一个 map/值，没有任何 body 该被格式化成一个路径段。
         *
         * 消息不复用 ARRAY 那条：两条的成因不同，合成一条会让人按错方向查。
         */
        return runtimec_member_key_text_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED,
            RUNTIMEC_MEMBER_KEY_TEXT_DIAG_UNSUPPORTED_LANGTYPE,
            "a map type is not a member key form: its formatter lives inside the "
            "quotes and its value side has no body to format",
            output, output_length);
    case RUNTIMEC_LANGTYPE_FORM_UNRECOGNIZED:
        return runtimec_member_key_text_fail(
            error, RUNTIMEC_STATUS_UNSUPPORTED,
            RUNTIMEC_MEMBER_KEY_TEXT_DIAG_UNSUPPORTED_LANGTYPE,
            "langtype cannot be a member key", output, output_length);
    case RUNTIMEC_LANGTYPE_FORM_CHARACTER:
    case RUNTIMEC_LANGTYPE_FORM_STRING:
        /*
         * 字符与字符串**没有「应有的宽度」**：任何长度（含 0）都是合法值。
         * 所以这里不判长度——空串走同一条路径，不另开分支。
         */
        return runtimec_member_key_text_text(&type, body, output, capacity,
                                             output_length, error);
    case RUNTIMEC_LANGTYPE_FORM_SCALAR:
        break;
    }
    if (body.size != type.scalar_width) {
        return runtimec_member_key_text_fail(
            error, RUNTIMEC_STATUS_INVALID,
            RUNTIMEC_MEMBER_KEY_TEXT_DIAG_MALFORMED_BODY,
            "scalar member key body width does not match its langtype", output,
            output_length);
    }
    switch (type.scalar) {
    case RUNTIMEC_LANGTYPE_SCALAR_BOOLEAN:
        return runtimec_member_key_text_boolean(body, output, capacity,
                                                output_length, error);
    case RUNTIMEC_LANGTYPE_SCALAR_SIGNED_INTEGER:
    case RUNTIMEC_LANGTYPE_SCALAR_UNSIGNED_INTEGER:
        return runtimec_member_key_text_integer(&type, body, output, capacity,
                                                output_length, error);
    case RUNTIMEC_LANGTYPE_SCALAR_FLOAT:
        return runtimec_member_key_text_float(&type, body, output, capacity,
                                              output_length, error);
    case RUNTIMEC_LANGTYPE_SCALAR_NONE:
        break;
    }
    return runtimec_member_key_text_fail(
        error, RUNTIMEC_STATUS_UNSUPPORTED,
        RUNTIMEC_MEMBER_KEY_TEXT_DIAG_UNSUPPORTED_LANGTYPE,
        "langtype cannot be a member key", output, output_length);
}
