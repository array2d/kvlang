/*
 * 组件：vmbin/handlers/string 的实现
 * 作用：字符串算子——长度、取字符、比较、拼接。
 * 联系：条目由 session 装进 `binrwir_registry_t`；值经 `vmbin/value_access`
 *       按地址读写，与 core 规则共用同一条数据面。
 * 约束：
 *   - **不按 langtype 分叉**：三种编码（utf8/utf32/ascii）先统一解码成**码点序列**
 *     （`uint32_t` 数组），之后的所有位置算术、比较、拼接走的都是同一段代码。
 *     编码差异只出现在「解码」与「编码」两个边界函数里。
 *   - **不写第二份编解码**：转换一律调 `contract/char_codec`。
 *   - 变长结果走 `kvspaceSetValue`；定长缓冲超限用既有细节码。
 *
 * 本批实现（opid / 名字 / catalog 行）：
 *   0x0125 string·len       rwir_catalog.def:541  string-codepoint-indexing
 *   0x0121 string·char      rwir_catalog.def:513  string-codepoint-indexing
 *   0x0123 string·cmp       rwir_catalog.def:527  string-compare-ordering
 *   0x0127 string·concat    rwir_catalog.def:555  string-concat-result-kind
 */

#include "vmbin/handlers/string.h"

#include <stdlib.h>
#include <string.h>

#include "binrwir/execution_exit.h"
#include "binrwir/execution_frame.h"
#include "binrwir/instruction.h"
#include "binrwir/operation_identifiers.h"
#include "contract/char_codec.h"
#include "contract/error.h"
#include "contract/status.h"
#include "contract/view.h"
#include "kvspace/kvspace.h"
#include "kvspace/xvalue_head.h"
#include "vmbin/handlers/container.h"
#include "vmbin/frame_target_key.h"
#include "vmbin/handlers/handler_call.h"
#include "vmbin/handlers/handler_call_text.h"
#include "vmbin/value_access.h"

#define STRING_KEY_CAPACITY 512U


/*
 * 按编码规定的**单位**数出长度。
 *
 * spec（`03-类型系统/23-langtype语义类型.kv:12`）：「UTF-8 的 len 为字节数，
 * N 为码点数」。所以三种编码的单位不同：
 *   char/utf8   字节数           （Go `len(s)` / Rust `s.len()` 阵营）
 *   char/utf32  码点数 = 字节/4
 *   char/ascii  字节数（= 码点数，ASCII 恒 1 字节/码点）
 *
 * **这是唯一结果依赖操作数编码的字符串算子**，所以它不解码——答案就在 body 长度里。
 * 其余算子解码后统一处理，那些不受影响。
 */
static void string_length_of(runtimec_string_view_t langtype, size_t body_size,
                             int64_t *out,
                             binrwir_execution_exit_t *output_exit) {
    static const struct {
        const char *text;
        int is_codepoint_count;
    } table[] = {
        {"char/utf8", 0}, {"char/ascii", 0}, {"char/utf32", 1},
    };
    size_t offset = 0U;

    while (offset < langtype.size && langtype.data[offset] == '[') {
        const char *closing =
            memchr(langtype.data + offset, ']', langtype.size - offset);

        if (closing == NULL) {
            break;
        }
        offset = (size_t)(closing - langtype.data) + 1U;
    }
    for (size_t ordinal = 0U; ordinal < sizeof(table) / sizeof(table[0]);
         ++ordinal) {
        const size_t size = strlen(table[ordinal].text);

        if (langtype.size - offset == size &&
            memcmp(langtype.data + offset, table[ordinal].text, size) == 0) {
            *out = table[ordinal].is_codepoint_count != 0
                       ? (int64_t)(body_size / 4U)
                       : (int64_t)body_size;
            return;
        }
    }
    vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_UNSUPPORTED,
                       VMBIN_HANDLER_CALL_TEXT_DIAG_UNSUPPORTED_ENCODING,
                       output_exit->logical_program_counter);
}

