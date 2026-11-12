#include <kvlang/xvalue.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

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
    
    CHECK(kvlangCoordIsCoord("[12.5,3]") == 1, "[12.5,3] is a coord");
    CHECK(kvlangCoordIsCoord("[my,name]") == 1, "[my,name] is a coord");
    
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
    
    CHECK(kvlangParseCoord("[-1]", c, 8) < 0, "[-1] is not integer-parseable");
    CHECK(kvlangParseCoord("[1.5]", c, 8) < 0, "[1.5] is not integer-parseable");
    CHECK(kvlangParseCoord("[a]", c, 8) < 0, "[a] is not integer-parseable");
    CHECK(kvlangParseCoord("[1,]", c, 8) < 0, "[1,] is malformed");
    CHECK(kvlangParseCoord("[1]x", c, 8) < 0, "[1]x is malformed");
    
    int64_t one[1];
    CHECK(kvlangParseCoord("[1,2]", one, 1) < 0, "capacity 1 rejects [1,2]");
}

static void test_cmp(void) {
    
    CHECK(kvlangCoordCmp("[1]", "x") < 0, "coord before non-coord");
    CHECK(kvlangCoordCmp("x", "[1]") > 0, "non-coord after coord");
    
    CHECK(kvlangCoordCmp("a", "b") < 0 && kvlangCoordCmp("b", "a") > 0,
          "non-coords compare lexicographically");
    CHECK(kvlangCoordCmp("ab", "ab") == 0, "equal non-coords");
    
    CHECK(kvlangCoordCmp("[2]", "[10]") < 0, "numeric order: 2 < 10");
    CHECK(kvlangCoordCmp("[10]", "[2]") > 0, "numeric order: 10 > 2");
    
    CHECK(kvlangCoordCmp("[1]", "[1,0]") < 0, "row-major: shorter first");
    CHECK(kvlangCoordCmp("[1,0]", "[1,1]") < 0, "[1,0] < [1,1]");
    CHECK(kvlangCoordCmp("[2,0]", "[1,9]") > 0, "[2,0] > [1,9]");
    
    CHECK(kvlangCoordCmp("[-1]", "[-2]") < 0, "lexicographic: [-1] < [-2]");
    CHECK(kvlangCoordCmp("[1.5]", "[10]") < 0, "lexicographic: '[1.' < '[10'");
    
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
