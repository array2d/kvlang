
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
    
    check(compare("[0]", "abc") < 0, "coord sorts before a plain name");
    check(compare("abc", "[0]") > 0, "plain name sorts after a coord");
    check(compare("abc", "abd") < 0, "plain names use lexicographic order");

    check(compare("[3]", "[a]") < 0, "[3] vs [a]: lexicographic, '3' < 'a'");
    check(compare("[z]", "[1]") > 0, "[z] vs [1]: lexicographic, 'z' > '1'");

    check(compare("[9]", "[10]") < 0, "integer coords compare by value");
    check(compare("[2,0]", "[10,0]") < 0, "first component is the major axis");
    check(compare("[1,2]", "[1,10]") < 0, "second component breaks the tie");
    
    check(compare("[1]", "[1,0]") < 0, "row-major puts the shorter coord first");

    check(compare("[a]", "[b]") < 0, "non-integer coords use lexicographic order");
    check(compare("[1.5]", "[1.25]") > 0, "float coords compare as text");

    check(compare("[1,2]", "[1,2]") == 0, "equal coords compare equal");
    check(compare("abc", "abc") == 0, "equal names compare equal");

    check(compare("[01]", "[1]") == 0, "leading zeros compare by value");
    
    check(runtimec_key_is_coord("[1,]", 4), "[1,] is still structurally a coord");
    check(runtimec_key_is_coord("[1,,2]", 6), "[1,,2] is still structurally a coord");
    check(compare("[1,]", "[a]") < 0, "both are non-integer coords: lexicographic");
    check(compare("[1,,2]", "[a]") < 0, "both are non-integer coords: lexicographic");
}

static void test_directory_entry_compare(void) {
    check(runtimec_key_compare_directory_entry("[1]", 3, "[1]/", 4) != 0,
          "[1] and [1]/ are distinct directory entries");
    check(runtimec_key_compare_directory_entry("[1]", 3, "[01]", 4) != 0,
          "[1] and [01] are distinct directory entries");
    check(runtimec_key_compare_directory_entry("[1]/", 4, "[1]/", 4) == 0,
          "identical entries compare equal");
    
    check(runtimec_key_compare_coord("abc", 3, "abc", 3) == 0,
          "member-name comparison ignores the directory slash");
    check(runtimec_key_compare_directory_entry("abc", 3, "abc/", 4) < 0,
          "the shorter raw name sorts first when coord-equal");

    static const char names[] = "abc\0abc/\0[2]\0[2]/\0";
    check(runtimec_key_compare_directory_entry(names, 3, names + 4, 4) < 0,
          "abc sorts before abc/");
    check(runtimec_key_compare_directory_entry(names + 8, 3, names + 12, 4) < 0,
          "[2] sorts before [2]/");
}

static void test_member_name(void) {
    const char *name = NULL;
    size_t size = 0U;
    
    runtimec_key_member_name("abc/", 4, &name, &size);
    check(size == 3U && strncmp(name, "abc", 3) == 0, "directory trailing slash stripped");
    runtimec_key_member_name("[1,2]", 5, &name, &size);
    check(size == 5U, "value name keeps its brackets");
    runtimec_key_member_name("/", 1, &name, &size);
    check(size == 0U, "root slash becomes empty member");
}

typedef struct fake_source {
    const char *names;   
    uint32_t length;
    uint32_t count;
    uint64_t generation;
    int calls;        
    int full_copies;  
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
    
    static const char raw[] = "b\0[10]\0[2]\0a/\0[1,1]\0[1,0]\0";
    fake_source_t source;
    source.names = raw;
    source.length = sizeof(raw) - 1U; 
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

    const int copies_after_first = source.full_copies;
    check(runtimec_key_directory_refresh(&directory, "/d") ==
              RUNTIMEC_STATUS_SUCCESS,
          "second refresh succeeds");
    check(source.full_copies == copies_after_first,
          "unchanged generation does not copy the names again");

    source.generation = 8U;
    check(runtimec_key_directory_refresh(&directory, "/d") ==
              RUNTIMEC_STATUS_SUCCESS,
          "refresh after a generation bump succeeds");
    check(source.full_copies > copies_after_first,
          "a bumped generation refetches the names");

    check(runtimec_key_directory_at(&directory, 6U, NULL, NULL) ==
              RUNTIMEC_STATUS_INVALID,
          "at() past the end is invalid");

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
