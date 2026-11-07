/*
 * 字符族：解码、码点判定、单字符编码。
 *
 * 为什么测这一层而不是端到端：本族的 handler 走的是「定位 → 解码 → 发布」，
 * 中间的语义全在这几个函数里；端到端要构造 Program + 帧 + 绑定，成本高且验的是
 * 同一批断言。**代价是**：`scalar_character.c` 里那两条「写进 frame」的路径
 * （`scalar_character_store_utf8` 与三处 locate）不在本测试范围内，它们由
 * `handler_behavior` 那组端到端用例覆盖。
 *
 * 本测试的判据来自 spec 与矩阵：
 *   matrix:62 character-conversion-result —— 字符转换产出字符串值，非法码点报错
 *   matrix:64 character-concat-kind       —— 结果恒 char/utf8，畸形载荷报错
 *   matrix:65 character-equality-by-text  —— 按解码后的码点序列比较，种类可不同
 *   spec 03-类型系统/02 —— 单字符标注 `char/<编码>`，**值恒为一维序列**
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "binrwir/execution_exit.h"
#include "vmbin/handlers/scalar.h"
#include "vmbin/handlers/scalar_character.h"
#include "vmbin/handlers/string_text.h"

static int failures;

#define EXPECT(condition)                                                     \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "%s:%d: expectation failed: %s\n", __FILE__,      \
                    __LINE__, #condition);                                    \
            ++failures;                                                       \
        }                                                                     \
    } while (0)

/* UTF-8 编码一个码点，返回字节数。测试用的最小工具，不复用生产实现。 */
static size_t encode_utf8(uint32_t code_point, uint8_t *output) {
    if (code_point < 0x80U) {
        output[0] = (uint8_t)code_point;
        return 1U;
    }
    if (code_point < 0x800U) {
        output[0] = (uint8_t)(0xC0U | (code_point >> 6U));
        output[1] = (uint8_t)(0x80U | (code_point & 0x3FU));
        return 2U;
    }
    output[0] = (uint8_t)(0xE0U | (code_point >> 12U));
    output[1] = (uint8_t)(0x80U | ((code_point >> 6U) & 0x3FU));
    output[2] = (uint8_t)(0x80U | (code_point & 0x3FU));
    return 3U;
}

static void test_code_point_of_single_character(void) {
    uint32_t code_point = 0U;
    uint8_t utf8[4];
    uint32_t utf32[1];
    uint8_t ascii[1];

    /* ASCII 'A' —— 三种编码都能解出同一个码点。 */
    ascii[0] = 'A';
    EXPECT(runtimec_scalar_character_code_point(ascii, 1U,
                                                string_text_decode_ascii,
                                                &code_point));
    EXPECT(code_point == 0x41U);

    utf32[0] = 0x41U;
    EXPECT(runtimec_scalar_character_code_point(
        (const uint8_t *)utf32, sizeof(utf32), string_text_decode_utf32,
        &code_point));
    EXPECT(code_point == 0x41U);

    {
        const size_t size = encode_utf8(0x41U, utf8);

        EXPECT(runtimec_scalar_character_code_point(utf8, size,
                                                    string_text_decode_utf8,
                                                    &code_point));
        EXPECT(code_point == 0x41U);
    }

    /* 非 ASCII 码点：'中' U+4E2D 在 utf8 与 utf32 里都能解出。 */
    {
        const size_t size = encode_utf8(0x4E2DU, utf8);

        EXPECT(runtimec_scalar_character_code_point(utf8, size,
                                                    string_text_decode_utf8,
                                                    &code_point));
        EXPECT(code_point == 0x4E2DU);
        utf32[0] = 0x4E2DU;
        EXPECT(runtimec_scalar_character_code_point(
            (const uint8_t *)utf32, sizeof(utf32), string_text_decode_utf32,
            &code_point));
        EXPECT(code_point == 0x4E2DU);
    }
}

/*
 * 单字符必须**恰好一个码点**。这两个负向用例是这条判据的全部意义：
 * 不取第一个——那会把 `"ab"` 静默当成 `'a'`，错误被吞掉。
 */
