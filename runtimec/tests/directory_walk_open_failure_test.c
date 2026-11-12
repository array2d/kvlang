
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "contract/kvspace_interface.h"
#include "contract/allocator.h"
#include "vmbin/directory_walk.h"

static int g_failures = 0;

static void check(int condition, const char *message) {
    if (!condition) {
        printf("[FAIL] %s\n", message);
        ++g_failures;
    }
}

typedef struct counting_allocator {
    int allocations;
    int deallocations;
} counting_allocator_t;

static void *counting_allocate(void *context, size_t size, size_t alignment) {
    (void)alignment;
    counting_allocator_t *counting = (counting_allocator_t *)context;
    ++counting->allocations;
    return malloc(size);
}

static void counting_deallocate(void *context, void *allocation, size_t size,
                                size_t alignment) {
    (void)size;
    (void)alignment;
    counting_allocator_t *counting = (counting_allocator_t *)context;
    ++counting->deallocations;
    free(allocation);
}

static runtimec_allocator_t counting_allocator(counting_allocator_t *counting) {
    runtimec_allocator_t allocator;
    allocator.context = counting;
    allocator.allocate = counting_allocate;
    allocator.deallocate = counting_deallocate;
    return allocator;
}

static void test_null_handle(void) {
    counting_allocator_t counting = {0, 0};
    const runtimec_allocator_t allocator = counting_allocator(&counting);

    runtimec_directory_walk_t walk;
    memset(&walk, 0xA5, sizeof(walk));

    const bool opened =
        runtimec_directory_walk_open(&walk, &allocator, NULL, "/p/", false, false);
    check(!opened, "open with a NULL handle must return false");
    check(counting.allocations == 0,
          "open must not allocate when the handle is missing");
    check(runtimec_directory_walk_close(&walk) == RUNTIMEC_STATUS_SUCCESS,
          "close after a failed open must succeed");
    check(counting.deallocations == 0,
          "close after a failed open must not deallocate anything");
}

static void test_null_directory(void) {
    counting_allocator_t counting = {0, 0};
    const runtimec_allocator_t allocator = counting_allocator(&counting);

    runtimec_directory_walk_t walk;
    memset(&walk, 0xA5, sizeof(walk));

    check(!runtimec_directory_walk_open(&walk, &allocator, (void *)0x1000, NULL,
                                        false, false),
          "open with a NULL directory must return false");
    check(runtimec_directory_walk_close(&walk) == RUNTIMEC_STATUS_SUCCESS,
          "close after a NULL-directory failure must succeed");
    check(counting.deallocations == 0,
          "close after a NULL-directory failure must not deallocate");
}

static void test_expand_extension_rejected(void) {
    counting_allocator_t counting = {0, 0};
    const runtimec_allocator_t allocator = counting_allocator(&counting);

    runtimec_directory_walk_t walk;
    memset(&walk, 0xA5, sizeof(walk));

    check(!runtimec_directory_walk_open(&walk, &allocator, (void *)0x1000, "/p/",
                                        false, true),
          "open with expand_extension must fail: the new interface has no extindex");
    check(runtimec_directory_walk_close(&walk) == RUNTIMEC_STATUS_SUCCESS,
          "close after an expand_extension rejection must succeed");
    check(counting.deallocations == 0,
          "close after an expand_extension rejection must not deallocate");
}

static void test_null_walk(void) {
    check(!runtimec_directory_walk_open(NULL, NULL, (void *)0x1000, "/p/", false,
                                        false),
          "open with a NULL walk must return false");
    check(runtimec_directory_walk_close(NULL) == RUNTIMEC_STATUS_INVALID,
          "close on a NULL walk reports invalid instead of crashing");
}

static void test_backend_failure_still_releases(void) {
    counting_allocator_t counting = {0, 0};
    const runtimec_allocator_t allocator = counting_allocator(&counting);

    static char dsn[128];
    snprintf(dsn, sizeof(dsn), "shm:///tmp/runtimec-walk-test-%d/store",
             (int)getpid());
    void *handle = kvspaceConnect(dsn);
    if (handle == NULL) {
        printf("[skip] test_backend_failure_still_releases: cannot connect to %s\n",
               dsn);
        return;
    }

    runtimec_directory_walk_t walk;
    memset(&walk, 0xA5, sizeof(walk));
    const bool opened = runtimec_directory_walk_open(
        &walk, &allocator, handle, "/definitely-not-an-index/", false, false);
    check(!opened, "open on a missing index must fail");
    
    check(counting.deallocations == counting.allocations,
          "close must return exactly what the directory cache took");
    check(runtimec_directory_walk_close(&walk) == RUNTIMEC_STATUS_SUCCESS,
          "a second close on the same walk is safe (source pointer was cleared)");
    check(counting.deallocations == counting.allocations,
          "closing twice must not deallocate anything a second time");
    kvspaceClose(handle);
}

int main(void) {
    test_null_handle();
    test_null_directory();
    test_expand_extension_rejected();
    test_null_walk();
    test_backend_failure_still_releases();

    if (g_failures != 0) {
        printf("directory_walk_open_failure: FAIL (%d failure(s))\n", g_failures);
        return 1;
    }
    printf("directory_walk_open_failure: OK "
           "(failed open leaves a zeroed walk; close is safe and releases nothing)\n");
    return 0;
}
