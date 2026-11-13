#include <pathlang/xvalue.h>

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
    CHECK(pathlangCoordIsCoord("[1]") == 1, "[1] is a coord");
    CHECK(pathlangCoordIsCoord("[1,2]") == 1, "[1,2] is a coord");
    CHECK(pathlangCoordIsCoord("[-1]") == 1, "[-1] is a coord");
    
    CHECK(pathlangCoordIsCoord("[12.5,3]") == 1, "[12.5,3] is a coord");
    CHECK(pathlangCoordIsCoord("[my,name]") == 1, "[my,name] is a coord");
    
    CHECK(pathlangCoordIsCoord("[]") == 0, "[] is not a coord (too short)");
    CHECK(pathlangCoordIsCoord("[1") == 0, "[1 is not a coord");
    CHECK(pathlangCoordIsCoord("1]") == 0, "1] is not a coord");
    CHECK(pathlangCoordIsCoord("[[1]]") == 0, "[[1]] is not a coord (nested)");
    CHECK(pathlangCoordIsCoord("x") == 0, "x is not a coord");
    CHECK(pathlangCoordIsCoord("") == 0, "empty is not a coord");
    CHECK(pathlangCoordIsCoord(NULL) == 0, "NULL is not a coord");
}

static void test_parse_coord(void) {
    int64_t c[8];
    CHECK(pathlangParseCoord("[1]", c, 8) == 1 && c[0] == 1, "[1] -> {1}");
    CHECK(pathlangParseCoord("[1,2]", c, 8) == 2 && c[0] == 1 && c[1] == 2,
          "[1,2] -> {1,2}");
    
    CHECK(pathlangParseCoord("[-1]", c, 8) < 0, "[-1] is not integer-parseable");
    CHECK(pathlangParseCoord("[1.5]", c, 8) < 0, "[1.5] is not integer-parseable");
    CHECK(pathlangParseCoord("[a]", c, 8) < 0, "[a] is not integer-parseable");
    CHECK(pathlangParseCoord("[1,]", c, 8) < 0, "[1,] is malformed");
    CHECK(pathlangParseCoord("[1]x", c, 8) < 0, "[1]x is malformed");
    
    int64_t one[1];
    CHECK(pathlangParseCoord("[1,2]", one, 1) < 0, "capacity 1 rejects [1,2]");
}

static void test_cmp(void) {
    
    CHECK(pathlangCoordCmp("[1]", "x") < 0, "coord before non-coord");
    CHECK(pathlangCoordCmp("x", "[1]") > 0, "non-coord after coord");
    
    CHECK(pathlangCoordCmp("a", "b") < 0 && pathlangCoordCmp("b", "a") > 0,
          "non-coords compare lexicographically");
    CHECK(pathlangCoordCmp("ab", "ab") == 0, "equal non-coords");
    
    CHECK(pathlangCoordCmp("[2]", "[10]") < 0, "numeric order: 2 < 10");
    CHECK(pathlangCoordCmp("[10]", "[2]") > 0, "numeric order: 10 > 2");
    
    CHECK(pathlangCoordCmp("[1]", "[1,0]") < 0, "row-major: shorter first");
    CHECK(pathlangCoordCmp("[1,0]", "[1,1]") < 0, "[1,0] < [1,1]");
    CHECK(pathlangCoordCmp("[2,0]", "[1,9]") > 0, "[2,0] > [1,9]");
    
    CHECK(pathlangCoordCmp("[-1]", "[-2]") < 0, "lexicographic: [-1] < [-2]");
    CHECK(pathlangCoordCmp("[1.5]", "[10]") < 0, "lexicographic: '[1.' < '[10'");
    
    CHECK(pathlangCoordCmp("[1,2]", "[1,2]") == 0, "equal coords");
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
