/*
 * contract/member_key_text 的用例。
 *
 * 判据逐条钉在 spec `05-runtime语义/03-成员访问.kv`「浮点文本」的键层规则上，
 * **不拿参考实现的输出当基线**——参考实现的 `fmt_float_key` 在整数值补 `.0`
 * （`if (!strpbrk(t, ".eE")) strcat(t, ".0")`），与 spec 第 3 条「键模式不带 `.0`」
 * 冲突，已按 spec 裁决。所以这里每一格的期望值都注明出处，而不是「跟参考一样」。
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "contract/member_key_text.h"
#include "contract/status.h"

static int failures;

#define KEY_EXPECT(condition)                                                 \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "%s:%d: expectation failed: %s\n", __FILE__,      \
                    __LINE__, #condition);                                    \
            failures += 1;                                                    \
        }                                                                     \
    } while (0)

/*
 * 复用同一个缓冲并整块填哨兵：失败路径必须**零写入**，而「零写入」只有在
 * 缓冲事先不是正确值时才能与「写对了」区分开。
 */
#define KEY_SENTINEL 0x5a
#define KEY_OUTPUT_CAPACITY 8192U

static char g_output[KEY_OUTPUT_CAPACITY + 1U];

static void key_reset(void) {
    memset(g_output, KEY_SENTINEL, sizeof(g_output));
}

/* 从 `cursor` 起直到缓冲末尾仍然全是哨兵。 */
static int key_untouched_from(size_t cursor) {
    for (size_t index = cursor; index < sizeof(g_output); ++index) {
        if ((unsigned char)g_output[index] != KEY_SENTINEL) {
            return 0;
        }
    }
    return 1;
}

static runtimec_status_t key_call(const char *langtype, const void *body,
                                  size_t body_size, size_t capacity,
                                  size_t *output_length,
                                  runtimec_error_t *error) {
    return runtimec_member_key_text(
        runtimec_string_view_from_c_string(langtype),
        runtimec_bytes_view_make(body, body_size), g_output, capacity,
        output_length, error);
}

/* 成功路径：状态 + 文本 + 长度 + NUL 一次钉住。 */
static void expect_text(const char *langtype, const void *body, size_t body_size,
                        const char *expected) {
    size_t length = 0U;
    runtimec_error_t error;

    runtimec_error_reset(&error);
    key_reset();
    {
        const runtimec_status_t status =
            key_call(langtype, body, body_size, sizeof(g_output), &length,
                     &error);

        if (status != RUNTIMEC_STATUS_SUCCESS) {
            fprintf(stderr, "%s: expected success, got status %d\n", langtype,
                    (int)status);
            failures += 1;
            return;
        }
    }
    if (length != strlen(expected) || memcmp(g_output, expected, length) != 0 ||
        g_output[length] != '\0') {
        fprintf(stderr, "%s: expected \"%s\", got \"%.*s\" (length %zu)\n",
                langtype, expected, (int)length, g_output, length);
        failures += 1;
    }
    /* 文本之后必须全是哨兵：不多写一个字节。 */
    KEY_EXPECT(key_untouched_from(length + 1U));
}

/*
 * 失败路径：状态、诊断码、`*output_length` 清零、缓冲零写入，四样一起断言。
 *
 * 只断言「失败」不够——把元组错报成「不支持形态」时它照样绿，而两者指向
 * 完全不同的修法。
 */
