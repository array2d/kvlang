/*
 * contract/langtype 的形态判定用例。
 *
 * 判据是「文本 → 形态」这一格，**不测格式化**（那在 member_key_text_test.c）。
 * 每条用例断言完整的 form，而不是「不是某种形态」——否定式断言在形态新增时
 * 仍会绿，而它要防的正是「新形态被误分到旧桶里」。
 */

#include <stdio.h>
#include <string.h>

#include "contract/langtype.h"

static int failures;

#define LANGTYPE_EXPECT(condition)                                            \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "%s:%d: expectation failed: %s\n", __FILE__,      \
                    __LINE__, #condition);                                    \
            failures += 1;                                                    \
        }                                                                     \
    } while (0)

static runtimec_langtype_t langtype_of(const char *text) {
    runtimec_langtype_t parsed;
    runtimec_status_t status;

    memset(&parsed, 0xff, sizeof(parsed));
    status = runtimec_langtype_parse(runtimec_string_view_from_c_string(text),
                                     &parsed);
    LANGTYPE_EXPECT(status == RUNTIMEC_STATUS_SUCCESS);
    return parsed;
}

static runtimec_status_t status_of(const char *text) {
    runtimec_langtype_t parsed;

    return runtimec_langtype_parse(runtimec_string_view_from_c_string(text),
                                   &parsed);
}

/* 裸标量：类与宽度。 */
static void test_scalars(void) {
    const runtimec_langtype_t int32_value = langtype_of("int32");
    const runtimec_langtype_t uint8_value = langtype_of("uint8");
    const runtimec_langtype_t float64_value = langtype_of("float64");
    const runtimec_langtype_t boolean = langtype_of("bool");

    LANGTYPE_EXPECT(int32_value.form == RUNTIMEC_LANGTYPE_FORM_SCALAR);
    LANGTYPE_EXPECT(int32_value.scalar ==
                    RUNTIMEC_LANGTYPE_SCALAR_SIGNED_INTEGER);
    LANGTYPE_EXPECT(int32_value.scalar_width == 4U);
    LANGTYPE_EXPECT(int32_value.dimension_count == 0);
    LANGTYPE_EXPECT(uint8_value.scalar ==
                    RUNTIMEC_LANGTYPE_SCALAR_UNSIGNED_INTEGER);
    LANGTYPE_EXPECT(uint8_value.scalar_width == 1U);
    LANGTYPE_EXPECT(float64_value.scalar == RUNTIMEC_LANGTYPE_SCALAR_FLOAT);
    LANGTYPE_EXPECT(float64_value.scalar_width == 8U);
    LANGTYPE_EXPECT(boolean.scalar == RUNTIMEC_LANGTYPE_SCALAR_BOOLEAN);
    LANGTYPE_EXPECT(boolean.scalar_width == 1U);
}

/* 裸字符与字符串形态是**两种**形态，不能混。 */
static void test_characters(void) {
    const runtimec_langtype_t character = langtype_of("char/utf8");
    const runtimec_langtype_t string = langtype_of("[]char/utf32");

    LANGTYPE_EXPECT(character.form == RUNTIMEC_LANGTYPE_FORM_CHARACTER);
    LANGTYPE_EXPECT(character.char_encoding ==
                    RUNTIMEC_LANGTYPE_CHAR_ENCODING_UTF8);
    LANGTYPE_EXPECT(character.dimension_count == 0);
    LANGTYPE_EXPECT(string.form == RUNTIMEC_LANGTYPE_FORM_STRING);
    LANGTYPE_EXPECT(string.char_encoding ==
                    RUNTIMEC_LANGTYPE_CHAR_ENCODING_UTF32);
    LANGTYPE_EXPECT(string.dimension_count == 1);
}

/*
 * 五个变长量词与空段是**同一个值类型**。
 *
 * 判据出处：`layout/src/lower.rs:72-83` 把它们全部 `parse::<i32>()` 失败成 0，
 * 于是 layout 眼里都是 `(1,[0],"char/…")`。这里断言它们给出同一个 form——
 * 若哪天有人只放行 `[]` 而拒 `[?]`，这条会红。
 */
static void test_variable_dimension_spellings(void) {
    const char *const spellings[] = {"[]char/utf8",  "[.]char/utf8",
                                     "[?]char/utf32", "[*]char/utf8",
                                     "[+]char/ascii"};

    for (size_t ordinal = 0U;
         ordinal < sizeof(spellings) / sizeof(spellings[0]); ++ordinal) {
        const runtimec_langtype_t parsed = langtype_of(spellings[ordinal]);

        LANGTYPE_EXPECT(parsed.form == RUNTIMEC_LANGTYPE_FORM_STRING);
        LANGTYPE_EXPECT(parsed.dimension_count == 1);
    }
}

/*
 * 有维度前缀但不是字符串形态 ⇒ ARRAY。
 *
 * `[]int32` 是关键一条：维度段是空的，看起来「不像数组」，但它的 body 是
 * 连续 int32。若被判成标量，长度恰好 4 时会把整段当单个 int32 格式化并
 * **静默产出错键**。
 */
static void test_arrays(void) {
    const char *const spellings[] = {"[]int32", "[4]char/utf8", "[2,3]int32",
                                     "[][]char/utf8"};

    for (size_t ordinal = 0U;
         ordinal < sizeof(spellings) / sizeof(spellings[0]); ++ordinal) {
        const runtimec_langtype_t parsed = langtype_of(spellings[ordinal]);

        LANGTYPE_EXPECT(parsed.form == RUNTIMEC_LANGTYPE_FORM_ARRAY);
    }
    LANGTYPE_EXPECT(langtype_of("[][]char/utf8").dimension_count == 2);
}

