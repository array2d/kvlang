/*
 * `contract/key_order` 与 `contract/key_directory` 的一致性用例。
 *
 * 为什么自成一册而不进 ctest：runtimec 的 CMake 目前没有任何 ctest 目标（测试都在
 * kvlang 的 tutorial / kv_runner 一层）。在引入整套测试框架之前，先用一个可执行文件
 * 把「比较器 + 排序缓存」这两件新东西钉住——它们是第二阶段 ABI 上移的那部分逻辑，
 * 没有后端也能跑。
 *
 * 判据来源：`stdlib/kvlang/spec/02-kvspace模型/07-数组两种物理形态.kv` 的 `cmp_coord`。
 * 每条断言都写清它对应规范哪一句，避免「测试自成一套规范」。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "contract/key_directory.h"
#include "contract/key_order.h"

static int g_failures = 0;

static void check(int condition, const char *message) {
    if (!condition) {
        printf("[FAIL] %s\n", message);
        ++g_failures;
    }
}

static int compare(const char *left, const char *right) {
    return runtimec_key_compare_coord(left, strlen(left), right, strlen(right));
}

static void test_is_coord(void) {
    /* 规范：`[` 开头、`]` 结尾、内部不含嵌套方括号，长度 >= 3。不要求整数。 */
    check(runtimec_key_is_coord("[0]", 3), "[0] is a coord");
    check(runtimec_key_is_coord("[12,34]", 7), "[12,34] is a coord");
    check(runtimec_key_is_coord("[1.5,2]", 7), "float coord is a coord");
    check(runtimec_key_is_coord("[my,name]", 9), "string coord is a coord");
    check(!runtimec_key_is_coord("[]", 2), "[] is too short to be a coord");
    check(!runtimec_key_is_coord("[1][2]", 6), "nested brackets are not a coord");
    check(!runtimec_key_is_coord("abc", 3), "plain name is not a coord");
    check(!runtimec_key_is_coord("[1]", 2), "unterminated bracket is not a coord");
}

static void test_compare(void) {
    /* 规范：坐标段恒排在非坐标段之前。 */
    check(compare("[0]", "abc") < 0, "coord sorts before a plain name");
    check(compare("abc", "[0]") > 0, "plain name sorts after a coord");
    check(compare("abc", "abd") < 0, "plain names use lexicographic order");

    /* **不是「整数优先」**：一边可解析、一边不可时按字典序——与后端
     * `kvspaceCoordCmp`（`na < 0 || nb < 0` → strcmp）逐项一致。
     * 字典序里 '3' < 'a'，所以 [3] 在前；换成 [z] 结论就反过来。 */
    check(compare("[3]", "[a]") < 0, "[3] vs [a]: lexicographic, '3' < 'a'");
    check(compare("[z]", "[1]") > 0, "[z] vs [1]: lexicographic, 'z' > '1'");

    /* 规范：整数值升序——注意这是**数值**序，不是字典序（"[10]" < "[9]" 在字典序成立）。 */
    check(compare("[9]", "[10]") < 0, "integer coords compare by value");
    check(compare("[2,0]", "[10,0]") < 0, "first component is the major axis");
    check(compare("[1,2]", "[1,10]") < 0, "second component breaks the tie");
    /* row-major：前缀相同时维数少者在前。 */
    check(compare("[1]", "[1,0]") < 0, "row-major puts the shorter coord first");

    /* 规范：不可整数解析者按字典序。 */
    check(compare("[a]", "[b]") < 0, "non-integer coords use lexicographic order");
    check(compare("[1.5]", "[1.25]") > 0, "float coords compare as text");

    check(compare("[1,2]", "[1,2]") == 0, "equal coords compare equal");
    check(compare("abc", "abc") == 0, "equal names compare equal");

    /* 前导零：按整数解析后相等（"[01]" 与 "[1]" 是同一坐标）。 */
    check(compare("[01]", "[1]") == 0, "leading zeros compare by value");
    /* 空段非法：`[1,]`、`[,1]`、`[1,,2]` 仍是**坐标段**（结构判定不要求整数），
     * 但不可整数解析，所以同为「非整数坐标」，按字典序比较。 */
    check(runtimec_key_is_coord("[1,]", 4), "[1,] is still structurally a coord");
    check(runtimec_key_is_coord("[1,,2]", 6), "[1,,2] is still structurally a coord");
    check(compare("[1,]", "[a]") < 0, "both are non-integer coords: lexicographic");
    check(compare("[1,,2]", "[a]") < 0, "both are non-integer coords: lexicographic");
}