/* 逻辑函数：一句话——按编码规定的单位数出长度。 */
static void string_length_build(const vmbin_value_location_t *text,
                                runtimec_string_view_t langtype, int64_t *out,
                                binrwir_execution_exit_t *output_exit) {
    string_length_of(langtype, text->size, out, output_exit);
}


/*
 * 第 i 个码点，结果是一个**单字符字符串**——不是整数（行为矩阵
 * string-codepoint-indexing 的正向例子就是「返回首个码点作为一字符字符串」）。
 *
 * **成功才写缓冲与长度，失败只写 `exit`**。越界按码点数判，与编码无关。
 */
static void string_character_of(const string_text_t *text, const int64_t *index,
                                runtimec_string_view_t write_langtype,
                                uint8_t *buffer, size_t capacity,
                                size_t *output_length,
                                binrwir_execution_exit_t *output_exit) {
    string_text_t single;

    *output_length = 0U;
    if (*index < 0 || (uint64_t)*index >= text->count) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    single.count = 1U;
    single.codepoints[0] = text->codepoints[*index];
    /*
     * **按写槽声明的 langtype 编码**，不是恒 UTF-8。
     *
     * 结果是一个「单字符字符串」，它的字节形态必须服从槽的类型：`char/utf32`
     * 槽里放 `'L'` 必须是 4 字节 `4C 00 00 00`，否则下游按 utf32 解时
     * `size % 4 != 0` 直接判畸形（这正是 0x4C08「两侧 1 vs 4 字节」的根因）。
     * 而 `char/ascii` 装不下的码点要**失败**，不是截断。
     */
    const runtimec_status_t encoded = string_text_encode(
        write_langtype, &single, buffer, capacity, output_length);
    if (encoded != RUNTIMEC_STATUS_SUCCESS) {
        *output_length = 0U;
        vmbin_handler_fail(output_exit,
                           encoded == RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY
                               ? RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY
                               : RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
    }
}

/* 逻辑函数：一句话——取出第 i 个码点作为单字符字符串。 */
static inline void string_character_build(const string_text_t *text,
                                          const int64_t *index,
                                          runtimec_string_view_t write_langtype,
                                          uint8_t *buffer, size_t capacity,
                                          size_t *output_length,
                                          binrwir_execution_exit_t *output_exit) {
    string_character_of(text, index, write_langtype, buffer, capacity,
                        output_length, output_exit);
}

VMBIN_HANDLER_TEXT_INDEX_CREATE(string_character_handler, string_character_build,
                                int64_t)


/* 按码点逐位比较：公共前缀相同则短的更小——各语言字典序的同一套规则。 */
static int64_t string_compare_codepoints(const string_text_t *left,
                                         const string_text_t *right) {
    const size_t shared = left->count < right->count ? left->count : right->count;
    size_t ordinal = 0U;

    for (; ordinal < shared; ++ordinal) {
        if (left->codepoints[ordinal] != right->codepoints[ordinal]) {
            return left->codepoints[ordinal] < right->codepoints[ordinal] ? -1 : 1;
        }
    }
    if (left->count == right->count) {
        return 0;
    }
    return left->count < right->count ? -1 : 1;
}

/* 逻辑函数：一句话——按码点比较两段文本。 */
static inline void string_compare_build(const string_text_t *left,
                                        const string_text_t *right,
                                        int64_t *out) {
    *out = string_compare_codepoints(left, right);
}

VMBIN_HANDLER_BINARY_TEXT_SCALAR(string_compare_handler, string_compare_build,
                                 int64_t)

/* 拼接结果：Buffer 与长度一起传递，免得每层各开一份同尺寸栈缓冲。 */
typedef struct runtimec_string_concat_result {
    uint8_t bytes[VMBIN_HANDLER_CREATE_BUFFER_CAPACITY];
    size_t length;
} runtimec_string_concat_result_t;

/*
 * 两侧文本拼接并编码 —— **逻辑函数只做这一件**：接起来、按共同编码写出。
 *
 * 编码不由它决定（那是 `string_text_common_encoding` 的事），它只拿结果。
 * 容量不足与码点超限各自具名失败，不截断。
 */
static void string_concat_of(const string_text_t *left,
                             const string_text_t *right,
                             runtimec_string_view_t common, uint8_t *buffer,
                             size_t capacity, size_t *output_length,
                             binrwir_execution_exit_t *output_exit) {
    string_text_t joined;

    *output_length = 0U;
    if (left->count + right->count > STRING_TEXT_CODEPOINT_CAPACITY) {
        /*
         * **不报 `LENGTH_MISMATCH`**：那条说「值体宽度与要求的宽度不符」，
         * 用户会去改自己的数据宽度；而真正的原因是拼接结果超过运行时定长缓冲。
         * 也不报 `INSUFFICIENT_CAPACITY`（可重试）——这个上限换多久都跑不了。
         */
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_OUT_OF_MEMORY,
                           VMBIN_HANDLER_CALL_DIAG_STRING_TOO_LONG,
                           output_exit->logical_program_counter);
        return;
    }
    joined.count = left->count + right->count;
    memcpy(joined.codepoints, left->codepoints, left->count * sizeof(uint32_t));
    memcpy(joined.codepoints + left->count, right->codepoints,
           right->count * sizeof(uint32_t));
    if (string_text_encode(common, &joined, buffer, capacity, output_length) !=
        RUNTIMEC_STATUS_SUCCESS) {
        *output_length = 0U;
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
    }
}



