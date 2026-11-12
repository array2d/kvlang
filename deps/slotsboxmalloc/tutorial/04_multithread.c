#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <slotsboxmalloc/slotsboxobj.h>

#define NUM_THREADS 8
#define OPS_PER_THREAD 500       
#define META_SIZE (1024 * 1024)  
#define DATA_SIZE (8ULL * 64 * 64 * 64 * 64)  

static uint8_t *meta;
static uint8_t *data;
static _Atomic int error_count = 0;

static const size_t kSizes[] = {8, 16, 32, 64, 128, 256, 512, 1024};

void* thread_func(void *arg) {
    int tid = *(int*)arg;
    uint64_t live[32];
    int live_count = 0;

    for (int i = 0; i < OPS_PER_THREAD; i++) {
        if (live_count >= 16 && (i & 1)) {
            int idx = (int)(((uint64_t)i * 1103515245 + 12345) % (uint64_t)live_count);
            sbo_free(meta, live[idx]);
            live[idx] = live[--live_count];
        } else {
            size_t sz = kSizes[i % 8];
            uint64_t off = sbo_alloc(meta, sz);
            if (off == (uint64_t)-1) {
                printf("[WARN] thread %d alloc(%zu) failed at i=%d\n", tid, sz, i);
                continue;
            }
            if (off + sizeof(uint64_t) > DATA_SIZE) {
                printf("[ERROR] thread %d offset %lu+8 out of range\n", tid, (unsigned long)off);
                error_count++;
                continue;
            }
            uint64_t marker = ((uint64_t)tid << 32) | (uint64_t)i;
            *(uint64_t*)(data + off) = marker;
            if (live_count < 32) {
                live[live_count++] = off;
            }
        }
    }

    for (int i = 0; i < live_count; i++) {
        sbo_free(meta, live[i]);
    }

    return NULL;
}

int main() {
    meta = malloc(META_SIZE);
    data = malloc(DATA_SIZE);
    memset(meta, 0, META_SIZE);
    memset(data, 0, DATA_SIZE);

    if (sbo_init(meta, META_SIZE, DATA_SIZE) != 0) {
        printf("[ERROR] box_init failed\n");
        return 1;
    }

    pthread_t threads[NUM_THREADS];
    int tids[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++) {
        tids[i] = i;
        pthread_create(&threads[i], NULL, thread_func, &tids[i]);
    }
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("multithread: errors=%d\n", error_count);
    free(meta);
    free(data);
    return error_count > 0 ? 1 : 0;
}
