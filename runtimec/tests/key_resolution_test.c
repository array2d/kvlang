/*
 * `contract/key_resolution` 的 std:: 用例：**Ptr 前缀解析**。
 *
 * 判据逐条对照后端 `kvspace-c/src/kvspace.c:resolve_fetch`：
 *   1. 非 Ptr 的前缀原样返回（后端不跟）；
 *   2. Ptr 的前缀跟到 body 指向的路径；
 *   3. **多跳**：跟随 Ptr 链直到非 Ptr 或 16 跳上限（不是单步跟）；
 *   4. 键不存在 → 用当前路径，**不是错误**；
 *   5. 跳数上限 16 → 错误（不是「用最后那个」）；
 *   6. `@` 扩展引用**不跟**（后端只判 PTR）；
 *   7. `**T` 同样算 Ptr（判据是首字符 `*`）。
 *
 * 用假 reader 而不是真后端：本模块刻意不依赖 kvspace 符号（见头注释），所以它的
 * 测试也不该需要后端——否则「换了 ABI 测试就跑不动」会把这条逻辑锁死在旧接口上。
 */

#include <stdio.h>
#include <string.h>

#include "contract/key_resolution.h"

static int g_failures = 0;

static void check(int condition, const char *message) {
    if (!condition) {
        printf("[FAIL] %s\n", message);
        ++g_failures;
    }
}

/* ── 假 reader ──────────────────────────────────────────────────────
 * `entries` 是一个静态表：key → (langtype, body)。查不到即「键不存在」。
 */
typedef struct fake_entry {
    const char *key;
    const char *langtype;
    const char *body;
} fake_entry_t;

typedef struct fake_store {
    const fake_entry_t *entries;
    size_t count;
    int reads; /* 被读了几次（用来钉住「不该读时确实没读」） */
} fake_store_t;

static runtimec_status_t fake_reader(void *context, const char *key,
                                     const char **output_langtype,
                                     size_t *output_langtype_size,
                                     const char **output_body,
                                     size_t *output_body_size) {
    fake_store_t *store = (fake_store_t *)context;
    ++store->reads;
    for (size_t index = 0U; index < store->count; ++index) {
        if (strcmp(store->entries[index].key, key) == 0) {
            *output_langtype = store->entries[index].langtype;
            *output_langtype_size = strlen(store->entries[index].langtype);
            *output_body = store->entries[index].body;
            *output_body_size = strlen(store->entries[index].body);
            return RUNTIMEC_STATUS_SUCCESS;
        }
    }
    *output_langtype = NULL;
    *output_langtype_size = 0U;
    *output_body = NULL;
    *output_body_size = 0U;
    return RUNTIMEC_STATUS_INVALID; /* 键不存在 */
}

static void expect_resolved(const fake_entry_t *entries, size_t count,
                            const char *key, const char *want,
                            const char *message) {
    fake_store_t store;
    store.entries = entries;
    store.count = count;
    store.reads = 0;

    char resolved[128];
    const runtimec_status_t status = runtimec_key_resolve_prefix(
        fake_reader, &store, key, resolved, sizeof(resolved));
    if (status != RUNTIMEC_STATUS_SUCCESS) {
        printf("[FAIL] %s: status=%d\n", message, (int)status);
        ++g_failures;
        return;
    }
    if (strcmp(resolved, want) != 0) {
        printf("[FAIL] %s: want %s, got %s\n", message, want, resolved);
        ++g_failures;
    }
}

/* 1. 非 Ptr：原样返回，且只读一次。 */
static void test_plain_prefix_is_returned_as_is(void) {
    static const fake_entry_t entries[] = {
        {"/a/", "int64", "42"},
    };
    expect_resolved(entries, 1U, "/a/", "/a/", "a non-pointer prefix is returned as is");

    fake_store_t store;
    store.entries = entries;
    store.count = 1U;
    store.reads = 0;
    char resolved[64];
    check(runtimec_key_resolve_prefix(fake_reader, &store, "/a/", resolved,
                                      sizeof(resolved)) == RUNTIMEC_STATUS_SUCCESS,
          "plain prefix resolves");
    check(store.reads == 1, "a plain prefix costs exactly one read");
}

/* 2. 单跳 Ptr。 */
static void test_single_hop(void) {
    static const fake_entry_t entries[] = {
        {"/link", "*char", "/target/"},
    };
    expect_resolved(entries, 1U, "/link", "/target/", "one pointer hop follows its body");
}

