#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdint.h>

#include <slotsboxmalloc/slotsboxobj.h>

#define SMALL_OBJ_SIZE 8

#define DATA_SIZE (8ULL * 64 * 64 * 64)

int main() {
    uint8_t *buddy = calloc(1, 4 * 1024 * 1024);
    uint8_t *data = malloc(DATA_SIZE);
    if (!buddy || !data) {
        perror("Failed to allocate memory for slotsboxmalloc");
        return 1;
    }

    if (sbo_init(buddy, 4 * 1024 * 1024, DATA_SIZE) != 0) {
        printf("Failed to initialize slotsboxmalloc\n");
        free(buddy);
        free(data);
        return 1;
    }

    printf("Phase 1: Allocating small objects until full...\n");
    int capacity = 1000000;
    void **ptrs = malloc(capacity * sizeof(void *));
    if (!ptrs) {
        perror("Failed to allocate ptrs array");
        free(buddy);
        free(data);
        return 1;
    }

    int alloc_count = 0;
    while (1) {
        uint64_t obj_offset = sbo_alloc(buddy,  SMALL_OBJ_SIZE);
        if (obj_offset == (uint64_t)-1) {
            break;
        }
        if (alloc_count >= capacity) {
            capacity *= 2;
            ptrs = realloc(ptrs, capacity * sizeof(void *));
            if (!ptrs) {
                perror("Failed to realloc ptrs array");
                free(buddy);
                free(data);
                return 1;
            }
        }
        ptrs[alloc_count] = data + obj_offset;
        *(uint64_t *)ptrs[alloc_count] = alloc_count;
        alloc_count++;
    }
    printf("Allocated %d small objects\n", alloc_count);

    printf("Phase 2: Starting random free/malloc loop (2s limit)...\n");
    srand(time(NULL));
    long long loop_count = 0;
    int failed = 0;
    clock_t start = clock();

    while (1) {
        if (loop_count > 0 && (loop_count % 5000 == 0) && (double)(clock() - start) / CLOCKS_PER_SEC > 2.0) break;
        int random_index = rand() % alloc_count;
        void *ptr_to_free = ptrs[random_index];

        uint64_t offset = (uint8_t *)ptr_to_free - data;
        sbo_free(buddy, offset);

        uint64_t new_offset = sbo_alloc(buddy, SMALL_OBJ_SIZE);
        if (new_offset == (uint64_t)-1) {
            printf("Re-allocation failed at loop %lld\n", loop_count);
            failed = 1;
            break;
        }

        ptrs[random_index] =  data + new_offset;
        *(uint64_t *)ptrs[random_index] = random_index;

        loop_count++;

        if (loop_count % 10000 == 0) {
            clock_t current = clock();
            double elapsed = (double)(current - start) / CLOCKS_PER_SEC;
            printf("Loop %lld: Time elapsed %.2f seconds\n", loop_count, elapsed);
        }
    }

    for (int i = 0; i < alloc_count; i++) {
        uint64_t offset= (uint8_t *)ptrs[i] - data;
        sbo_free(buddy, offset);
    }
    free(ptrs);
    free(buddy);
    free(data);
    printf("Stress test completed after %lld loops.\n", loop_count);
    return failed;
}