/*
 * `string·concat` 的完整流程：arity → 共同编码 → 解码两侧 → 拼起来编码 → 写回。
 *
 * 每一段的判据都在別处（`string_text_common_encoding` 判编码、`string_text_decode_argument`
 * 解码、`string_text_encode` 编码、`write_new_value` 写回），本函数只把它们按
 * 顺序串起来——**不重新判断任何一件事**。
 */
static runtimec_status_t string_concat_pipeline(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    runtimec_string_concat_result_t *output, binrwir_execution_exit_t *output_exit) {
    runtimec_string_view_t left_langtype;
    runtimec_string_view_t right_langtype;
    runtimec_string_view_t common;
    string_text_t left_text;
    string_text_t right_text;
    runtimec_error_t error;
    runtimec_status_t status;

    runtimec_error_reset(&error);
    status = vmbin_value_access_langtype(
        frame, &instruction->read_arguments.data[0], &left_langtype, &error);
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = vmbin_value_access_langtype(
            frame, &instruction->read_arguments.data[1], &right_langtype, &error);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = string_text_common_encoding(left_langtype, right_langtype,
                                             &common);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = string_text_decode_argument(
            frame, &instruction->read_arguments.data[0], &left_text, &error);
    }
    if (status == RUNTIMEC_STATUS_SUCCESS) {
        status = string_text_decode_argument(
            frame, &instruction->read_arguments.data[1], &right_text, &error);
    }
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_UNSUPPORTED_LANGTYPE,
                           instruction->source_logical_program_counter);
        return RUNTIMEC_STATUS_ERROR;
    }
    string_concat_of(&left_text, &right_text, common, output->bytes,
                     sizeof(output->bytes), &output->length, output_exit);
    if (output_exit->kind != BINRWIR_EXECUTION_EXIT_NONE) {
        return RUNTIMEC_STATUS_ERROR;
    }
    return vmbin_handler_write_new_value(frame, instruction, output->bytes,
                                         output->length, output_exit);
}

/*
 * `string·concat` handler：两段文本接起来，按**操作数的共同编码**写出。
 *
 * ── 结果编码跟随操作数（矩阵 `character-concat-kind`，2026-11-06 裁决）──
 *
 * 两个 utf32 ⇒ utf32、两个 utf8 ⇒ utf8、ascii 提升到另一侧、utf8 + utf32 ⇒ 失败。
 * 判据在 `string_text_common_encoding`（编码层知识），本文件只拿结果。
 *
 * **为什么不再恒 utf8**：字面量默认 utf32、concat 恒产 utf8，两者相撞会让同一个
 * 表达式在类型推导里出现 `[]char/utf32|char/utf8` 的并集——根源就是这里把操作数
 * 的编码抹掉了。
 *
 * ── 为什么展开了 `VMBIN_HANDLER_BINARY_TEXT_CREATE` ──────────────────
 *
 * 那个宏**已经取好两侧 langtype**却不传给逻辑函数，而共同编码判定恰恰要它们。
 * 给一个只有本算子使用的宏加参数，会让它变成「为唯一用户定制的通用宏」。展开时
 * **逐段保留了宏原来的行为**：arity 校验、两侧定位、两侧取类型、两侧解码、错误
 * 出口、以及最后的 `write_new_value`。
 */
