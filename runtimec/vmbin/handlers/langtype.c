/*
 * 组件：vmbin/handlers/langtype 的实现
 * 作用：`parselangtype` 算子——把 head 上的 langtype 解析成基名与维数。
 * 联系：条目由 session 装进 `binrwir_registry_t`；langtype 与值都经
 *       `vmbin/value_access` 取，本文件不读值头、不写偏移算术。
 * 约束：这是**值侧**解析器。实例 head 上只会出现单一具体 kind、定长 dims 与
 *       `*`/`@` 前缀；并集 `A|B` 与轴量词 `?`/`*`/`+`/`.` 属于 `def` 声明侧，
 *       一律拒绝，不猜、不降级。
 *
 * 返回形状：`-> (kind[, ndim])`。写参 0 是基名（char/utf8），写参多于 1 个时
 * 写参 1 是维数（int64）。只绑一个写参时不出 ndim。
 *
 * 两个 opid 共用同一实现（catalog 里 compactarray·parselangtype 0x010D、
 * xv·parselangtype 0x0136，行为决策同为 xv-introspection-result-kind）：
 *   compactarray·parselangtype   rwir_catalog.def:443
 *   xv·parselangtype             rwir_catalog.def:639
 *
 * 与 layout 的 `kvlangKindexprParse` 的关系见 `type_is_parseable` 的注释：
 * 共同域逐字段相等，`[]T` 与 `?` 是**已知且有意**的差异。
 */

#include "vmbin/handlers/langtype.h"

#include <string.h>

#include "binrwir/execution_exit.h"
#include "binrwir/binvthread_frame.h"
#include "binrwir/instruction.h"
#include "binrwir/operation_identifiers.h"
#include "contract/error.h"
#include "contract/program_counter.h"
#include "contract/status.h"
#include "contract/view.h"
#include "vmbin/value_access.h"

/* 基名缓冲：ltype 串最长受 head 约束，256 足够覆盖全部已知 kind。 */
#define LANGTYPE_KIND_CAPACITY 256U
/* 维数上限：与 layout 的 kvlangKindexpr.dims[8] 一致。 */
#define LANGTYPE_DIMENSION_LIMIT 8U

typedef enum langtype_diagnostic {
    LANGTYPE_DIAG_INVALID_REQUEST = 1,
    LANGTYPE_DIAG_UNPARSEABLE = 2,
    LANGTYPE_DIAG_WRITE_FAILED = 3
} langtype_diagnostic_t;

typedef struct langtype_parsed {
    runtimec_string_view_t kind;
    uint32_t dimension_count;
} langtype_parsed_t;

static runtimec_status_t langtype_fail(runtimec_error_t *error,
                                       runtimec_status_t status,
                                       langtype_diagnostic_t code,
                                       const char *message) {
    runtimec_error_set(error, status, RUNTIMEC_ERROR_DOMAIN_CONTRACT,
                       (int64_t)code,
                       runtimec_string_view_from_c_string(message),
                       RUNTIMEC_PROGRAM_COUNTER_NONE);
    return status;
}

/* 十进制非负整数；溢出返回 false。dims 里只允许这种定长元。 */
static bool langtype_parse_dimension(const char *text, size_t size,
                                     uint32_t *output_value) {
    uint32_t value = 0U;
    size_t ordinal;

    if (size == 0U) {
        return false;
    }
    for (ordinal = 0U; ordinal < size; ++ordinal) {
        const char digit = text[ordinal];

        if (digit < '0' || digit > '9') {
            return false;
        }
        if (value > (UINT32_MAX - (uint32_t)(digit - '0')) / 10U) {
            return false;
        }
        value = value * 10U + (uint32_t)(digit - '0');
    }
    *output_value = value;
    return true;
}

/*
 * 解析一个值侧 kindexpr。
 *
 * 接受：`*` / `@` 前缀（剥掉，它们的语义在 head 的 ref 字段里，基名不需要它）、
 *       `[d1,d2,…]` 定长维度、以及纯 kind。
 * 拒绝：并集（出现 `|`）、轴量词（`?` `*` `+` `.` 作 dim 元）、空 kind、以及
 *       `…`（变参）。
 *
 * `[]T` 按 spec 给 ndim=1（`[]` 等价 `[.]`，恰一维任意长）。**layout 当前给
 * ndim=0**——`kvkind::parse_langtype` 把 `[...]` 里的空项 filter 掉了，属实现
 * 缺陷；值侧不照抄，一致性测试里对这条差异有显式断言。
 */
