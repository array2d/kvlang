/*
 * 组件：contract/langtype 的实现
 * 作用：langtype 文本 → 形态描述符。完整契约见头注释。
 * 联系：`contract/member_key_text` 的唯一形态判定入口。
 * 约束：纯函数、不分配；输出先清零。
 *
 * ── 分段顺序就是判据顺序 ──────────────────────────────────────────────
 *
 * 先认「整串根本不是值类型」（structref / mapexpr），再剥维度段，再看基种类。
 * 顺序颠倒会给出**错误的拒绝理由**：`[]char/utf8·int64` 剥完维度段后 base 是
 * `char/utf8·int64`，既不是字符种类也不是标量，会被判成 ARRAY 并报
 * 「形状是值形态不是键形态」——而它其实是一张 map。拒绝是对的，理由是错的，
 * 而错误的消息会把人引到错的地方修。
 */

#include <stdio.h>

#include "contract/langtype.h"

#include <string.h>

/* 十个定宽数字 + bool 的唯一定义点（本文件内）。 */
typedef struct runtimec_langtype_scalar_entry {
    const char *text;
    runtimec_langtype_scalar_t scalar;
    uint32_t width;
} runtimec_langtype_scalar_entry_t;

static const runtimec_langtype_scalar_entry_t runtimec_langtype_scalar_table[] = {
    {"bool", RUNTIMEC_LANGTYPE_SCALAR_BOOLEAN, 1U},
    {"int8", RUNTIMEC_LANGTYPE_SCALAR_SIGNED_INTEGER, 1U},
    {"int16", RUNTIMEC_LANGTYPE_SCALAR_SIGNED_INTEGER, 2U},
    {"int32", RUNTIMEC_LANGTYPE_SCALAR_SIGNED_INTEGER, 4U},
    {"int64", RUNTIMEC_LANGTYPE_SCALAR_SIGNED_INTEGER, 8U},
    {"uint8", RUNTIMEC_LANGTYPE_SCALAR_UNSIGNED_INTEGER, 1U},
    {"uint16", RUNTIMEC_LANGTYPE_SCALAR_UNSIGNED_INTEGER, 2U},
    {"uint32", RUNTIMEC_LANGTYPE_SCALAR_UNSIGNED_INTEGER, 4U},
    {"uint64", RUNTIMEC_LANGTYPE_SCALAR_UNSIGNED_INTEGER, 8U},
    {"float32", RUNTIMEC_LANGTYPE_SCALAR_FLOAT, 4U},
    {"float64", RUNTIMEC_LANGTYPE_SCALAR_FLOAT, 8U},
};

static bool runtimec_langtype_text_is(runtimec_string_view_t text,
                                      const char *literal) {
    const size_t size = strlen(literal);

    return text.data != NULL && text.size == size &&
           memcmp(text.data, literal, size) == 0;
}

/* 标量表查找；命中即写 width 与分类。 */
static bool runtimec_langtype_scalar_lookup(runtimec_string_view_t base,
                                            runtimec_langtype_t *output) {
    const size_t count = sizeof(runtimec_langtype_scalar_table) /
                         sizeof(runtimec_langtype_scalar_table[0]);

    for (size_t ordinal = 0U; ordinal < count; ++ordinal) {
        if (runtimec_langtype_text_is(base,
                                      runtimec_langtype_scalar_table[ordinal].text)) {
            output->scalar = runtimec_langtype_scalar_table[ordinal].scalar;
            output->scalar_width =
                runtimec_langtype_scalar_table[ordinal].width;
            return true;
        }
    }
    return false;
}

/* char/<enc> 查找；命中即写编码。 */
static bool runtimec_langtype_char_lookup(runtimec_string_view_t base,
                                         runtimec_langtype_t *output) {
    if (runtimec_langtype_text_is(base, "char/utf8")) {
        output->char_encoding = RUNTIMEC_LANGTYPE_CHAR_ENCODING_UTF8;
        return true;
    }
    if (runtimec_langtype_text_is(base, "char/utf32")) {
        output->char_encoding = RUNTIMEC_LANGTYPE_CHAR_ENCODING_UTF32;
        return true;
    }
    if (runtimec_langtype_text_is(base, "char/ascii")) {
        output->char_encoding = RUNTIMEC_LANGTYPE_CHAR_ENCODING_ASCII;
        return true;
    }
    return false;
}

/*
 * 一个维度段的内容是不是「变长」量词。
 *
 * `""`、`.`、`?`、`*`、`+` 五者等价——这不是宽松解释，是 layout 的判定结果：
 * `layout/src/lower.rs:72-83` 的 `value_shape` 把这五个串全部 `parse::<i32>()`
 * 失败成 `0`，于是 `[]char/utf8`、`[.]char/utf8`、`[?]char/utf8`、`[*]char/utf8`、
 * `[+]char/utf8` 在 layout 里是同一个值类型 `(1,[0],"char/…")`。同一个值类型
 * 必须给出同一个键文本，否则同一个值会有两个键。
 */
