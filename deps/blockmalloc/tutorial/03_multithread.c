#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "blockmalloc/blockmalloc.h"

#define NUM_THREADS 8
#define BLOCKS_PER_THREAD 500
#define POOL_SIZE (1024 * 1024 * 4)
#define BLOCK_SIZE 64

static blocks_meta_t meta;
static uint8_t *pool;

void* thread_func(void *arg) {
    int64_t *ids = malloc(BLOCKS_PER_THREAD * sizeof(int64_t));

    for (int i = 0; i < BLOCKS_PER_THREAD; i++) {
        ids[i] = blocks_alloc(&meta, pool);
        if (ids[i] < 0) {
            printf("[ERROR] alloc failed at i=%d\n", i);
            free(ids);
            return (void*)1;
        }
    }
    for (int i = 0; i < BLOCKS_PER_THREAD; i++) {
        blocks_free(&meta, pool, ids[i]);
    }
    free(ids);
    return NULL;
}

int main() {
    pool = malloc(POOL_SIZE);
    memset(pool, 0, POOL_SIZE);
    blocks_init(&meta, POOL_SIZE, BLOCK_SIZE);

    pthread_t threads[NUM_THREADS];
    int errors = 0;

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_create(&threads[i], NULL, thread_func, NULL);
    }
    for (int i = 0; i < NUM_THREADS; i++) {
        void *ret;
        pthread_join(threads[i], &ret);
        if (ret != NULL) errors++;
    }

    printf("multithread: used=%zu malloc=%zu errors=%d\n",
           (size_t)meta.used_blocks, (size_t)meta.malloc_blocks, errors);

    int ok = (meta.used_blocks == 0 && errors == 0);
    free(pool);
    return ok ? 0 : 1;
}
