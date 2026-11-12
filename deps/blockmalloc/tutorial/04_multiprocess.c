#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include "blockmalloc/blockmalloc.h"

#define NUM_PROCESSES 4
#define BLOCKS_PER_PROCESS 500
#define POOL_SIZE (1024 * 1024 * 4)
#define BLOCK_SIZE 64

int main() {
    blocks_meta_t *meta = mmap(NULL, sizeof(blocks_meta_t),
        PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    uint8_t *pool = mmap(NULL, POOL_SIZE,
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
            int64_t *ids = malloc(BLOCKS_PER_PROCESS * sizeof(int64_t));

            for (int i = 0; i < BLOCKS_PER_PROCESS; i++) {
                ids[i] = blocks_alloc(meta, pool);
                if (ids[i] < 0) {
                    printf("[ERROR] process %d alloc failed at i=%d\n", getpid(), i);
                    free(ids);
                    _exit(1);
                }
            }
            for (int i = 0; i < BLOCKS_PER_PROCESS; i++) {
                blocks_free(meta, pool, ids[i]);
            }
            free(ids);
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

    printf("multiprocess: used=%zu malloc=%zu\n",
           (size_t)meta->used_blocks, (size_t)meta->malloc_blocks);

    ok = ok && (meta->used_blocks == 0);
    munmap(meta, sizeof(blocks_meta_t));
    munmap(pool, POOL_SIZE);
    return ok ? 0 : 1;
}