static bool runtimec_langtype_dimension_is_variable(
    runtimec_string_view_t dimension) {
    if (dimension.size == 0U) {
        return true;
    }
    if (dimension.size != 1U) {
        return false;
    }
    return dimension.data[0] == '.' || dimension.data[0] == '?' ||
           dimension.data[0] == '*' || dimension.data[0] == '+';
}

/* 段内每一节都是标量名 ⇒ 标量元组（`[int32,int32]`）。 */
static bool runtimec_langtype_tuple_matches(runtimec_string_view_t dimension) {
    size_t start = 0U;

    for (size_t cursor = 0U; cursor <= dimension.size; ++cursor) {
        runtimec_langtype_t element;

        if (cursor != dimension.size && dimension.data[cursor] != ',') {
            continue;
        }
        if (cursor == start) {
            return false; /* 空节（`,,` 或首尾逗号）不是标量名。 */
        }
        memset(&element, 0, sizeof(element));
        if (!runtimec_langtype_scalar_lookup(
                runtimec_string_view_make(dimension.data + start,
                                          cursor - start),
                &element)) {
            return false;
        }
        start = cursor + 1U;
    }
    return true;
}


runtimec_status_t runtimec_langtype_parse(runtimec_string_view_t text,
                                          runtimec_langtype_t *output) {
    size_t offset = 0U;
    int32_t dimension_count = 0;
    runtimec_string_view_t first_dimension = runtimec_string_view_make(NULL, 0U);
    bool malformed = false;

    if (output == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    memset(output, 0, sizeof(*output));
    if (text.size == 0U || text.data == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    /*
     * 清零后 `form` 是 0，而 0 是**合法形态** `SCALAR`——两者不能混。
     * 所以成功路径必须先写一个真形态，失败路径（上面那条）保持全零。
     * 「失败时 `form` 不可读」是契约的一部分：没有 `FORM_NONE` 就没有
     * 「失败的形态值」，读它就是读清零的 0。
     */
    output->form = RUNTIMEC_LANGTYPE_FORM_UNRECOGNIZED;
    /*
     * `*`/`@` 前缀与 `/` 开头：**先判，不剥**。
     *
     * 不剥 `*` 是失败封闭：剥掉后 `*int64` 会变成标量并被当作键放行，而它
     * 是**指针**——指针的键语义是「解引用后的目标」，不是「目标类型的字面量」。
     * 让它在 UNRECOGNIZED 处停下，比猜一个解引用规则安全。
     *
     * **含 `·` 的整串不在这条里**（旧版本曾把它并列在这里，是错的）：`·` 是
     * 旧 map 的键值分隔符，issue #631 改成 `'K'V` 之后按 p5 不再接受。它的
     * 处理在下面单独一段——**具名拒绝（INVALID）并指路新写法**，不是
     * UNRECOGNIZED。UNRECOGNIZED 的含义是「认得出不是引用、只是不在本表」，
     * 而旧语法连「是什么」都谈不上，两者不能混。
     */
    /*
     * **map 类型：首字符是单引号**（issue #631）。键侧是 formatter，
     * 整串不是值类型，但它是一等 form——不是「认不出」。
     *
     * 新判据只需要看**首字符**：`'<formatter>'<value>` 里第一个 `'` 就定了，
     * 不需要再扫整串找 `·`（那是旧 `K·V` 时代的做法）。
     */
    if (text.data[0] == '\'') {
        /*
         * **切分按单引号配对**（issue #631 的文法）：
         *   第 1 个 `'` 是开头；
         *   其后**必须恰好再出现一次 `'`**（闭引号），它是 formatter 的结束；
         *   闭引号之后到串尾是**值侧** langtype，且不得再含 `'`（本轮不支持嵌套）。
         *
         * `base` 填**值侧**：按本头文件的约定「判值是什么种类的用 `base`」，
         * map 的值侧才是「这个 map 的元素是什么」的答案。
         */
        const char *closing =
            memchr(text.data + 1U, '\'', text.size - 1U);

        if (closing == NULL) {
            return RUNTIMEC_STATUS_INVALID; /* 引号未闭合 */
        }
        {
            const size_t value_offset =
                (size_t)(closing - text.data) + 1U;

            if (value_offset >= text.size) {
                return RUNTIMEC_STATUS_INVALID; /* `'K'` 缺值类型 */
            }
            if (memchr(text.data + value_offset, '\'',
                       text.size - value_offset) != NULL) {
                return RUNTIMEC_STATUS_INVALID; /* 嵌套 map：本轮不支持 */
            }
            output->form = RUNTIMEC_LANGTYPE_FORM_MAPPING;
            output->dimension_count = 0;
            output->base = runtimec_string_view_make(
                text.data + value_offset, text.size - value_offset);
        }
        return RUNTIMEC_STATUS_SUCCESS;
    }
    if (text.data[0] == '*' || text.data[0] == '@' || text.data[0] == '/') {
        return RUNTIMEC_STATUS_SUCCESS; /* UNRECOGNIZED */
    }
    /*
     * **旧 map 语法 `[K]·V` 具名拒绝，不落进任何 form。**
     *
     * `·` 曾经是 map 的键值分隔符（`[]char/utf8·int64`）。issue #631 改成
     * `'K'V` 之后，旧写法按 p5（不向后兼容）不再接受——但**必须指路**：
     * 「不行」与「不行、而且该这么写」是两种可用性。
     *
     * 为什么不能让它落进 ARRAY：`[]char/utf8·int64` 剥掉 `[]` 之后 base 是
     * `char/utf8·int64`，那看起来「有形状、有基种类」，会被判成 ARRAY 并报
     * 「形状是值形态不是键形态」——**错误的原因与真实原因（旧语法）无关**。
     *
     * 判据是「类型位置出现 `·`」，不是「首字符是 `[`」：裸 mapexpr
     * （`char/utf8·int64`）同样要走这条。
     */
    {
        size_t scan = 0U;
        bool has_member_separator = false;

        while (scan + 1U < text.size) {
            /* `·` 是 U+00B7，UTF-8 两字节 `0xC2 0xB7`。按单字节找会误命中别的
             * 字符的尾字节，所以必须比完整序列。 */
            if ((uint8_t)text.data[scan] == 0xC2U &&
                (uint8_t)text.data[scan + 1U] == 0xB7U) {
                has_member_separator = true;
                break;
            }
            ++scan;
        }
        if (has_member_separator) {
            return RUNTIMEC_STATUS_INVALID;
        }
    }
    while (offset < text.size && text.data[offset] == '[') {
        const char *closing =
            memchr(text.data + offset, ']', text.size - offset);

        if (closing == NULL) {
            malformed = true;
            break;
        }
        if (dimension_count == 0) {
            first_dimension =
                runtimec_string_view_make(text.data + offset + 1U,
                                          (size_t)(closing - (text.data + offset)) - 1U);
        }
        offset = (size_t)(closing - text.data) + 1U;
        ++dimension_count;
    }
    if (malformed) {
        return RUNTIMEC_STATUS_INVALID;
    }
    output->dimension_count = dimension_count;
    output->base = runtimec_string_view_make(text.data + offset,
                                             text.size - offset);
    if (output->base.size == 0U) {
        /*
         * 维度段之后没有基种类：只有「标量元组」是合法形态，其余是畸形。
         *
         * `[2,3]` 落在畸形：它的节不是标量名。**不能报成元组**——拒绝消息
         * 会说谎（说它是元组，其实它不是），后来的人会顺着错线索去查。
         */
        if (dimension_count == 1 && memchr(first_dimension.data, ',',
                                           first_dimension.size) != NULL &&
            runtimec_langtype_tuple_matches(first_dimension)) {
            output->form = RUNTIMEC_LANGTYPE_FORM_TUPLE;
            return RUNTIMEC_STATUS_SUCCESS;
        }
        return RUNTIMEC_STATUS_INVALID;
    }
    if (dimension_count == 0) {
        if (memchr(output->base.data, '[', output->base.size) != NULL ||
            memchr(output->base.data, ']', output->base.size) != NULL) {
            return RUNTIMEC_STATUS_INVALID; /* 游离括号是畸形，不是未知类型。 */
        }
        if (runtimec_langtype_scalar_lookup(output->base, output)) {
            output->form = RUNTIMEC_LANGTYPE_FORM_SCALAR;
            return RUNTIMEC_STATUS_SUCCESS;
        }
        if (runtimec_langtype_char_lookup(output->base, output)) {
            output->form = RUNTIMEC_LANGTYPE_FORM_CHARACTER;
            return RUNTIMEC_STATUS_SUCCESS;
        }
        return RUNTIMEC_STATUS_SUCCESS; /* UNRECOGNIZED：time、stringkeymap… */
    }
    /*
     * 判据是「**有没有维度前缀**」，不是「形状是否定长」。
     *
     * `[]int32` 的维度段是空的，看起来不像数组——但它的 body 是一段连续的
     * int32，把整段当单个 int32 格式化，长度恰好是 4 时就会**静默产出错键**。
     * 所以只有字符串形态能接受维度前缀，其余一律 ARRAY。
     */
    if (dimension_count == 1 &&
        runtimec_langtype_dimension_is_variable(first_dimension) &&
        runtimec_langtype_char_lookup(output->base, output)) {
        output->form = RUNTIMEC_LANGTYPE_FORM_STRING;
        return RUNTIMEC_STATUS_SUCCESS;
    }
    output->form = RUNTIMEC_LANGTYPE_FORM_ARRAY;
    return RUNTIMEC_STATUS_SUCCESS;
}