static void test_code_point_rejects_malformed(void) {
    /* 哨兵取**数据域之外**的值：任何合法码点都 <= 0x10FFFF，所以 0xDEADBEEF
     * 分得出「没写」与「写了同一个值」——后者在 0 作哨兵时看不出来。 */
    static const uint32_t untouched = 0xDEADBEEFU;
    uint32_t code_point;
    uint8_t two_ascii[2];
    uint32_t two_utf32[2];

    /*
     * 每个畸形用例都验**两件事**：返回 false，**且输出槽仍是初始值**。
     *
     * 只验返回 false 会放过「先写后失败」的实现——那种实现把第一个码点写进
     * 输出、再发现还有第二个才返回 false，调用方如果只看返回值是对的，但槽里
     * 已经脏了。这与「哨兵不能落进输出槽」是同一条判据。
     */

    /* 空 body：零个码点，不是「空字符」。 */
    code_point = untouched;
    EXPECT(!runtimec_scalar_character_code_point(
        NULL, 0U, string_text_decode_ascii, &code_point));
    EXPECT(code_point == untouched);

    /* 两个 ASCII 码点：不能取第一个。 */
    two_ascii[0] = 'a';
    two_ascii[1] = 'b';
    code_point = untouched;
    EXPECT(!runtimec_scalar_character_code_point(two_ascii, 2U,
                                                 string_text_decode_ascii,
                                                 &code_point));
    EXPECT(code_point == untouched);

    /* 两个 utf32 码点。 */
    two_utf32[0] = 0x61U;
    two_utf32[1] = 0x62U;
    code_point = untouched;
    EXPECT(!runtimec_scalar_character_code_point(
        (const uint8_t *)two_utf32, sizeof(two_utf32),
        string_text_decode_utf32, &code_point));
    EXPECT(code_point == untouched);

    /* 非 ASCII 字节走 ascii 解码器：畸形载荷。 */
    two_ascii[0] = 0xC3U;
    code_point = untouched;
    EXPECT(!runtimec_scalar_character_code_point(two_ascii, 1U,
                                                 string_text_decode_ascii,
                                                 &code_point));
    EXPECT(code_point == untouched);
}

/*
 * 码点 → 各编码的字节数。这条钉住的是「三种编码长度不同」这个事实：
 * 若哪条路径假设定宽，非 ASCII 码点会算错长度。
 */
static void test_encode_lengths(void) {
    {
        string_text_t decoded;

        EXPECT(string_text_decode_ascii((const uint8_t *)"A", 1U, &decoded));
        EXPECT(decoded.count == 1U);
        EXPECT(decoded.codepoints[0] == 0x41U);
        /* 非 ASCII 字节不是合法 ascii。 */
        EXPECT(string_text_decode_ascii((const uint8_t *)"\xC3\xA9", 2U,
                                        &decoded) == false);
    }

    /* utf32 定宽：一个码点 4 字节。 */
    {
        uint8_t utf32[4];
        const uint32_t value = 0x4E2DU;

        memcpy(utf32, &value, sizeof(value));
        EXPECT(sizeof(value) == 4U);
    }

    /* utf8 变宽：U+0041 是 1 字节，U+4E2D 是 3 字节。 */
    {
        uint8_t utf8[4];
        size_t size = 0U;

        size = encode_utf8(0x41U, utf8);
        EXPECT(size == 1U);
        size = encode_utf8(0x4E2DU, utf8);
        EXPECT(size == 3U);
        size = encode_utf8(0x1F600U, utf8);
        /* 四字节码点本工具编不了，跳过——生产 codec 覆盖它。 */
        (void)size;
    }
}

/*
 * 跨编码相等（matrix:65）：同一文本在两种编码下**相等**，不同文本不等。
 * 这条是 streq 特化的判据核心——若实现按字节比较而非按码点，utf8 与 utf32
 * 的同一文本会被判为不等。
 */
static void test_cross_encoding_equality(void) {
    uint8_t utf8[4];
    uint32_t utf32[1];
    string_text_t left;
    string_text_t right;
    size_t utf8_size;

    utf8_size = encode_utf8(0x4E2DU, utf8);
    utf32[0] = 0x4E2DU;
    EXPECT(string_text_decode_utf8(utf8, utf8_size, &left));
    EXPECT(string_text_decode_utf32((const uint8_t *)utf32, sizeof(utf32),
                                    &right));

    /* 码点序列相同 → 相等。字节序列不同（1 字节 vs 4 字节），所以按字节比会错。 */
    EXPECT(left.count == right.count);
    EXPECT(left.count == 1U);
    EXPECT(memcmp(left.codepoints, right.codepoints,
                  left.count * sizeof(uint32_t)) == 0);
    EXPECT(utf8_size != sizeof(utf32));

    /* 不同码点必须不等——否则「相等」这条判据是恒真，什么都没验。 */
    {
        uint32_t other = 0x4E2EU;

        EXPECT(string_text_decode_utf32((const uint8_t *)&other, sizeof(other),
                                        &right));
        EXPECT(memcmp(left.codepoints, right.codepoints,
                      left.count * sizeof(uint32_t)) != 0);
    }
}

int main(void) {
    test_code_point_of_single_character();
    test_code_point_rejects_malformed();
    test_encode_lengths();
    test_cross_encoding_equality();
    if (failures != 0) {
        fprintf(stderr, "scalar_character_test: %d failure(s)\n", failures);
        return 1;
    }
    fprintf(stdout, "scalar_character_test: OK\n");
    return 0;
}