static void expect_failure(const char *langtype, const void *body,
                           size_t body_size, runtimec_status_t expected_status,
                           runtimec_member_key_text_diagnostic_t expected_code) {
    size_t length = 123456U;
    runtimec_error_t error;

    runtimec_error_reset(&error);
    key_reset();
    {
        const runtimec_status_t status =
            key_call(langtype, body, body_size, sizeof(g_output), &length,
                     &error);

        if (status != expected_status) {
            fprintf(stderr, "%s: expected status %d, got %d\n", langtype,
                    (int)expected_status, (int)status);
            failures += 1;
        }
    }
    if (length != 0U) {
        fprintf(stderr, "%s: expected output_length 0 on failure, got %zu\n",
                langtype, length);
        failures += 1;
    }
    if (error.code != (int64_t)expected_code) {
        fprintf(stderr, "%s: expected diagnostic %d, got %lld\n", langtype,
                (int)expected_code, (long long)error.code);
        failures += 1;
    }
    if (!key_untouched_from(0U)) {
        fprintf(stderr, "%s: failure wrote into the output buffer\n", langtype);
        failures += 1;
    }
}

/* ── 浮点键层 ─────────────────────────────────────────────────────────
 *
 * 每一格的期望值出处是 spec `05-runtime语义/03-成员访问.kv`「浮点文本」：
 *   第 1 条 负零归一 → `-0.0` 得 `"0"`；
 *   第 3 条 整数值键模式不带 `.0` → `1.0` 得 `"1"`；
 *   第 4 条 指数阈值 `< -4` 或 `>= 16`、补两位 → `1e-05`、`1e+16`；
 *   第 5 条 键模式 ±Inf 允许、NaN 报错。
 */
static void test_floats(void) {
    const double one = 1.0;
    const double hundred = 100.0;
    const double negative_zero = -0.0;
    const double positive_zero = 0.0;
    const double one_and_half = 1.5;
    const double tenth = 0.1;
    const double third = 1.0 / 3.0;
    const double small = 1e-05;
    const double small_fixed = 1e-04;
    const double big = 1e16;
    const double big_fixed = 1e15;
    const double infinity = 1.0 / 0.0;
    const double negative_infinity = -1.0 / 0.0;

    expect_text("float64", &one, sizeof(one), "1");
    expect_text("float64", &hundred, sizeof(hundred), "100");
    expect_text("float64", &negative_zero, sizeof(negative_zero), "0");
    expect_text("float64", &positive_zero, sizeof(positive_zero), "0");
    expect_text("float64", &one_and_half, sizeof(one_and_half), "1.5");
    expect_text("float64", &tenth, sizeof(tenth), "0.1");
    expect_text("float64", &third, sizeof(third), "0.3333333333333333");
    expect_text("float64", &small, sizeof(small), "1e-05");
    expect_text("float64", &small_fixed, sizeof(small_fixed), "0.0001");
    expect_text("float64", &big, sizeof(big), "1e+16");
    expect_text("float64", &big_fixed, sizeof(big_fixed), "1000000000000000");
    expect_text("float64", &infinity, sizeof(infinity), "inf");
    expect_text("float64", &negative_infinity, sizeof(negative_infinity),
                "-inf");
}

/*
 * float32 的往返域是 `strtof`，不是 `strtod`。
 *
 * 判据出处同 spec 第 2 条：按值本身的宽度定往返域。用 double 域判会让
 * `3.4028235f` 一路加到 17 位，打出那些在被截断成 float32 后根本不存在的尾数。
 */
static void test_float32(void) {
    const float one = 1.0f;
    const float maximum = 3.4028235e38f;
    const float negative_zero = -0.0f;

    expect_text("float32", &one, sizeof(one), "1");
    expect_text("float32", &maximum, sizeof(maximum), "3.4028235e+38");
    expect_text("float32", &negative_zero, sizeof(negative_zero), "0");
}

/* NaN：键模式报错（显示模式才输出 `nan`）。 */
static void test_nan_rejected(void) {
    const double not_a_number = 0.0 / 0.0;

    expect_failure("float64", &not_a_number, sizeof(not_a_number),
                   RUNTIMEC_STATUS_INVALID,
                   RUNTIMEC_MEMBER_KEY_TEXT_DIAG_NON_FINITE);
}

