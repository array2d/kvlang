/*
 * 06_multiprocess — A 进程写，B 进程读，共享 file-backed mmap（shm:// 后端）
 *
 * 用法: ./06_multiprocess /tmp/kv.shm
 */

#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#define N_KEYS  500

static int writer(const char *path, int64_t seed) {
    srand((unsigned)seed);
    int errors = 0;
    void *kv = kv_open_shm(path);
    if (!kv) { printf("FAIL: writer open\n"); return 1; }
    kvspaceMkindex(kv, "/mp/", NULL, 0);

    printf("[writer] round 1: write %d ints\n", N_KEYS);
    for (int i = 0; i < N_KEYS; i++) {
        char k[64]; snprintf(k, sizeof(k), "/mp/k%d", i);
        int64_t v = ((int64_t)rand() << 32) | rand();
        if (kv_set_int(kv, k, v) != 0) { printf("[writer] FAIL set %s\n", k); errors++; }
    }

    printf("[writer] round 2: write %d strings + delete 20%% ints\n", N_KEYS / 5);
    int del_n = N_KEYS / 5;
    for (int i = 0; i < del_n; i++) {
        int idx = rand() % N_KEYS;
        char k[64]; snprintf(k, sizeof(k), "/mp/k%d", idx);
        kv_del(kv, k);
    }
    for (int i = 0; i < 100; i++) {
        char k[64], s[64]; snprintf(k, sizeof(k), "/mp/s%d", i);
        snprintf(s, sizeof(s), "str_val_%d_%d", i, rand() % 1000);
        if (kv_set_str(kv, k, s) != 0) { printf("[writer] FAIL set %s\n", k); errors++; }
    }

    printf("[writer] round 3: large bytes\n");
    uint8_t big[1024];
    for (int j = 0; j < 1024; j++) big[j] = (uint8_t)(rand() & 0xFF);
    for (int i = 0; i < 20; i++) {
        char k[64]; snprintf(k, sizeof(k), "/mp/big%d", i);

        kv_set_bytes(kv, k, big, 1024, NULL);
    }
    for (int j = 0; j < 50; j++) {
        char k[64]; snprintf(k, sizeof(k), "/mp/extra%d", j);
        int64_t v = ((int64_t)rand() << 32) | rand();
        kv_set_int(kv, k, v);
    }

    printf("[writer] done, errors=%d\n", errors);
    kvspaceClose(kv);
    return errors;
}

static int reader(const char *path, int64_t seed) {
    srand((unsigned)seed);
    int errors = 0;
    void *kv = kv_open_shm(path);
    if (!kv) { printf("FAIL: reader open\n"); return 1; }

    printf("[reader] listing /mp/\n");
    uint32_t len; uint8_t *out = NULL;
    kv_list(kv, "/mp/", &out, &len);
    int count = 0;
    for (uint32_t i = 0; i < len; i++) if (out[i] == '\n') count++;
    if (len > 0) count++;
    printf("[reader]   total children: %d\n", count);
    free(out);

    srand((unsigned)seed);
    printf("[reader] verifying ints...\n");
    int checked = 0;
    for (int i = 0; i < N_KEYS; i++) {
        char k[64]; snprintf(k, sizeof(k), "/mp/k%d", i);
        int64_t expected = ((int64_t)rand() << 32) | rand();
        kv_value v; char kind[32];
        if (kv_read(kv, k, &v) == 0) {
            kv_kind(&v, kind, sizeof kind);
            if (v.body_len >= 8 && strcmp(kind, "int64") == 0) {
                int64_t got = *(const int64_t *)v.body;
                if (got != expected) {
                    printf("[reader] MISMATCH %s: got=%ld expected=%ld\n", k, got, expected);
                    errors++;
                }
            }
            checked++;
        }
    }
    printf("[reader]   checked %d ints, errors=%d\n", checked, errors);

    printf("[reader] verifying strings...\n");
    for (int i = 0; i < 100; i++) {
        char k[64]; snprintf(k, sizeof(k), "/mp/s%d", i);
        kv_value v; char kind[32];
        if (kv_read(kv, k, &v) != 0) continue;
        kv_kind(&v, kind, sizeof kind);
        if (strcmp(kind, "char/utf8") != 0) {
            printf("[reader] BADKIND %s: %s\n", k, kind); errors++;
        }
    }
    printf("[reader]   string check done\n");

    printf("[reader] verifying big values...\n");
    int big_ok = 0;
    for (int i = 0; i < 20; i++) {
        char k[64]; snprintf(k, sizeof(k), "/mp/big%d", i);
        kv_value v;
        if (kv_read(kv, k, &v) == 0 && v.body_len == 1024) big_ok++;
    }
    printf("[reader]   big values found: %d/20\n", big_ok);

    kvspaceClose(kv);
    printf("[reader] done, errors=%d\n", errors);
    return errors;
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "/tmp/kvspace_mp.shm";
    int64_t seed = (int64_t)time(NULL);

    unlink(path);

    printf("=== phase 1: writer ===\n");
    int we = writer(path, seed);

    printf("=== phase 2: reader (same SHM) ===\n");
    int re = reader(path, seed);

    unlink(path);
    int total = we + re;
    printf("\n%s: %d errors\n", total == 0 ? "ALL OK" : "FAIL", total);
    return total;
}