static bool langtype_parse(runtimec_string_view_t text,
                           langtype_parsed_t *output_parsed) {
    size_t cursor = 0U;
    size_t kind_start;
    uint32_t dimension_count = 0U;

    output_parsed->kind = runtimec_string_view_make(NULL, 0U);
    output_parsed->dimension_count = 0U;
    if (text.data == NULL && text.size != 0U) {
        return false;
    }
    /* `*T` / `@T`：ref 已由 head 表达，基名里不带前缀。 */
    if (cursor < text.size &&
        (text.data[cursor] == '*' || text.data[cursor] == '@')) {
        cursor += 1U;
    }
    if (cursor >= text.size) {
        return false;
    }
    if (text.data[cursor] == '[') {
        const size_t close = cursor + 1U;
        size_t scan = close;

        while (scan < text.size && text.data[scan] != ']') {
            ++scan;
        }
        if (scan >= text.size) {
            return false;
        }
        if (scan == close) {
            /* 空 `[]` 等价 `[.]`：恰一维、任意长。 */
            dimension_count = 1U;
        } else {
            size_t element_start = close;

            while (element_start <= scan) {
                size_t element_end = element_start;
                uint32_t dimension = 0U;

                while (element_end < scan && text.data[element_end] != ',') {
                    ++element_end;
                }
                if (!langtype_parse_dimension(text.data + element_start,
                                              element_end - element_start,
                                              &dimension)) {
                    return false;
                }
                if (dimension_count >= LANGTYPE_DIMENSION_LIMIT) {
                    return false;
                }
                dimension_count += 1U;
                element_start = element_end + 1U;
            }
        }
        cursor = scan + 1U;
    }
    kind_start = cursor;
    while (cursor < text.size) {
        const char character = text.data[cursor];

        /* 并集与变参都不是值侧能表达的形状。 */
        if (character == '|' || character == '?') {
            return false;
        }
        ++cursor;
    }
    if (cursor == kind_start) {
        return false;
    }
    output_parsed->kind =
        runtimec_string_view_make(text.data + kind_start, cursor - kind_start);
    output_parsed->dimension_count = dimension_count;
    return true;
}

/* 剥掉声明前缀 `def `：`def rwir` → `rwir`、`def langtype` → `langtype`。
 * 依据 stdlib/kvlang/kvlangbrief.kv:235 对 parselangtype 的定义，以及
 * stdlib/kvlang/reflect.kv:32 的实际依赖（靠它把声明槽归到同一类）。 */
static runtimec_string_view_t langtype_strip_declaration_prefix(
    runtimec_string_view_t kind) {
    static const char prefix[] = "def ";

    if (kind.size > sizeof(prefix) - 1U &&
        memcmp(kind.data, prefix, sizeof(prefix) - 1U) == 0) {
        return runtimec_string_view_make(kind.data + (sizeof(prefix) - 1U),
                                         kind.size - (sizeof(prefix) - 1U));
    }
    return kind;
}

/* 写一个写参；长度必须与既有值体一致，由 value_access 裁定。 */
static runtimec_status_t langtype_write(
    binrwir_binvthread_frame_t *frame, const binrwir_argument_t *argument,
    const void *bytes, size_t size, runtimec_error_t *error) {
    return vmbin_value_access_write_body(
        frame, argument, runtimec_bytes_view_make(bytes, size), error);
}

static runtimec_status_t langtype_execute(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction, runtimec_error_t *error) {
    runtimec_string_view_t langtype = runtimec_string_view_make(NULL, 0U);
    langtype_parsed_t parsed;
    runtimec_string_view_t kind;
    uint64_t dimension_value;
    runtimec_status_t status;

    if (instruction->read_arguments.count != 1U ||
        instruction->write_arguments.count == 0U ||
        instruction->write_arguments.count > 2U) {
        return langtype_fail(
            error, RUNTIMEC_STATUS_INVALID, LANGTYPE_DIAG_INVALID_REQUEST,
            "parselangtype takes one read and one or two writes");
    }
    status = vmbin_value_access_langtype(
        frame, &instruction->read_arguments.data[0], &langtype, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return status;
    }
    if (!langtype_parse(langtype, &parsed)) {
        return langtype_fail(
            error, RUNTIMEC_STATUS_INVALID, LANGTYPE_DIAG_UNPARSEABLE,
            "parselangtype received a kindexpr a value side cannot express");
    }
    kind = langtype_strip_declaration_prefix(parsed.kind);
    status = langtype_write(frame, &instruction->write_arguments.data[0],
                            kind.data, kind.size, error);
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        return langtype_fail(error, status, LANGTYPE_DIAG_WRITE_FAILED,
                             "parselangtype could not write the kind");
    }
    if (instruction->write_arguments.count > 1U) {
        dimension_value = (uint64_t)parsed.dimension_count;
        status = langtype_write(frame, &instruction->write_arguments.data[1],
                                &dimension_value, sizeof(dimension_value),
                                error);
        if (status != RUNTIMEC_STATUS_SUCCESS) {
            return langtype_fail(error, status, LANGTYPE_DIAG_WRITE_FAILED,
                                 "parselangtype could not write the ndim");
        }
    }
    return RUNTIMEC_STATUS_SUCCESS;
}

static runtimec_status_t runtimec_langtype_parse(
    binrwir_binvthread_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit) {
    runtimec_error_t error;

    (void)output_exit;
    runtimec_error_reset(&error);
    return langtype_execute(frame, instruction, &error);
}

/* 静态存储，进程级生命周期；调用方不得释放。 */
static const binrwir_registry_entry_t langtype_entries[] = {
    /* compactarray·parselangtype — rwir_catalog.def:443 */
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_COMPACT_ARRAY_PARSE_LANGTYPE,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = runtimec_langtype_parse},
    /* xv·parselangtype — rwir_catalog.def:639 */
    {.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_XVALUE_PARSE_LANGTYPE,
     .flags = BINRWIR_REGISTRY_FLAG_NONE,
     .handler = runtimec_langtype_parse},
};

const binrwir_registry_entry_t *runtimec_langtype_handler_entries(
    size_t *output_count) {
    if (output_count == NULL) {
        return NULL;
    }
    *output_count = sizeof(langtype_entries) / sizeof(langtype_entries[0]);
    return langtype_entries;
}