/* ── 整数 ───────────────────────────────────────────────────────────── */
static void test_integers(void) {
    const int64_t minimum = INT64_MIN;
    const uint64_t maximum = UINT64_MAX;
    const int8_t negative_one = -1;
    const uint8_t two_hundred_fifty_five = 255U;
    const int32_t negative_thirty = -30;

    expect_text("int64", &minimum, sizeof(minimum), "-9223372036854775808");
    expect_text("uint64", &maximum, sizeof(maximum), "18446744073709551615");
    /* 符号扩展按**声明宽度**：int8 的 0xFF 是 -1，不是 255。 */
    expect_text("int8", &negative_one, sizeof(negative_one), "-1");
    expect_text("uint8", &two_hundred_fifty_five, sizeof(two_hundred_fifty_five),
                "255");
    expect_text("int32", &negative_thirty, sizeof(negative_thirty), "-30");
}

/* 布尔：小写 true/false（Go/Rust/JS/C 阵营，Python 的 True 不取）。 */
static void test_booleans(void) {
    const uint8_t yes = 1U;
    const uint8_t no = 0U;

    expect_text("bool", &yes, sizeof(yes), "true");
    expect_text("bool", &no, sizeof(no), "false");
}

/* ── 字符与字符串 ───────────────────────────────────────────────────── */

/*
 * `[.]`/`[?]`/`[*]`/`[+]` 与 `[]` 是**同一个值类型**，必须给出同一个键文本。
 *
 * 判据出处：`layout/src/lower.rs:72-83` 的 `value_shape` 把这五个 inner 全部
 * `parse::<i32>()` 失败成 0，于是 layout 眼里都是 `(1,[0],"char/…")`。
 * 同一个值类型给出两个键，就是同一个值在容器里占两个成员。
 */
static void test_variable_dimension_same_key(void) {
    const char text[] = "中文";
    const char *const spellings[] = {"[]char/utf8", "[.]char/utf8",
                                     "[?]char/utf8", "[*]char/utf8",
                                     "[+]char/utf8"};
    char expected[sizeof(text)];

    memcpy(expected, text, sizeof(text));
    for (size_t ordinal = 0U;
         ordinal < sizeof(spellings) / sizeof(spellings[0]); ++ordinal) {
        expect_text(spellings[ordinal], text, sizeof(text) - 1U, expected);
    }
}

/* utf32 与 utf8 对同一串必须给出同一个键（编码是存储形态，不是键内容）。 */
static void test_utf32_matches_utf8(void) {
    /* "中" = U+4E2D，"文" = U+6587，小端 4 字节一码点。 */
    const uint8_t codepoints[] = {0x2d, 0x4e, 0x00, 0x00,
                                  0x87, 0x65, 0x00, 0x00};

    expect_text("[]char/utf32", codepoints, sizeof(codepoints), "中文");
    expect_text("char/utf32", codepoints, sizeof(codepoints), "中文");
}

/* ascii 与 utf8 对纯 ASCII 同键。 */
static void test_ascii(void) {
    const char text[] = "abc";

    expect_text("[]char/ascii", text, sizeof(text) - 1U, "abc");
    expect_text("char/ascii", text, sizeof(text) - 1U, "abc");
}

/*
 * 空 body ⇒ 空串键（已裁）。字符串没有「应有的宽度」，空串是合法值。
 *
 * 这里**不另开特例分支**是实现的判据；测试只断言结果，不论证实现方式。
 */
static void test_empty_string(void) {
    size_t length = 123456U;
    runtimec_error_t error;

    runtimec_error_reset(&error);
    key_reset();
    KEY_EXPECT(key_call("[]char/utf8", "", 0U, sizeof(g_output), &length,
                        &error) == RUNTIMEC_STATUS_SUCCESS);
    KEY_EXPECT(length == 0U);
    KEY_EXPECT(g_output[0] == '\0');
    KEY_EXPECT(key_untouched_from(1U));
}

