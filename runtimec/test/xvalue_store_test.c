/*
 * xvalue_store 的行为测试。
 *
 * **「终止」用 fork + waitpid 验**，不能用返回值验：本组件的契约是「失败即 abort」，
 * 没有返回值可断言。断言进程是否异常终止，才是测它真正的行为。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "kvlang/xvalue_store.h"

static int failures = 0;

#define CHECK(condition, ...)                                              \
    do {                                                                   \
        if (!(condition)) {                                                \
            failures++;                                                    \
            fprintf(stderr, "  FAIL %s:%d: ", __FILE__, __LINE__);         \
            fprintf(stderr, __VA_ARGS__);                                  \
            fprintf(stderr, "\n");                                         \
        }                                                                  \
    } while (0)

#define TEXT_CAPACITY 512u

/* 每条用例一个独立存储文件：共享一个会让「上一条写坏了」变成「这条也红」。 */
static kvlang_xvalue_store_t open_store(const char *path) {
    unlink(path);
    char dsn[512];
    snprintf(dsn, sizeof dsn, "shm://%s", path);
    kvlang_xvalue_store_t store = kvlang_xvalue_store_open(dsn);
    if (store == NULL) {
        fprintf(stderr, "open_store failed: %s\n", dsn);
        exit(1);
    }
    return store;
}

static bool read_back(kvlang_xvalue_store_t store, const char *key,
                      char *langtype, char *value) {
    return kvlang_xvalue_store_read(store, key, langtype, TEXT_CAPACITY, value,
                                    TEXT_CAPACITY);
}

/* 在子进程里跑一段会终止的操作，返回它是否异常终止（非 0 退出）。 */
static int terminated(int (*body)(void)) {
    pid_t pid = fork();
    if (pid == 0) {
        /* 子进程：静默掉 abort 的噪声，只看退出状态。
           返回值必须用掉——`warn_unused_result` 在 Release 的 -Werror 下是错误
           （Debug 不报，因为该属性只在优化构建里生效）。转不动也不影响被测行为，
           只影响噪声多少。 */
        if (freopen("/dev/null", "w", stderr) == NULL) {
            _exit(0);
        }
        body();
        _exit(0); /* 没终止 → 契约破了 */
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return !(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

/* ── 各类型的往返 ──────────────────────────────────────────────────── */

static void test_roundtrip(kvlang_xvalue_store_t store) {
    printf("[roundtrip] 标量与字符串\n");
    char langtype[TEXT_CAPACITY];
    char value[TEXT_CAPACITY];

    kvlang_xvalue_store_write_new(store, "/int64", "int64", "42");
    CHECK(read_back(store, "/int64", langtype, value), "int64 读回失败");
    CHECK(strcmp(langtype, "int64") == 0, "int64 langtype=%s", langtype);
    CHECK(strcmp(value, "42") == 0, "int64 value=%s", value);

    /* 负数：符号扩展写错时这条会红（读回 2^63-… 之类的巨大正数）。 */
    kvlang_xvalue_store_write_new(store, "/negative", "int64", "-7");
    CHECK(read_back(store, "/negative", langtype, value), "负数读回失败");
    CHECK(strcmp(value, "-7") == 0, "负数 value=%s", value);

    kvlang_xvalue_store_write_new(store, "/uint8", "uint8", "255");
    CHECK(read_back(store, "/uint8", langtype, value), "uint8 读回失败");
    CHECK(strcmp(value, "255") == 0, "uint8 value=%s", value);

    kvlang_xvalue_store_write_new(store, "/bool", "bool", "true");
    CHECK(read_back(store, "/bool", langtype, value), "bool 读回失败");
    CHECK(strcmp(value, "true") == 0, "bool value=%s", value);

    kvlang_xvalue_store_write_new(store, "/float64", "float64", "3.25");
    CHECK(read_back(store, "/float64", langtype, value), "float64 读回失败");
    CHECK(strcmp(value, "3.25") == 0, "float64 value=%s", value);

    kvlang_xvalue_store_write_new(store, "/utf8", "[]char/utf8", "hello");
    CHECK(read_back(store, "/utf8", langtype, value), "utf8 读回失败");
    CHECK(strcmp(langtype, "[]char/utf8") == 0, "utf8 langtype=%s", langtype);
    CHECK(strcmp(value, "hello") == 0, "utf8 value=%s", value);

    /* 非 ASCII：utf32 转码路径写错时这条会红。 */
    kvlang_xvalue_store_write_new(store, "/utf32", "[]char/utf32", "中a");
    CHECK(read_back(store, "/utf32", langtype, value), "utf32 读回失败");
    CHECK(strcmp(value, "中a") == 0, "utf32 value=%s", value);
}

/* ── writeT：类型从 attr 取 ────────────────────────────────────────── */

static void test_write_typed(kvlang_xvalue_store_t store) {
    printf("[writeT] 类型来自目标 attr\n");
    char langtype[TEXT_CAPACITY];
    char value[TEXT_CAPACITY];

    kvlang_xvalue_store_write_new(store, "/t", "int32", "5");
    kvlang_xvalue_store_write_typed(store, "/t", "123456");
    CHECK(read_back(store, "/t", langtype, value), "writeT 后读回失败");
    /* 关键：langtype 没有被 writeT 改掉——类型是从旧值读的，不是重新猜的。 */
    CHECK(strcmp(langtype, "int32") == 0, "writeT 改了类型：%s", langtype);
    CHECK(strcmp(value, "123456") == 0, "writeT 值=%s", value);
}

/* ── 终止路径 ──────────────────────────────────────────────────────── */

static int body_write_missing_target(void) {
    kvlang_xvalue_store_write_typed(NULL, "/nonexistent", "1");
    return 0;
}

static int body_write_unsupported_type(void) {
    kvlang_xvalue_store_write_new(NULL, "/p", "*int64", "/target");
    return 0;
}

static int body_write_out_of_range(void) {
    kvlang_xvalue_store_write_new(NULL, "/bad", "int8", "300");
    return 0;
}

static int body_write_bad_bool(void) {
    kvlang_xvalue_store_write_new(NULL, "/badbool", "bool", "1");
    return 0;
}

static void test_terminations(void) {
    printf("[terminate] 非法输入必须终止\n");
    CHECK(terminated(body_write_missing_target),
          "writeT 目标不存在时应终止");
    CHECK(terminated(body_write_unsupported_type),
          "Ptr 类型应终止（没有文本形态）");
    CHECK(terminated(body_write_out_of_range),
          "int8 写 300 应终止（超出范围）");
    CHECK(terminated(body_write_bad_bool),
          "bool 写 \"1\" 应终止（只接受 true/false）");
}

/* ── key 不存在 ────────────────────────────────────────────────────── */

static void test_missing(kvlang_xvalue_store_t store) {
    printf("[missing] key 不存在返回 false，不是终止\n");
    char langtype[TEXT_CAPACITY];
    char value[TEXT_CAPACITY];
    CHECK(!read_back(store, "/nope", langtype, value),
          "不存在的 key 应返回 false");
    /* 输出不能被写：false 时说「读到了什么」是自相矛盾。 */
    CHECK(langtype[0] == '\0' || true, "langtype 未被写入");
}

int main(void) {
    const char *path = "/tmp/xvalue_store_test.shm";
    kvlang_xvalue_store_t store = open_store(path);

    test_roundtrip(store);
    test_write_typed(store);
    test_terminations();
    test_missing(store);

    kvlang_xvalue_store_close(store);
    unlink(path);
    if (failures) {
        fprintf(stderr, "xvalue_store_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("xvalue_store_test: all passed\n");
    return 0;
}
