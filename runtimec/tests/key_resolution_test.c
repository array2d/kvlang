
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

typedef struct fake_entry {
    const char *key;
    const char *langtype;
    const char *body;
} fake_entry_t;

typedef struct fake_store {
    const fake_entry_t *entries;
    size_t count;
    int reads; 
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
    return RUNTIMEC_STATUS_INVALID; 
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

static void test_single_hop(void) {
    static const fake_entry_t entries[] = {
        {"/link", "*char", "/target/"},
    };
    expect_resolved(entries, 1U, "/link", "/target/", "one pointer hop follows its body");
}

static void test_multi_hop(void) {
    static const fake_entry_t entries[] = {
        {"/one", "*char", "/two"},
        {"/two", "*char", "/three/"},
    };
    expect_resolved(entries, 2U, "/one", "/three/",
                    "a pointer chain is followed until it reaches a non-pointer");
}

static void test_chain_ending_at_missing_key(void) {
    static const fake_entry_t entries[] = {
        {"/one", "*char", "/missing/"},
    };
    expect_resolved(entries, 1U, "/one", "/missing/",
                    "a chain that ends at a missing key keeps that path");
}

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

static void test_hop_limit(void) {
    
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

static void test_extension_reference_is_not_followed(void) {
    static const fake_entry_t entries[] = {
        {"/ext", "@s3://bucket/key", "/should-not-be-used"},
    };
    expect_resolved(entries, 1U, "/ext", "/ext",
                    "an @ extension reference is not followed (backend only follows PTR)");
}

static void test_double_pointer_counts(void) {
    static const fake_entry_t entries[] = {
        {"/double", "**char", "/through/"},
    };
    expect_resolved(entries, 1U, "/double", "/through/",
                    "**T starts with * and therefore counts as a pointer");
}

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