/* ── 容量：`snprintf` 模型的两条单位用例 ────────────────────────────── */

/*
 * 空串 + `capacity == 0` ⇒ 需要 1 字节放 NUL，所以是 `INSUFFICIENT_CAPACITY`
 * 且回报所需**文本**长度 0。这条钉住「capacity 含 NUL、output_length 不含」。
 */
static void test_capacity_empty_string(void) {
    size_t length = 123456U;
    runtimec_error_t error;

    runtimec_error_reset(&error);
    key_reset();
    KEY_EXPECT(key_call("[]char/utf8", "", 0U, 0U, &length, &error) ==
               RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY);
    KEY_EXPECT(length == 0U);
    KEY_EXPECT(error.code ==
               (int64_t)RUNTIMEC_MEMBER_KEY_TEXT_DIAG_INSUFFICIENT_CAPACITY);
    KEY_EXPECT(key_untouched_from(0U));
}

/* `"abcd"` + `capacity == 4` ⇒ 4 字节文本 + NUL 放不下，回报所需文本长度 4。 */
static void test_capacity_missing_nul(void) {
    const char text[] = "abcd";
    size_t length = 123456U;
    runtimec_error_t error;

    runtimec_error_reset(&error);
    key_reset();
    KEY_EXPECT(key_call("[]char/utf8", text, sizeof(text) - 1U, 4U, &length,
                        &error) == RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY);
    KEY_EXPECT(length == 4U);
    KEY_EXPECT(key_untouched_from(0U));
}

/* 两段式：4KB 字符串首次报 4096，二次调用成功。 */
static void test_capacity_two_stage(void) {
    static char large[4096];
    size_t length = 0U;
    runtimec_error_t error;

    memset(large, 'x', sizeof(large));
    runtimec_error_reset(&error);
    key_reset();
    KEY_EXPECT(key_call("[]char/utf8", large, sizeof(large), 64U, &length,
                        &error) == RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY);
    KEY_EXPECT(length == sizeof(large));
    KEY_EXPECT(key_untouched_from(0U));
    KEY_EXPECT(key_call("[]char/utf8", large, sizeof(large), sizeof(g_output),
                        &length, &error) == RUNTIMEC_STATUS_SUCCESS);
    KEY_EXPECT(length == sizeof(large));
}

/* ── 四条不同的拒绝，各自断言消息文本 ────────────────────────────────
 *
 * 只断言「拒了」不够：四条路径的修法完全不同。消息文本是**契约的一部分**，
 * 断言原文能防止「以后有人以为是漏实现又给补上」。
 */