/* 逻辑主体：arity → 共同编码 → 解码 → 拼接编码 → 写回。 */
static inline runtimec_status_t string_concat_execute(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    runtimec_string_concat_result_t result;

    VMBIN_HANDLER_REQUIRE_ARITY(instruction, 2U, 1U, output_exit);
    return string_concat_pipeline(frame, instruction, &result, output_exit);
}

/*
 * 注册用的薄封装：主体是 `static inline`，注册表要的是函数地址，两件事各自
 * 独立；薄封装只在**本文件**定义一次，注册表里因此只有一份地址。
 */
static runtimec_status_t string_concat_handler(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    return string_concat_execute(frame, instruction, output_exit);
}

/*
 * 注册用的薄封装：主体是 `static inline`，注册表要的是函数地址，两件事各自
 * 独立；薄封装只在**本文件**定义一次，注册表里因此只有一份地址。
 */
VMBIN_HANDLER_UNARY_TEXT_SCALAR(string_length_handler, string_length_build,
                                int64_t)




/*
 * 同槽 patch：`char/utf32` 上换一个码点**长度不变**（恒 4 字节），可以直接改那一段。
 * **成功不写哨兵，失败只写 `exit`**。
 */
static void string_set_in_place(const string_text_t *text,
                                const vmbin_value_location_t *write_location,
                                const int64_t *position, const string_text_t *replacement,
                                binrwir_execution_exit_t *output_exit) {
    size_t byte_offset = 0U;

    if (*position < 0 || (uint64_t)*position >= text->count ||
        replacement->count != 1U) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    /* utf32：每码点 4 字节，偏移就是 position × 4。 */
    byte_offset = (size_t)*position * 4U;
    if (byte_offset + 4U > write_location->size) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    {
        const uint32_t codepoint = replacement->codepoints[0];

        write_location->write_bytes[byte_offset] = (uint8_t)(codepoint & 0xFFU);
        write_location->write_bytes[byte_offset + 1U] =
            (uint8_t)((codepoint >> 8U) & 0xFFU);
        write_location->write_bytes[byte_offset + 2U] =
            (uint8_t)((codepoint >> 16U) & 0xFFU);
        write_location->write_bytes[byte_offset + 3U] =
            (uint8_t)((codepoint >> 24U) & 0xFFU);
    }
}

/*
 * 重建整串：`char/utf8` 与 `char/ascii` 上一个码点的字节数不固定，
 * 换码点会**改变 body 长度**——那是存储层的事，必须造新 wire。
 * **成功才写缓冲与长度，失败只写 `exit`**。
 */
static void string_set_rebuild(const string_text_t *text,
                               const int64_t *position,
                               const string_text_t *replacement,
                               runtimec_string_view_t write_langtype,
                               uint8_t *buffer, size_t capacity,
                               size_t *output_length,
                               binrwir_execution_exit_t *output_exit) {
    string_text_t rebuilt;

    *output_length = 0U;
    if (*position < 0 || (uint64_t)*position >= text->count ||
        replacement->count != 1U) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    rebuilt = *text;
    rebuilt.codepoints[*position] = replacement->codepoints[0];
    /* **按写槽声明的 langtype 编码**：这条分支只在源串非定宽（utf8/ascii）时走，
     * 而写槽可能声明成 `char/utf32`——恒写 utf8 就会把 1 字节塞进 4 字节槽，
     * 下游判「不是 4 的倍数」直接畸形。 */
    if (string_text_encode(write_langtype, &rebuilt, buffer, capacity,
                           output_length) != RUNTIMEC_STATUS_SUCCESS) {
        *output_length = 0U;
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
    }
}

