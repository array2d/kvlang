#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include "blockmalloc/blockmalloc.h"

#define NUM_PROCESSES 3
#define NUM_THREADS 3
#define BLOCKS_PER_THREAD 300
#define POOL_SIZE (1024 * 1024 * 4)
#define BLOCK_SIZE 64

static blocks_meta_t *meta;
static uint8_t *pool;

void* thread_func(void *arg) {
    int64_t *ids = malloc(BLOCKS_PER_THREAD * sizeof(int64_t));

    for (int i = 0; i < BLOCKS_PER_THREAD; i++) {
        ids[i] = blocks_alloc(meta, pool);
        if (ids[i] < 0) {
            printf("[ERROR] alloc failed at i=%d\n", i);
            free(ids);
            return (void*)1;
        }
    }
    for (int i = 0; i < BLOCKS_PER_THREAD; i++) {
        blocks_free(meta, pool, ids[i]);
    }
    free(ids);
    return NULL;
}

int main() {
    meta = mmap(NULL, sizeof(blocks_meta_t),
        PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    pool = mmap(NULL, POOL_SIZE,
        PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    if (meta == MAP_FAILED || pool == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    memset(pool, 0, POOL_SIZE);
    blocks_init(meta, POOL_SIZE, BLOCK_SIZE);

    for (int p = 0; p < NUM_PROCESSES; p++) {
        pid_t pid = fork();
        if (pid == 0) {
            pthread_t threads[NUM_THREADS];

            for (int t = 0; t < NUM_THREADS; t++) {
                pthread_create(&threads[t], NULL, thread_func, NULL);
            }
            for (int t = 0; t < NUM_THREADS; t++) {
                void *ret;
                pthread_join(threads[t], &ret);
                if (ret != NULL) _exit(1);
            }
            _exit(0);
        } else if (pid < 0) {
            perror("fork");
            return 1;
        }
    }

    int ok = 1;
    for (int i = 0; i < NUM_PROCESSES; i++) {
        int status;
        wait(&status);
        if (WEXITSTATUS(status) != 0) ok = 0;
    }

    printf("multiprocess+multithread: used=%zu malloc=%zu\n",
           (size_t)meta->used_blocks, (size_t)meta->malloc_blocks);

    ok = ok && (meta->used_blocks == 0);
    munmap(meta, sizeof(blocks_meta_t));
    munmap(pool, POOL_SIZE);
    return ok ? 0 : 1;
}