static void test_rejections(void) {
    const uint8_t tuple_body[8] = {1, 0, 0, 0, 2, 0, 0, 0};
    const uint8_t int32_body[4] = {1, 0, 0, 0};
    const uint8_t text_body[4] = {'a', 'b', 'c', 'd'};
    struct {
        const char *langtype;
        const void *body;
        size_t body_size;
        const char *fragment;
    } cases[] = {
        /* 元组：**没有值形态**。文本由 parser 从源码原样产出。 */
        {"[int32,int32]", tuple_body, sizeof(tuple_body),
         "tuple keys have no value form"},
        /* 形状：`[]int32` 是最要紧的一条——body 恰好 4 字节时会被静默当单个 int32。 */
        {"[]int32", int32_body, sizeof(int32_body),
         "a shaped langtype is a value form"},
        {"[4]char/utf8", text_body, sizeof(text_body),
         "a shaped langtype is a value form"},
        {"[][]char/utf8", text_body, sizeof(text_body),
         "a shaped langtype is a value form"},
        {"[2,3]int32", text_body, sizeof(text_body),
         "a shaped langtype is a value form"},
        /* 不在表：解析成功，但形态不是可用键。 */
        {"time", int32_body, sizeof(int32_body),
         "langtype cannot be a member key"},
        {"[]char/utf8\xc2\xb7int64", text_body, sizeof(text_body),
         "langtype cannot be a member key"},
    };

    for (size_t ordinal = 0U; ordinal < sizeof(cases) / sizeof(cases[0]);
         ++ordinal) {
        size_t length = 123456U;
        runtimec_error_t error;
        runtimec_status_t status;

        runtimec_error_reset(&error);
        key_reset();
        status = key_call(cases[ordinal].langtype, cases[ordinal].body,
                          cases[ordinal].body_size, sizeof(g_output), &length,
                          &error);
        if (status != RUNTIMEC_STATUS_UNSUPPORTED) {
            fprintf(stderr, "%s: expected UNSUPPORTED, got %d\n",
                    cases[ordinal].langtype, (int)status);
            failures += 1;
        }
        if (error.code !=
            (int64_t)RUNTIMEC_MEMBER_KEY_TEXT_DIAG_UNSUPPORTED_LANGTYPE) {
            fprintf(stderr, "%s: expected UNSUPPORTED_LANGTYPE, got %lld\n",
                    cases[ordinal].langtype, (long long)error.code);
            failures += 1;
        }
        if (length != 0U) {
            fprintf(stderr, "%s: expected output_length 0, got %zu\n",
                    cases[ordinal].langtype, length);
            failures += 1;
        }
        if (error.message.data == NULL ||
            strstr(error.message.data, cases[ordinal].fragment) == NULL) {
            fprintf(stderr, "%s: message does not contain \"%s\", got \"%s\"\n",
                    cases[ordinal].langtype, cases[ordinal].fragment,
                    error.message.data != NULL ? error.message.data : "(null)");
            failures += 1;
        }
        if (!key_untouched_from(0U)) {
            fprintf(stderr, "%s: failure wrote into the buffer\n",
                    cases[ordinal].langtype);
            failures += 1;
        }
    }
}

/* 畸形 langtype 走 `INVALID_REQUEST`，与上面三条「形态不支持」不同码。 */
static void test_malformed_langtype(void) {
    const uint8_t body[4] = {1, 0, 0, 0};

    expect_failure("[2,3]", body, sizeof(body), RUNTIMEC_STATUS_INVALID,
                   RUNTIMEC_MEMBER_KEY_TEXT_DIAG_INVALID_REQUEST);
    expect_failure("", body, sizeof(body), RUNTIMEC_STATUS_INVALID,
                   RUNTIMEC_MEMBER_KEY_TEXT_DIAG_INVALID_REQUEST);
}

/* body 宽度与声明类型不符 ⇒ `MALFORMED_BODY`，不按长度反推类型。 */
static void test_malformed_body(void) {
    const uint8_t five_bytes[5] = {1, 2, 3, 4, 5};
    const uint8_t three_codepoint_bytes[3] = {1, 2, 3};

    expect_failure("int32", five_bytes, sizeof(five_bytes),
                   RUNTIMEC_STATUS_INVALID,
                   RUNTIMEC_MEMBER_KEY_TEXT_DIAG_MALFORMED_BODY);
    /* utf32 body 必须是 4 的倍数。 */
    expect_failure("[]char/utf32", three_codepoint_bytes,
                   sizeof(three_codepoint_bytes), RUNTIMEC_STATUS_INVALID,
                   RUNTIMEC_MEMBER_KEY_TEXT_DIAG_MALFORMED_BODY);
}

int main(void) {
    test_floats();
    test_float32();
    test_nan_rejected();
    test_integers();
    test_booleans();
    test_variable_dimension_same_key();
    test_utf32_matches_utf8();
    test_ascii();
    test_empty_string();
    test_capacity_empty_string();
    test_capacity_missing_nul();
    test_capacity_two_stage();
    test_rejections();
    test_malformed_langtype();
    test_malformed_body();
    if (failures != 0) {
        fprintf(stderr, "member_key_text_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("member_key_text_test: OK\n");
    return 0;
}
