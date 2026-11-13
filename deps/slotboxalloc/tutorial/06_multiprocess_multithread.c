#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <slotboxalloc/slotboxobj.h>

static const size_t kSizes[] = {8, 16, 32, 64, 128, 256, 512, 1024};

#define NUM_PROCESSES 3
#define NUM_THREADS 3
#define OPS_PER_THREAD 200
#define META_SIZE (1024 * 1024)
#define DATA_SIZE (8ULL * 64 * 64 * 64 * 64)  

static uint8_t *meta;
static uint8_t *data;

void* thread_func(void *arg) {
    int tid = *(int*)arg;
    uint64_t live[16];
    int live_count = 0;

    for (int i = 0; i < OPS_PER_THREAD; i++) {
        if (live_count >= 8 && (i & 1)) {
            int idx = (int)(((uint64_t)i * 1103515245 + 12345) % (uint64_t)live_count);
            sbo_free(meta, live[idx]);
            live[idx] = live[--live_count];
        } else {
            size_t sz = kSizes[i % 8];
            uint64_t off = sbo_alloc(meta, sz);
            if (off == (uint64_t)-1) continue;
            if (off + sizeof(uint64_t) > DATA_SIZE) return (void*)1;
            uint64_t marker = ((uint64_t)getpid() << 32) | ((uint64_t)tid << 16) | (uint64_t)i;
            *(uint64_t*)(data + off) = marker;
            if (live_count < 16) live[live_count++] = off;
        }
    }
    for (int i = 0; i < live_count; i++) sbo_free(meta, live[i]);
    return NULL;
}

int main() {
    meta = mmap(NULL, META_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    data = mmap(NULL, DATA_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (meta == MAP_FAILED || data == MAP_FAILED) { perror("mmap"); return 1; }
    memset(meta, 0, META_SIZE); memset(data, 0, DATA_SIZE);

    if (sbo_init(meta, META_SIZE, DATA_SIZE) != 0) { printf("[ERROR] box_init failed\n"); return 1; }

    for (int p = 0; p < NUM_PROCESSES; p++) {
        pid_t pid = fork();
        if (pid == 0) {
            pthread_t threads[NUM_THREADS];
            int tids[NUM_THREADS], errors = 0;
            for (int t = 0; t < NUM_THREADS; t++) {
                tids[t] = t;
                pthread_create(&threads[t], NULL, thread_func, &tids[t]);
            }
            for (int t = 0; t < NUM_THREADS; t++) {
                void *ret; pthread_join(threads[t], &ret);
                if (ret != NULL) errors++;
            }
            _exit(errors > 0 ? 1 : 0);
        } else if (pid < 0) { perror("fork"); return 1; }
    }

    int ok = 1;
    for (int i = 0; i < NUM_PROCESSES; i++) { int status; wait(&status); if (WEXITSTATUS(status) != 0) ok = 0; }
    printf("multiprocess+multithread: %s\n", ok ? "ok" : "FAIL");
    munmap(meta, META_SIZE); munmap(data, DATA_SIZE);
    return ok ? 0 : 1;
}