/* 逻辑函数（patch 路径）：一句话——就地把第 i 个码点换掉。 */
static inline void string_set_patch_build(
    const string_text_t *text, const vmbin_value_location_t *write_location,
    const int64_t *position, const string_text_t *replacement,
    binrwir_execution_exit_t *output_exit) {
    string_set_in_place(text, write_location, position, replacement, output_exit);
}

/* 逻辑函数（重建路径）：一句话——重建整串并给出新字节。 */
static inline void string_set_rebuild_build(
    const string_text_t *text, const int64_t *position,
    const string_text_t *replacement, runtimec_string_view_t write_langtype,
    uint8_t *buffer, size_t capacity, size_t *output_length,
    binrwir_execution_exit_t *output_exit) {
    string_set_rebuild(text, position, replacement, write_langtype, buffer,
                       capacity, output_length, output_exit);
}


/*
 * 首码点；解不出来或空串时**置 exit 并返回 0**（调用方据 `exit.kind` 判成败）。
 *
 * 空串没有首码点：**失败封闭**，不返回一个看起来像码点的哨兵。
 */
static int64_t string_first_codepoint(const vmbin_value_location_t *text,
                                      runtimec_string_view_t langtype,
                                      binrwir_execution_exit_t *output_exit) {
    string_text_t decoded;

    if (string_text_decode(
            runtimec_bytes_view_make(text->read_bytes, text->size), langtype,
            &decoded) != RUNTIMEC_STATUS_SUCCESS ||
        decoded.count == 0U) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return 0;
    }
    return (int64_t)decoded.codepoints[0];
}

/*
 * `string·ord(s)`：**单读参**，取首码点的数值（不是「第 i 个」）。
 *
 * 契约以前是错的：它复用了 `VMBIN_HANDLER_TEXT_INDEX_SCALAR`，那个宏硬要求
 * **2 读 1 写**——而 `string·ord` 只有一个字符串实参。于是每个正确调用都在
 * 运行期报 `0x4A01 WRONG_ARITY`，把一条**实现错**伪装成**用例错**。
 *
 * 裁决依据是参考实现 `runtime/src/rwir_string.c:181` 的 `kvlangBuiltinStringOrd`：
 * 只取 `in[0]`、返回首码点；带下标的那个是 `string·char(s, i)`，不是这个。
 *
 * 逻辑函数签名用 `_UNARY_TEXT_SCALAR` 那一族（文本 + 语言类型 → 标量）：
 * **自己解码**，不借 `string_ordinal_of` 的「按下标取」，因为这里没有下标。
 */
static void string_ordinal_build(const vmbin_value_location_t *text,
                                 runtimec_string_view_t langtype, int64_t *out,
                                 binrwir_execution_exit_t *output_exit) {
    *out = string_first_codepoint(text, langtype, output_exit);
}

VMBIN_HANDLER_UNARY_TEXT_SCALAR(string_ordinal_handler, string_ordinal_build,
                                int64_t)

/* 在 haystack 里找 needle 首次出现的**码点下标**；找不到 -1，空 needle 返回 0。 */
static int64_t string_find_in(const string_text_t *haystack,
                              const string_text_t *needle) {
    size_t start = 0U;

    if (needle->count == 0U) {
        return 0;
    }
    for (; start + needle->count <= haystack->count; ++start) {
        size_t matched = 0U;

        while (matched < needle->count &&
               haystack->codepoints[start + matched] ==
                   needle->codepoints[matched]) {
            ++matched;
        }
        if (matched == needle->count) {
            return (int64_t)start;
        }
    }
    return -1;
}

/* 逻辑函数：一句话——找 needle 在 haystack 里首次出现的位置。 */
static inline void string_find_build(const string_text_t *haystack,
                                     const string_text_t *needle,
                                     int64_t *out) {
    *out = string_find_in(haystack, needle);
}

VMBIN_HANDLER_BINARY_TEXT_SCALAR(string_find_handler, string_find_build, int64_t)