/* 3. Ptr 链：一路跟到非 Ptr 为止。 */
static void test_multi_hop(void) {
    static const fake_entry_t entries[] = {
        {"/one", "*char", "/two"},
        {"/two", "*char", "/three/"},
    };
    expect_resolved(entries, 2U, "/one", "/three/",
                    "a pointer chain is followed until it reaches a non-pointer");
}

/* 3b. 链尾那一跳键不存在：用最后那个路径。 */
static void test_chain_ending_at_missing_key(void) {
    static const fake_entry_t entries[] = {
        {"/one", "*char", "/missing/"},
    };
    expect_resolved(entries, 1U, "/one", "/missing/",
                    "a chain that ends at a missing key keeps that path");
}

/* 4. 键不存在（根本不是 Ptr）：原样返回，不报错。 */
static void test_missing_key_is_not_an_error(void) {
    fake_store_t store;
    store.entries = NULL;
    store.count = 0U;
    store.reads = 0;
    char resolved[64];
    check(runtimec_key_resolve_prefix(fake_reader, &store, "/nowhere/", resolved,
                                      sizeof(resolved)) == RUNTIMEC_STATUS_SUCCESS,
          "a missing key is not an error");
    check(strcmp(resolved, "/nowhere/") == 0,
          "a missing key resolves to itself, so listing it yields an empty list");
}

/* 5. 跳数上限：17 跳的自环必须失败，不是「用最后那个」。 */
static void test_hop_limit(void) {
    /* 自环：/loop 指向自己。 */
    static const fake_entry_t entries[] = {
        {"/loop", "*char", "/loop"},
    };
    fake_store_t store;
    store.entries = entries;
    store.count = 1U;
    store.reads = 0;
    char resolved[64];
    const runtimec_status_t status = runtimec_key_resolve_prefix(
        fake_reader, &store, "/loop", resolved, sizeof(resolved));
    check(status == RUNTIMEC_STATUS_ERROR, "a pointer cycle fails at the hop limit");
    check(store.reads == (int)RUNTIMEC_KEY_RESOLUTION_HOP_LIMIT,
          "the hop limit bounds how many reads the cycle costs");
}

/* 6. `@` 扩展引用不跟。 */
static void test_extension_reference_is_not_followed(void) {
    static const fake_entry_t entries[] = {
        {"/ext", "@s3://bucket/key", "/should-not-be-used"},
    };
    expect_resolved(entries, 1U, "/ext", "/ext",
                    "an @ extension reference is not followed (backend only follows PTR)");
}

/* 7. `**T` 也算 Ptr。 */
static void test_double_pointer_counts(void) {
    static const fake_entry_t entries[] = {
        {"/double", "**char", "/through/"},
    };
    expect_resolved(entries, 1U, "/double", "/through/",
                    "**T starts with * and therefore counts as a pointer");
}

/* 8. 缓冲不足：具名失败，不截断。 */
static void test_capacity_short(void) {
    static const fake_entry_t entries[] = {
        {"/link", "*char", "/a-rather-long-target/"},
    };
    fake_store_t store;
    store.entries = entries;
    store.count = 1U;
    store.reads = 0;
    char resolved[8];
    check(runtimec_key_resolve_prefix(fake_reader, &store, "/link", resolved,
                                      sizeof(resolved)) ==
              RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY,
          "a target that does not fit is reported, never truncated");
}

/* 9. reader 报的真错误要透传，不能被当成「键不存在」。 */
static runtimec_status_t failing_reader(void *context, const char *key,
                                        const char **output_langtype,
                                        size_t *output_langtype_size,
                                        const char **output_body,
                                        size_t *output_body_size) {
    (void)context;
    (void)key;
    *output_langtype = NULL;
    *output_langtype_size = 0U;
    *output_body = NULL;
    *output_body_size = 0U;
    return RUNTIMEC_STATUS_ERROR;
}

static void test_reader_error_propagates(void) {
    char resolved[64];
    check(runtimec_key_resolve_prefix(failing_reader, NULL, "/x", resolved,
                                      sizeof(resolved)) == RUNTIMEC_STATUS_ERROR,
          "a reader failure propagates instead of being read as a missing key");
}

int main(void) {
    test_plain_prefix_is_returned_as_is();
    test_single_hop();
    test_multi_hop();
    test_chain_ending_at_missing_key();
    test_missing_key_is_not_an_error();
    test_hop_limit();
    test_extension_reference_is_not_followed();
    test_double_pointer_counts();
    test_capacity_short();
    test_reader_error_propagates();

    if (g_failures != 0) {
        printf("key_resolution: FAIL (%d failure(s))\n", g_failures);
        return 1;
    }
    printf("key_resolution: OK (Ptr prefix resolution matches the backend's resolve=1)\n");
    return 0;
}