/* 标量元组：唯一没有 base 的合法形态。 */
static void test_tuples(void) {
    const runtimec_langtype_t tuple = langtype_of("[int32,int32]");
    const runtimec_langtype_t mixed = langtype_of("[float32,int8,uint32]");

    LANGTYPE_EXPECT(tuple.form == RUNTIMEC_LANGTYPE_FORM_TUPLE);
    LANGTYPE_EXPECT(tuple.base.size == 0U);
    LANGTYPE_EXPECT(mixed.form == RUNTIMEC_LANGTYPE_FORM_TUPLE);
}

/* 不在本表的类型：解析成功，但形态是 UNRECOGNIZED（**不等于可当键**）。 */
static void test_unrecognized(void) {
    const char *const spellings[] = {"time", "time/duration", "/lib/Point",
                                     "stringkeymap", "/lib/kvlang/x"};

    for (size_t ordinal = 0U;
         ordinal < sizeof(spellings) / sizeof(spellings[0]); ++ordinal) {
        const runtimec_langtype_t parsed = langtype_of(spellings[ordinal]);

        LANGTYPE_EXPECT(parsed.form == RUNTIMEC_LANGTYPE_FORM_UNRECOGNIZED);
    }
}

/*
 * map 类型（issue #631）：`'<formatter>'<value>`。
 *
 * **用例来自 `workspace/622/coding2/map-type-syntax-cases.md`**（唯一输入源）。
 * Rust 侧 `layout/src/kvkind.rs` 的 `map_langtype_tests` 用**同一份清单**断言，
 * 两边的判定结果必须逐条相同——不同就说明两边语义没对齐，不要改用例迁就实现。
 */
static void test_mapping_spellings(void) {
    /* #1–#8：合法 map 类型，全部应是 MAPPING。 */
    const char *const map_spellings[] = {
        "'int64'int64",                     /* #1 标量 formatter */
        "'[]char/utf8'int64",               /* #2 字符串 formatter（utf8） */
        "'[]char/utf32'int64",              /* #3 字符串 formatter（utf32） */
        "'[int64]'int64",                   /* #4 一元元组 */
        "'[float64,float64]'[]char/utf32",  /* #5 二元元组 */
        "'[int32,int32]'int64",             /* #6 二元元组 */
        "'[int64]\xc2\xb7[]char/utf8'int64", /* #7 两段 key */
        "'bool'int64",                      /* #8 bool formatter */
    };
    for (size_t ordinal = 0U;
         ordinal < sizeof(map_spellings) / sizeof(map_spellings[0]); ++ordinal) {
        const runtimec_langtype_t parsed = langtype_of(map_spellings[ordinal]);

        LANGTYPE_EXPECT(parsed.form == RUNTIMEC_LANGTYPE_FORM_MAPPING);
    }
    /*
     * #3 与 #2 的核心断言：formatter 描述的是**被格式化值的编码**，
     * 输出恒是路径文本。两者都应判为 map——编码不进路径形态。
     */
    LANGTYPE_EXPECT(langtype_of("'[]char/utf8'int64").form ==
                    langtype_of("'[]char/utf32'int64").form);

    /* #13 缺开头引号 ⇒ 不是 map 形态。 */
    LANGTYPE_EXPECT(langtype_of("int64'int64").form !=
                    RUNTIMEC_LANGTYPE_FORM_MAPPING);

    /* #15–#16 非 map。 */
    LANGTYPE_EXPECT(langtype_of("[]char/utf8").form !=
                    RUNTIMEC_LANGTYPE_FORM_MAPPING);
    LANGTYPE_EXPECT(langtype_of("[2]float64").form !=
                    RUNTIMEC_LANGTYPE_FORM_MAPPING);
}

/*
 * 畸形：返回 INVALID，**不产出 form**。
 *
 * `[2,3]` 在这里而不是 TUPLE：它的节不是标量名。若它被报成元组，拒绝消息
 * 会说谎——后来的人会顺着「元组」这条错线索去查，而问题在参数形状。
 */
static void test_malformed(void) {
    const char *const spellings[] = {"[2,3]", "[]", "[[int32]", "int32]"};

    LANGTYPE_EXPECT(status_of("") == RUNTIMEC_STATUS_INVALID);
    for (size_t ordinal = 0U;
         ordinal < sizeof(spellings) / sizeof(spellings[0]); ++ordinal) {
        LANGTYPE_EXPECT(status_of(spellings[ordinal]) ==
                        RUNTIMEC_STATUS_INVALID);
    }
}

/* 失败时输出必须清零，不留上一次的形态。 */
static void test_failure_clears_output(void) {
    runtimec_langtype_t parsed;

    memset(&parsed, 0xff, sizeof(parsed));
    LANGTYPE_EXPECT(runtimec_langtype_parse(
                        runtimec_string_view_from_c_string(""),
                        &parsed) == RUNTIMEC_STATUS_INVALID);
    LANGTYPE_EXPECT(parsed.form == RUNTIMEC_LANGTYPE_FORM_SCALAR);
    LANGTYPE_EXPECT(parsed.scalar == RUNTIMEC_LANGTYPE_SCALAR_NONE);
    LANGTYPE_EXPECT(parsed.scalar_width == 0U);
    LANGTYPE_EXPECT(parsed.dimension_count == 0);
    LANGTYPE_EXPECT(parsed.base.size == 0U);
}

int main(void) {
    test_scalars();
    test_characters();
    test_variable_dimension_spellings();
    test_arrays();
    test_tuples();
    test_unrecognized();
    test_mapping_spellings();
    test_malformed();
    test_failure_clears_output();
    if (failures != 0) {
        fprintf(stderr, "langtype_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("langtype_test: OK\n");
    return 0;
}
