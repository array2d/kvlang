/*
 * 替身与真库的 wire 逐字节金标准。
 *
 * 用它验证的是一条**替身不漂移**的事实：`tests/fake_kvspace.c` 是对
 * kvspace 实现的照抄，抄的那一刻对，漂移时不报错。2026-11-03 就发生过一次——
 * 替身的 `new_slack` 还在造 8 字节头，而断言已按 16 字节算，于是用例红，
 * 第一反应却是去查产品代码。
 *
 * 做法：同一批构造用例，替身与真库各产出一份 `<case> <total> <hex>`，由
 * `compare_wire_golden.sh` 逐字节 diff。**只比字节，不解释字节**——解释会引入
 * 第二套「什么算合法」的判断，而这里要验的恰恰是两边对同一份 head 定义的理解
 * 一致。
 *
 * 用例只覆盖产品真正使用的构造面（`new_slack`）：适配层的三处调用都走它。
 * 扩大范围到产品不用的构造器，等于为没人走的路维护金标准。
 *
 * 本文件由两个可执行各编译一次：
 *   - 链 `fake_kvspace.c`  → 替身输出
 *   - 由真库提供符号       → 真库输出
 * 两种情况调用的是同一个 `kvspaceXvalueHeadNewSlack` 签名，所以这里不写任何
 * 布局知识——造值与打印之间不允许有第二份事实源。
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <kvspace/xvalue_head.h>

/* 与 backend interface 的函数指针类型逐字一致，两种链接方式都能直接取地址。 */
/*
 * 两版编译各自提供这个符号：`WIRE_GOLDEN_FAKE` 时用替身，否则用真库。
 * 名字不同、签名相同——金标准只比字节，不关心谁来造。
 */
#if defined(WIRE_GOLDEN_FAKE)
int fake_kvspace_new_slack(const uint8_t *data, uint64_t data_size,
                           uint64_t capacity, const char *langtype,
                           uint8_t **output, uint64_t *output_size);
#define golden_new_slack fake_kvspace_new_slack
#else
extern int kvspaceXvalueHeadNewSlack(const uint8_t *data, uint64_t data_size,
                                     uint64_t capacity, const char *langtype,
                                     uint8_t **output, uint64_t *output_size);
#define golden_new_slack kvspaceXvalueHeadNewSlack
#endif

static int failures;

static void emit(const char *name, const uint8_t *wire, uint64_t length) {
    printf("%s %llu ", name, (unsigned long long)length);
    for (uint64_t index = 0; index < length; ++index) {
        printf("%02x", wire[index]);
    }
    printf("\n");
}

/* 构造并以固定格式打印；构造失败也打印一行，让两边在同一位置一起失败。 */
static void emit_case(const char *name, const uint8_t *body, uint64_t body_size,
                      uint64_t capacity, const char *langtype) {
    uint8_t *wire = NULL;
    uint64_t wire_size = 0;

    const int status = golden_new_slack(body, body_size, capacity, langtype,
                                        &wire, &wire_size);
    if (status != 0 || wire == NULL) {
        fprintf(stderr, "golden: case %s failed to construct\n", name);
        ++failures;
        printf("%s CONSTRUCT-FAILED\n", name);
        return;
    }
    emit(name, wire, wire_size);
    free(wire);
}

int main(void) {
    static const uint8_t empty_body[1] = {0};
    static const uint8_t int64_body[8] = {0x2a, 0, 0, 0, 0, 0, 0, 0};
    static const uint8_t bool_body[1] = {1};
    static const uint8_t utf8_body[7] = {0xe4, 0xbd, 0xa0, 0xf0, 0x9f, 0x98,
                                         0x80};
    static const uint8_t ascii_body[3] = {'a', 'b', 'c'};

    /* 顺序固定，两边由同一个 main 保证。 */
    emit_case("slack-empty-no-langtype", empty_body, 0, 0, "");
    emit_case("slack-int64", int64_body, sizeof int64_body, sizeof int64_body,
              "int64");
    emit_case("slack-bool", bool_body, sizeof bool_body, sizeof bool_body,
              "bool");
    emit_case("slack-utf8", utf8_body, sizeof utf8_body, sizeof utf8_body,
              "[]char/utf8");
    emit_case("slack-no-langtype", ascii_body, sizeof ascii_body,
              sizeof ascii_body, "");
    emit_case("slack-no-langtype-null", ascii_body, sizeof ascii_body,
              sizeof ascii_body, NULL);
    /* 容量大于数据：预留形态仍按有效长度落 wire。 */
    emit_case("slack-capacity-larger", ascii_body, sizeof ascii_body, 32U,
              "[]char/ascii");
    /* 空 langtype 空 body：最小合法值。 */
    emit_case("slack-empty-with-langtype", empty_body, 0, 0, "int64");

    if (failures != 0) {
        fprintf(stderr, "wire golden: %d construction failure(s)\n", failures);
        return 1;
    }
    return 0;
}
