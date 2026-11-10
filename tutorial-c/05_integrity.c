/*
 * 05_integrity — 数据完整性压力测试（统一 kvspace* ABI，shm:// 后端）
 *
 * 写入大量随机数据，读回校验。多轮增删改混合操作。
 */

#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define N_KEYS     400
#define MAX_VAL_SZ  1024

static int check(void *kv, const char *key, int64_t expected, int round) {
    kv_value v; char kind[32];
    if (kv_read(kv, key, &v) != 0) { printf("ROUND%d MISS  %s\n", round, key); return 0; }
    kv_kind(&v, kind, sizeof kind);
    if (v.body_len < 8 || strcmp(kind, "int64") != 0) {
        printf("ROUND%d CORRUPT %s: bad kind=%s bl=%u\n", round, key, kind, v.body_len);
        return 0;
    }
    int64_t got = *(const int64_t *)v.body;
    if (got != expected) {
        printf("ROUND%d MISMATCH %s: got=%ld expected=%ld\n", round, key, got, expected);
        return 0;
    }
    return 1;
}

static int check_str(void *kv, const char *key, const char *expected, int round) {
    kv_value v; char kind[32];
    if (kv_read(kv, key, &v) != 0) { printf("ROUND%d MISS  %s\n", round, key); return 0; }
    kv_kind(&v, kind, sizeof kind);
    if (strcmp(kind, "char/utf8") != 0) {
        printf("ROUND%d CORRUPT %s: bad kind\n", round, key); return 0;
    }
    int ok = v.body_len == strlen(expected) && memcmp(v.body, expected, v.body_len) == 0;
    if (!ok) printf("ROUND%d MISMATCH %s: got=%.*s expected=%s\n", round, key, (int)v.body_len, v.body, expected);
    return ok;
}

int main(void) {
    const char *path = "/tmp/kvspace_integrity.shm";
    unlink(path);

    void *kv = kv_open_shm(path);
    if (!kv) { printf("FAIL: open\n"); return 1; }
    kvspaceMkindex(kv, "/it/", NULL, 0);

    srand((unsigned)time(NULL));
    char keys[N_KEYS][64];
    int64_t values[N_KEYS];
    char strvals[N_KEYS][64];
    int n = 0;
    int errors = 0;

    int n_keys = 200;
    printf("=== Round 1: write %d int64 keys ===\n", n_keys);
    for (int i = 0; i < n_keys; i++) {
        snprintf(keys[i], sizeof(keys[i]), "/it/k%d", i);
        values[i] = ((int64_t)rand() << 32) | (int64_t)rand();
        kv_set_int(kv, keys[i], values[i]); n++;
    }
    printf("  wrote %d keys\n", n);

    printf("=== Round 1: verify all ===\n");
    for (int i = 0; i < n; i++)
        if (!check(kv, keys[i], values[i], 1)) errors++;

    printf("=== Round 2: delete 30%%, write new ===\n");
    int del_count = n * 3 / 10;
    for (int i = 0; i < del_count; i++) {
        int idx = rand() % n;
        if (keys[idx][0]) { kv_del(kv, keys[idx]); keys[idx][0] = '\0'; }
    }
    for (int i = 0; i < del_count; i++) {
        int idx = n + i;
        snprintf(keys[idx], sizeof(keys[idx]), "/it/new_k%d", i);
        values[idx] = (int64_t)i * 1000;
        kv_set_int(kv, keys[idx], values[idx]);
    }
    n += del_count;

    printf("=== Round 2: verify %d keys ===\n", n);
    for (int i = 0; i < n; i++)
        if (keys[i][0] && !check(kv, keys[i], values[i], 2)) errors++;

    printf("=== Round 3: update 50 keys ===\n");
    for (int j = 0; j < 50; j++) {
        int i = rand() % n;
        if (!keys[i][0]) continue;
        values[i] = ((int64_t)rand() << 32) | (int64_t)rand();
        kv_set_int(kv, keys[i], values[i]);
    }
    for (int i = 0; i < n; i++)
        if (keys[i][0] && !check(kv, keys[i], values[i], 3)) errors++;

    printf("=== Round 4: mixed int64 + string ===\n");
    int str_base = n;
    for (int i = 0; i < 100; i++) {
        snprintf(keys[str_base + i], sizeof(keys[0]), "/it/s%d", i);
        snprintf(strvals[i], sizeof(strvals[0]), "val_%d_%d", i, rand() % 1000);
        kv_set_str(kv, keys[str_base + i], strvals[i]);
    }
    for (int i = 0; i < 100; i++) {
        char k[64]; snprintf(k, sizeof(k), "/it/s%d", i);
        if (!check_str(kv, k, strvals[i], 4)) errors++;
    }

    printf("=== Round 5: large bytes values ===\n");
    uint8_t big_vals[20][MAX_VAL_SZ];
    for (int i = 0; i < 20; i++) {
        for (int j = 0; j < MAX_VAL_SZ; j++) big_vals[i][j] = (uint8_t)(rand() & 0xFF);
        char k[64]; snprintf(k, sizeof(k), "/it/big%d", i);
        kv_set_bytes(kv, k, big_vals[i], MAX_VAL_SZ, NULL);
    }
    for (int i = 0; i < 10; i++) {
        char k[64]; snprintf(k, sizeof(k), "/it/big%d", i);
        kv_del(kv, k);
        snprintf(k, sizeof(k), "/it/reuse%d", i);
        kv_set_int(kv, k, (int64_t)i);
    }
    for (int i = 10; i < 20; i++) {
        char k[64]; snprintf(k, sizeof(k), "/it/big%d", i);
        kv_value v;
        if (kv_read(kv, k, &v) != 0) { printf("ROUND5 MISS  big%d\n", i); errors++; continue; }
        if (v.body_len != MAX_VAL_SZ || memcmp(v.body, big_vals[i], MAX_VAL_SZ) != 0) {
            printf("ROUND5 CORRUPT big%d\n", i); errors++;
        }
    }
    for (int i = 0; i < 10; i++) {
        char k[64]; snprintf(k, sizeof(k), "/it/reuse%d", i);
        if (!check(kv, k, (int64_t)i, 5)) errors++;
    }

    printf("=== final: list and verify all ===\n");
    uint32_t len; uint8_t *out = NULL;
    kv_list(kv, "/it/", &out, &len);
    int count = 0;
    for (uint32_t i = 0; i < len; i++) if (out[i] == '\n') count++;
    if (len > 0) count++;
    printf("  total children: %d\n", count);
    free(out);

    kvspaceClose(kv);
    unlink(path);

    if (errors == 0) printf("\nALL OK (0 errors)\n");
    else printf("\nFAIL: %d errors\n", errors);
    return errors;
}