/* 目录项比较：`cmp_coord` 判等时退回原始名字字节序（后端 child_cmp 的兜底）。 */
static void test_directory_entry_compare(void) {
    check(runtimec_key_compare_directory_entry("[1]", 3, "[1]/", 4) != 0,
          "[1] and [1]/ are distinct directory entries");
    check(runtimec_key_compare_directory_entry("[1]", 3, "[01]", 4) != 0,
          "[1] and [01] are distinct directory entries");
    check(runtimec_key_compare_directory_entry("[1]/", 4, "[1]/", 4) == 0,
          "identical entries compare equal");
    /* 成员名比较先剥尾斜杠：`abc/` 与 `abc` 的 cmp_coord 相等，但作目录项不同。 */
    check(runtimec_key_compare_coord("abc", 3, "abc", 3) == 0,
          "member-name comparison ignores the directory slash");
    check(runtimec_key_compare_directory_entry("abc", 3, "abc/", 4) < 0,
          "the shorter raw name sorts first when coord-equal");

    /* 排序视角：目录项与同名值相邻，且目录项在后（'/' > '\0' 的字节序）。 */
    static const char names[] = "abc\0abc/\0[2]\0[2]/\0";
    check(runtimec_key_compare_directory_entry(names, 3, names + 4, 4) < 0,
          "abc sorts before abc/");
    check(runtimec_key_compare_directory_entry(names + 8, 3, names + 12, 4) < 0,
          "[2] sorts before [2]/");
}

static void test_member_name(void) {
    const char *name = NULL;
    size_t size = 0U;
    /* 目录项带尾 `/`；比较前剥掉。 */
    runtimec_key_member_name("abc/", 4, &name, &size);
    check(size == 3U && strncmp(name, "abc", 3) == 0, "directory trailing slash stripped");
    runtimec_key_member_name("[1,2]", 5, &name, &size);
    check(size == 5U, "value name keeps its brackets");
    runtimec_key_member_name("/", 1, &name, &size);
    check(size == 0U, "root slash becomes empty member");
}

/* ── key_directory：假 source，只返回一段固定名字 ────────────────── */

typedef struct fake_source {
    const char *names;   /* `name\0name\0…` */
    uint32_t length;
    uint32_t count;
    uint64_t generation;
    int calls;        /* 含只探测（names == NULL）的调用 */
    int full_copies;  /* 实际复制名字块的调用 */
} fake_source_t;

static runtimec_status_t fake_list(void *context, const char *directory,
                                   char *names, uint32_t capacity,
                                   uint32_t *output_length, uint32_t *output_count,
                                   uint64_t *output_generation) {
    fake_source_t *source = (fake_source_t *)context;
    (void)directory;
    ++source->calls;
    *output_length = source->length;
    *output_count = source->count;
    *output_generation = source->generation;
    if (names == NULL) {
        return RUNTIMEC_STATUS_SUCCESS;
    }
    ++source->full_copies;
    if (capacity < source->length) {
        return RUNTIMEC_STATUS_INVALID;
    }
    memcpy(names, source->names, source->length);
    return RUNTIMEC_STATUS_SUCCESS;
}

static void *test_allocate(void *context, size_t size, size_t alignment) {
    (void)alignment;
    (void)context;
    return malloc(size);
}

static void test_deallocate(void *context, void *allocation, size_t size,
                            size_t alignment) {
    (void)context;
    (void)size;
    (void)alignment;
    free(allocation);
}

static runtimec_allocator_t test_allocator(void) {
    runtimec_allocator_t allocator;
    allocator.context = NULL;
    allocator.allocate = test_allocate;
    allocator.deallocate = test_deallocate;
    return allocator;
}