/*
 * 按**码点**取半开区间 `[lo, hi)` 并编码成 UTF-8。位置越界或 `lo > hi` 失败
 * （行为矩阵 string-slice-bounds）。**成功才写缓冲与长度，失败只写 `exit`**。
 *
 * 结果是**码点的一段**，不是字节的一段——`"héllo"[1:3]` 取的是两个码点，
 * 字节边界由编码决定。
 */
static void string_slice_of(const string_text_t *text,
                            const vmbin_value_location_t *bounds,
                            runtimec_string_view_t write_langtype,
                            uint8_t *buffer, size_t capacity,
                            size_t *output_length,
                            binrwir_execution_exit_t *output_exit) {
    int64_t lo = 0;
    int64_t hi = 0;
    string_text_t sliced;

    *output_length = 0U;
    if (bounds[0].size != sizeof(int64_t) || bounds[1].size != sizeof(int64_t)) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    memcpy(&lo, bounds[0].read_bytes, sizeof(lo));
    memcpy(&hi, bounds[1].read_bytes, sizeof(hi));
    if (lo < 0 || hi < lo || (uint64_t)hi > text->count) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    sliced.count = (size_t)(hi - lo);
    memcpy(sliced.codepoints, text->codepoints + lo,
           sliced.count * sizeof(uint32_t));
    if (string_text_encode(write_langtype, &sliced, buffer, capacity,
                           output_length) != RUNTIMEC_STATUS_SUCCESS) {
        *output_length = 0U;
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
    }
}

/* 逻辑函数：一句话——按码点切一段并编码。 */
static inline void string_slice_build(const string_text_t *text,
                                      const vmbin_value_location_t *bounds,
                                      runtimec_string_view_t write_langtype,
                                      uint8_t *buffer, size_t capacity,
                                      size_t *output_length,
                                      binrwir_execution_exit_t *output_exit) {
    string_slice_of(text, bounds, write_langtype, buffer, capacity,
                    output_length, output_exit);
}


/*
 * 整数 → 文本：base 缺省 10，10 以上用**小写**字母（行为矩阵
 * string-format-base-radix）。负号只对带符号版本生效。
 *
 * 写法：先把各位数字**从低位到高位**写进 `digits`，再倒序输出到码点序列。
 * 负号在倒序输出时先写——**不用占位符**：占位符要额外判断「这格是不是占位」，
 * 那是自找的分支。
 *
 * **成功才写缓冲与长度，失败只写 `exit`**。`magnitude` 按无符号算，
 * 避免 `INT64_MIN` 取绝对值时溢出。
 */
static void string_format_of(const vmbin_value_location_t *value_location,
                             int64_t base, int is_signed,
                             runtimec_string_view_t write_langtype,
                             uint8_t *buffer, size_t capacity,
                             size_t *output_length,
                             binrwir_execution_exit_t *output_exit) {
    char digits[80];
    size_t digit_count = 0U;
    uint64_t magnitude;
    int64_t value = 0;
    int negative;
    string_text_t text;

    *output_length = 0U;
    if (value_location->size != sizeof(int64_t) || base < 2 || base > 36) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    memcpy(&value, value_location->read_bytes, sizeof(value));
    negative = (is_signed != 0 && value < 0) ? 1 : 0;
    /* 取绝对值按无符号算：`-(INT64_MIN + 1) + 1` 不会溢出。 */
    magnitude = negative != 0 ? (uint64_t)(-(value + 1)) + 1U : (uint64_t)value;
    do {
        const uint64_t remainder = magnitude % (uint64_t)base;

        digits[digit_count++] = remainder < 10U
                                    ? (char)('0' + remainder)
                                    : (char)('a' + (remainder - 10U));
        magnitude /= (uint64_t)base;
    } while (magnitude != 0U && digit_count < sizeof(digits));
    text.count = 0U;
    if (negative != 0) {
        text.codepoints[text.count++] = (uint32_t)'-';
    }
    while (digit_count > 0U) {
        text.codepoints[text.count++] = (uint32_t)(uint8_t)digits[--digit_count];
    }
    /* **按写槽声明的 langtype 编码**：十进制码点全 <= 0x7F 只说明内容窄，
     * 不说明字节宽度——`char/utf32` 槽要 4 字节/字符。 */
    if (string_text_encode(write_langtype, &text, buffer, capacity,
                           output_length) != RUNTIMEC_STATUS_SUCCESS) {
        *output_length = 0U;
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
    }
}

