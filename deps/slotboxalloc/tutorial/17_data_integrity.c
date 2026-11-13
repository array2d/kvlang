#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <slotboxalloc/slotboxobj.h>

#define META_SIZE (16 * 1024 * 1024)
#define DATA_SIZE (8ULL * 64 * 64 * 64 * 64)
#define MAX_LIVE 512

static void wr64(uint8_t *base, uint64_t off, uint64_t val) {
    memcpy(base + off, &val, sizeof(val));
}
static uint64_t rd64(uint8_t *base, uint64_t off) {
    uint64_t v;
    memcpy(&v, base + off, sizeof(v));
    return v;
}

static uint64_t mkpat(uint64_t off, uint64_t seed) {
    uint64_t h = 0xcbf29ce484222325ULL ^ seed;
    h ^= off; h *= 0x100000001b3ULL;
    return h;
}

int main() {
    uint8_t *meta = malloc(META_SIZE);
    uint8_t *data = malloc(DATA_SIZE);
    memset(meta, 0, META_SIZE);
    if (sbo_init(meta, META_SIZE, DATA_SIZE) != 0) {
        printf("FAIL: init\n"); return 1;
    }

    typedef struct { uint64_t off; uint64_t req_sz; uint64_t alloc_sz; uint64_t seed; } live_t;
    live_t live[MAX_LIVE];
    int nlive = 0;

    size_t sizes[] = {8, 16, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384};
    int ns = (int)(sizeof(sizes) / sizeof(sizes[0]));
    uint64_t seed = 1;
    int n_allocs = 0;

    for (int round = 0; round < 20000; round++) {
        
        int n_to_free = (nlive > 10) ? (round % 3 + 1) : 0;
        for (int f = 0; f < n_to_free && nlive > 0; f++) {
            int idx = (round * 7 + f * 13) % nlive;

            uint64_t want = mkpat(live[idx].off, live[idx].seed);
            if (rd64(data, live[idx].off) != want) {
                printf("FAIL: [r%d] off=%lu req_sz=%lu alloc_sz=%lu corrupt\n",
                       round, live[idx].off, live[idx].req_sz, live[idx].alloc_sz);
                return 1;
            }
            
            if (live[idx].req_sz >= 16) {
                uint64_t toff = live[idx].off + live[idx].req_sz - 8;
                uint64_t twant = mkpat(toff, live[idx].seed);
                if (rd64(data, toff) != twant) {
                    printf("FAIL: [r%d] tail-corrupt off=%lu req_sz=%lu alloc_sz=%lu tail_off=%lu\n",
                           round, live[idx].off, live[idx].req_sz, live[idx].alloc_sz, toff);
                    return 1;
                }
            }

            sbo_free(meta, live[idx].off);
            live[idx] = live[--nlive];
        }

        int n_to_alloc = (round % 3) + 1;
        for (int a = 0; a < n_to_alloc && nlive < MAX_LIVE; a++) {
            size_t req_sz = sizes[(round * 3 + a * 7) % ns];
            uint64_t off = sbo_alloc(meta, req_sz);
            if (off == (uint64_t)-1) {
                if (nlive > MAX_LIVE / 2) continue;
                printf("FAIL: [r%d] alloc(%zu)=\n", round, req_sz);
                return 1;
            }
            n_allocs++;

            uint64_t alloc_sz = sbo_allocated_size(meta, off);
            if (alloc_sz < req_sz) {
                printf("FAIL: [r%d] allocated_size=%lu < req=%zu\n", round, alloc_sz, req_sz);
                return 1;
            }

            uint64_t end = off + alloc_sz;
            for (int i = 0; i < nlive; i++) {
                uint64_t le = live[i].off + live[i].alloc_sz;
                if (!(end <= live[i].off || off >= le)) {
                    printf("FAIL: [r%d] OVERLAP [%lu,%lu) vs live[%d][%lu,%lu)\n",
                           round, off, end, i, live[i].off, le);
                    return 1;
                }
            }

            if (off + req_sz > DATA_SIZE) {
                printf("FAIL: [r%d] off=%lu req=%zu > DATA\n", round, off, req_sz);
                return 1;
            }
            uint64_t pat_seed = ++seed;
            wr64(data, off, mkpat(off, pat_seed));
            if (req_sz >= 16) {
                wr64(data, off + req_sz - 8, mkpat(off + req_sz - 8, pat_seed));
            }

            for (int i = 0; i < nlive; i++) {
                if (rd64(data, live[i].off) != mkpat(live[i].off, live[i].seed)) {
                    printf("FAIL: [r%d] neighbor[%d] off=%lu corrupted\n", round, i, live[i].off);
                    return 1;
                }
            }

            live[nlive++] = (live_t){off, req_sz, alloc_sz, pat_seed};
        }

        if (round % 500 == 0) {
            for (int i = 0; i < nlive; i++) {
                if (rd64(data, live[i].off) != mkpat(live[i].off, live[i].seed)) {
                    printf("FAIL: periodic[%d] off=%lu\n", i, live[i].off);
                    return 1;
                }
            }
        }
    }

    for (int i = 0; i < nlive; i++) {
        if (rd64(data, live[i].off) != mkpat(live[i].off, live[i].seed)) {
            printf("FAIL: final[%d] off=%lu\n", i, live[i].off);
            return 1;
        }
        sbo_free(meta, live[i].off);
    }

    free(meta); free(data);
    printf("PASS allocs=%d rounds=20000 max_live=%d\n", n_allocs, nlive);
    return 0;
}