static void test_directory_sorts_and_caches(void) {
    /* 后端顺序（故意打乱）：值、目录、乱序坐标。 */
    static const char raw[] = "b\0[10]\0[2]\0a/\0[1,1]\0[1,0]\0";
    fake_source_t source;
    source.names = raw;
    source.length = sizeof(raw) - 1U; /* 去掉字面量末尾的隐式 NUL */
    source.count = 6U;
    source.generation = 7U;
    source.calls = 0;
    source.full_copies = 0;

    runtimec_allocator_t allocator = test_allocator();
    runtimec_key_directory_t directory;
    runtimec_key_directory_reset(&directory, &allocator, &source, fake_list);

    check(runtimec_key_directory_refresh(&directory, "/d") ==
              RUNTIMEC_STATUS_SUCCESS,
          "refresh succeeds");
    check(directory.count == 6U, "all six entries are counted");

    static const char *const expected[] = {"[1,0]", "[1,1]", "[2]", "[10]",
                                           "a/", "b"};
    for (uint32_t index = 0U; index < 6U; ++index) {
        const char *name = NULL;
        uint32_t size = 0U;
        check(runtimec_key_directory_at(&directory, index, &name, &size) ==
                  RUNTIMEC_STATUS_SUCCESS,
              "at() succeeds inside the list");
        const int matches = name != NULL &&
                            strlen(expected[index]) == size &&
                            strncmp(name, expected[index], size) == 0;
        if (!matches) {
            printf("[FAIL] index %u: want %s, got %.*s\n", index,
                   expected[index], (int)size, name != NULL ? name : "(null)");
            ++g_failures;
        }
    }

    /* 缓存：generation 未变时第二次 refresh **只探测、不复制**名字块。
     * 探测本身省不掉——不先问 generation 就无从知道缓存是否还有效。 */
    const int copies_after_first = source.full_copies;
    check(runtimec_key_directory_refresh(&directory, "/d") ==
              RUNTIMEC_STATUS_SUCCESS,
          "second refresh succeeds");
    check(source.full_copies == copies_after_first,
          "unchanged generation does not copy the names again");

    /* generation 前进 → 必须重建。 */
    source.generation = 8U;
    check(runtimec_key_directory_refresh(&directory, "/d") ==
              RUNTIMEC_STATUS_SUCCESS,
          "refresh after a generation bump succeeds");
    check(source.full_copies > copies_after_first,
          "a bumped generation refetches the names");

    /* 越界是 INVALID，不是静默回绕。 */
    check(runtimec_key_directory_at(&directory, 6U, NULL, NULL) ==
              RUNTIMEC_STATUS_INVALID,
          "at() past the end is invalid");

    /* 按名查找走二分，结果与 at() 一致。 */
    uint32_t found = 0U;
    check(runtimec_key_directory_find(&directory, "[10]", 4U, &found) ==
              RUNTIMEC_STATUS_SUCCESS,
          "find a present name");
    const char *name = NULL;
    uint32_t size = 0U;
    runtimec_key_directory_at(&directory, found, &name, &size);
    check(name != NULL && size == 4U && strncmp(name, "[10]", 4) == 0,
          "find returns the same entry at() does");
    check(runtimec_key_directory_find(&directory, "[3]", 3U, &found) ==
              RUNTIMEC_STATUS_INVALID,
          "find a missing name is invalid");

    check(runtimec_key_directory_destroy(&directory) == RUNTIMEC_STATUS_SUCCESS,
          "destroy reports success");
    check(directory.names == NULL && directory.offsets == NULL,
          "destroy releases both buffers");
}

static void test_directory_empty(void) {
    fake_source_t source;
    source.names = "";
    source.length = 0U;
    source.count = 0U;
    source.generation = 1U;
    source.calls = 0;
    source.full_copies = 0;

    runtimec_allocator_t allocator = test_allocator();
    runtimec_key_directory_t directory;
    runtimec_key_directory_reset(&directory, &allocator, &source, fake_list);
    check(runtimec_key_directory_refresh(&directory, "/empty") ==
              RUNTIMEC_STATUS_SUCCESS,
          "an empty directory refreshes cleanly");
    check(directory.count == 0U, "an empty directory has no entries");
    check(runtimec_key_directory_at(&directory, 0U, NULL, NULL) ==
              RUNTIMEC_STATUS_INVALID,
          "at(0) on an empty directory is invalid");
    check(runtimec_key_directory_destroy(&directory) == RUNTIMEC_STATUS_SUCCESS,
          "destroying an empty directory reports success");
}

int main(void) {
    test_is_coord();
    test_compare();
    test_member_name();
    test_directory_entry_compare();
    test_directory_sorts_and_caches();
    test_directory_empty();

    if (g_failures != 0) {
        printf("key_order: FAIL (%d failure(s))\n", g_failures);
        return 1;
    }
    printf("key_order: OK (cmp_coord + sorted directory cache)\n");
    return 0;
}