/* 逻辑函数：一句话——把一个整数按进制写成文本。 */
static inline void string_format_build(const vmbin_value_location_t *value,
                                       int64_t base, int is_signed,
                                       runtimec_string_view_t write_langtype,
                                       uint8_t *buffer, size_t capacity,
                                       size_t *output_length,
                                       binrwir_execution_exit_t *output_exit) {
    string_format_of(value, base, is_signed, write_langtype, buffer, capacity,
                     output_length, output_exit);
}


/*
 * 严格全消费的数字解析：**整串必须吃完**，否则失败。
 *
 * 行为矩阵 `string-parse-strict-full-consumption`（Python / Rust / Go 阵营）：
 * `strtol` 那种「停在第一个非法字符」正是这条决策明确否掉的语义——`"12x"` 必须
 * 失败而不是解析出 12。判据是 `*end == '\0'` 且**确实消费了内容**（`end != 开头`），
 * 两条缺一不可：`""` 与 `"x"` 都不能成功。
 *
 * `is_signed == 0` 时额外拒绝前导 `-`：`strtoull` 接受它并回绕，那不是「无符号解析」。
 *
 * **成功才写 `*out`，失败只写 `exit`**。
 */
static void string_parse_number_of(const string_text_t *text, int allow_float,
                                   int is_signed, uint8_t *output,
                                   size_t output_size,
                                   binrwir_execution_exit_t *output_exit) {
    char ascii[STRING_TEXT_CODEPOINT_CAPACITY + 1U];
    size_t ordinal = 0U;

    for (; ordinal < text->count; ++ordinal) {
        /* 只接受 ASCII：非 ASCII 码点一律不是数字。 */
        if (text->codepoints[ordinal] > 0x7FU) {
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                               VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                               output_exit->logical_program_counter);
            return;
        }
        ascii[ordinal] = (char)(uint8_t)text->codepoints[ordinal];
    }
    ascii[ordinal] = '\0';
    if (ordinal == 0U) {
        vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                           VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                           output_exit->logical_program_counter);
        return;
    }
    {
        char *parse_end = NULL;
        uint8_t scratch[sizeof(double)];

        if (output_size > sizeof(scratch)) {
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                               VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                               output_exit->logical_program_counter);
            return;
        }
        memset(scratch, 0, sizeof(scratch));
        if (allow_float != 0) {
            const double parsed = strtod(ascii, &parse_end);

            memcpy(scratch, &parsed, sizeof(parsed));
        } else if (is_signed != 0) {
            const long long parsed = strtoll(ascii, &parse_end, 10);

            memcpy(scratch, &parsed, sizeof(parsed));
        } else {
            const unsigned long long parsed = strtoull(ascii, &parse_end, 10);

            memcpy(scratch, &parsed, sizeof(parsed));
        }
        /* 严格全消费：必须真的消费了内容，且停在结尾。 */
        if (parse_end == ascii || *parse_end != '\0' ||
            (is_signed == 0 && allow_float == 0 && ascii[0] == '-')) {
            vmbin_handler_fail(output_exit, RUNTIMEC_STATUS_INVALID,
                               VMBIN_HANDLER_CALL_DIAG_LENGTH_MISMATCH,
                               output_exit->logical_program_counter);
            return;
        }
        memcpy(output, scratch, output_size);
    }
}


VMBIN_HANDLER_SET_BY_ENCODING(string_set_handler, string_set_patch_build,
                               string_set_rebuild_build)


VMBIN_HANDLER_TEXT_INDEX2(string_slice_handler, string_slice_build, int64_t)
VMBIN_HANDLER_FORMAT_INTEGER(string_format_integer_handler, string_format_build, 1)
VMBIN_HANDLER_FORMAT_INTEGER(string_format_unsigned_handler, string_format_build, 0)

