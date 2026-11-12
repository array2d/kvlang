#include "kvlangxvalue.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 坐标段工具是「排序上移」后的**唯一**比较器：存储层 `kvspaceList` 按契约返回
   后端顺序、不排序，需要坐标序的调用方全部走这里。本测试就是那份排序规格。 */

static int failures = 0;
#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            failures++;                                                        \
            fprintf(stderr, "  FAIL %s:%d: ", __FILE__, __LINE__);             \
            fprintf(stderr, __VA_ARGS__);                                      \
            fprintf(stderr, "\n");                                             \
        }                                                                      \
    } while (0)

static void test_is_coord(void) {
    CHECK(kvlangCoordIsCoord("[1]") == 1, "[1] is a coord");
    CHECK(kvlangCoordIsCoord("[1,2]") == 1, "[1,2] is a coord");
    CHECK(kvlangCoordIsCoord("[-1]") == 1, "[-1] is a coord");
    /* 结构判定，不要求整数：小数/字符串坐标也算坐标段。 */
    CHECK(kvlangCoordIsCoord("[12.5,3]") == 1, "[12.5,3] is a coord");
    CHECK(kvlangCoordIsCoord("[my,name]") == 1, "[my,name] is a coord");
    /* 空、缺括号、嵌套括号、太短：都不是。 */
    CHECK(kvlangCoordIsCoord("[]") == 0, "[] is not a coord (too short)");
    CHECK(kvlangCoordIsCoord("[1") == 0, "[1 is not a coord");
    CHECK(kvlangCoordIsCoord("1]") == 0, "1] is not a coord");
    CHECK(kvlangCoordIsCoord("[[1]]") == 0, "[[1]] is not a coord (nested)");
    CHECK(kvlangCoordIsCoord("x") == 0, "x is not a coord");
    CHECK(kvlangCoordIsCoord("") == 0, "empty is not a coord");
    CHECK(kvlangCoordIsCoord(NULL) == 0, "NULL is not a coord");
}

static void test_parse_coord(void) {
    int64_t c[8];
    CHECK(kvlangParseCoord("[1]", c, 8) == 1 && c[0] == 1, "[1] -> {1}");
    CHECK(kvlangParseCoord("[1,2]", c, 8) == 2 && c[0] == 1 && c[1] == 2,
          "[1,2] -> {1,2}");
    /* 负号不被整数解析接受——这是与 is_coord 的差别：前者是结构，后者是数值。 */
    CHECK(kvlangParseCoord("[-1]", c, 8) < 0, "[-1] is not integer-parseable");
    CHECK(kvlangParseCoord("[1.5]", c, 8) < 0, "[1.5] is not integer-parseable");
    CHECK(kvlangParseCoord("[a]", c, 8) < 0, "[a] is not integer-parseable");
    CHECK(kvlangParseCoord("[1,]", c, 8) < 0, "[1,] is malformed");
    CHECK(kvlangParseCoord("[1]x", c, 8) < 0, "[1]x is malformed");
    /* 容量不足：拒绝而不是截断。 */
    int64_t one[1];
    CHECK(kvlangParseCoord("[1,2]", one, 1) < 0, "capacity 1 rejects [1,2]");
}

static void test_cmp(void) {
    /* 坐标段恒排在非坐标段之前。 */
    CHECK(kvlangCoordCmp("[1]", "x") < 0, "coord before non-coord");
    CHECK(kvlangCoordCmp("x", "[1]") > 0, "non-coord after coord");
    /* 两个非坐标段：字典序。 */
    CHECK(kvlangCoordCmp("a", "b") < 0 && kvlangCoordCmp("b", "a") > 0,
          "non-coords compare lexicographically");
    CHECK(kvlangCoordCmp("ab", "ab") == 0, "equal non-coords");
    /* 两个整数坐标段：数值序，**不是**字典序（"[10]" < "[9]" 在字典序下成立，
       数值序下反过来）。 */
    CHECK(kvlangCoordCmp("[2]", "[10]") < 0, "numeric order: 2 < 10");
    CHECK(kvlangCoordCmp("[10]", "[2]") > 0, "numeric order: 10 > 2");
    /* 多维：逐维比，前缀相同时维数少者在前（row-major）。 */
    CHECK(kvlangCoordCmp("[1]", "[1,0]") < 0, "row-major: shorter first");
    CHECK(kvlangCoordCmp("[1,0]", "[1,1]") < 0, "[1,0] < [1,1]");
    CHECK(kvlangCoordCmp("[2,0]", "[1,9]") > 0, "[2,0] > [1,9]");
    /* 任一侧不能整数解析（小数/负数/字符串坐标）→ 两侧都退回字典序：比较的是
       原始字节，所以 `[-1]` 与 `[10]` 之间是 `'[' `='['`、`'-'`(0x2D) < `'1'`(0x31)
       ⇒ `[-1] < [10]`。注意这**与数值序相反**（-1 < 10 恰好也成立），真正暴露字典序
       的是 `[-1]` vs `[-2]`：`'1' < '2'` ⇒ `[-1] < [-2]`，而数值序是 -1 > -2。 */
    CHECK(kvlangCoordCmp("[-1]", "[-2]") < 0, "lexicographic: [-1] < [-2]");
    CHECK(kvlangCoordCmp("[1.5]", "[10]") < 0, "lexicographic: '[1.' < '[10'");
    /* 相等。 */
    CHECK(kvlangCoordCmp("[1,2]", "[1,2]") == 0, "equal coords");
}

int main(void) {
    test_is_coord();
    test_parse_coord();
    test_cmp();
    if (failures) {
        fprintf(stderr, "coord: %d failure(s)\n", failures);
        return 1;
    }
    printf("coord: all tests passed\n");
    return 0;
}