/* 逻辑函数：文本 → 有符号整数。 */
static inline void string_parse_integer_build(const string_text_t *text,
                                              uint8_t *buffer, size_t capacity,
                                              binrwir_execution_exit_t *output_exit) {
    string_parse_number_of(text, 0, 1, buffer, capacity, output_exit);
}
VMBIN_HANDLER_UNARY_TEXT(string_parse_integer_handler, string_parse_integer_build)

/* 逻辑函数：文本 → 无符号整数。 */
static inline void string_parse_unsigned_build(const string_text_t *text,
                                               uint8_t *buffer, size_t capacity,
                                               binrwir_execution_exit_t *output_exit) {
    string_parse_number_of(text, 0, 0, buffer, capacity, output_exit);
}
VMBIN_HANDLER_UNARY_TEXT(string_parse_unsigned_handler, string_parse_unsigned_build)

/* 逻辑函数：文本 → 浮点。 */
static inline void string_parse_float_build(const string_text_t *text,
                                            uint8_t *buffer, size_t capacity,
                                            binrwir_execution_exit_t *output_exit) {
    string_parse_number_of(text, 1, 0, buffer, capacity, output_exit);
}
VMBIN_HANDLER_UNARY_TEXT(string_parse_float_handler, string_parse_float_build)

static const binrwir_registry_entry_t runtimec_string_entry_table[] = {
    {BINRWIR_OPERATION_IDENTIFIER_STRING_LENGTH, BINRWIR_REGISTRY_FLAG_NONE,
     string_length_handler},
    {BINRWIR_OPERATION_IDENTIFIER_STRING_CHARACTER, BINRWIR_REGISTRY_FLAG_NONE,
     string_character_handler},
    {BINRWIR_OPERATION_IDENTIFIER_STRING_COMPARE, BINRWIR_REGISTRY_FLAG_NONE,
     string_compare_handler},
    {BINRWIR_OPERATION_IDENTIFIER_STRING_CONCAT, BINRWIR_REGISTRY_FLAG_NONE,
     string_concat_handler},
    {BINRWIR_OPERATION_IDENTIFIER_STRING_SET, BINRWIR_REGISTRY_FLAG_NONE,
     string_set_handler},
    {BINRWIR_OPERATION_IDENTIFIER_STRING_ORDINAL, BINRWIR_REGISTRY_FLAG_NONE,
     string_ordinal_handler},
    {BINRWIR_OPERATION_IDENTIFIER_STRING_FIND, BINRWIR_REGISTRY_FLAG_NONE,
     string_find_handler},
    {BINRWIR_OPERATION_IDENTIFIER_STRING_SLICE, BINRWIR_REGISTRY_FLAG_NONE,
     string_slice_handler},
    {BINRWIR_OPERATION_IDENTIFIER_STRING_FORMAT_INTEGER,
     BINRWIR_REGISTRY_FLAG_NONE, string_format_integer_handler},
    {BINRWIR_OPERATION_IDENTIFIER_STRING_FORMAT_UNSIGNED_INTEGER,
     BINRWIR_REGISTRY_FLAG_NONE, string_format_unsigned_handler},
    {BINRWIR_OPERATION_IDENTIFIER_STRING_PARSE_INTEGER, BINRWIR_REGISTRY_FLAG_NONE,
     string_parse_integer_handler},
    {BINRWIR_OPERATION_IDENTIFIER_STRING_PARSE_UNSIGNED_INTEGER,
     BINRWIR_REGISTRY_FLAG_NONE, string_parse_unsigned_handler},
    {BINRWIR_OPERATION_IDENTIFIER_STRING_PARSE_FLOAT, BINRWIR_REGISTRY_FLAG_NONE,
     string_parse_float_handler},
};

const binrwir_registry_entry_t *runtimec_string_handler_entries(
    size_t *output_count) {
    if (output_count == NULL) {
        return NULL;
    }
    *output_count = sizeof(runtimec_string_entry_table) /
                    sizeof(runtimec_string_entry_table[0]);
    return runtimec_string_entry_table;
}
