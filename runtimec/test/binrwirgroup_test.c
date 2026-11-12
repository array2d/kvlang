/*
 * 组件：test/binrwirgroup
 * 作用：**只生产 `.bin`**——临时造一份 iops 的 bin 产物，写进 kvspace。
 *
 *   binrwirgroup_test <dsn>
 *
 * 固定写到 `/lib/test/iops.bin`：这是 v9 的产物 key 形态（`/lib/<pkg>·<func>.bin`）。
 * 夹具按 benchmark/cases/iops/iops.kv 的循环形状摆格：
 *
 *   s0=0 le(i, n) -> cond
 *   s0=1 branch(cond, 2, 6)
 *   s0=2 add(a, one) -> tmp
 *   s0=3 C·memcpy(tmp, 8) -> a
 *   s0=4 add(i, one) -> i
 *   s0=5 goto 0
 *   s0=6 return
 *
 * 操作数格目前是**占位值**（0xB1 起）：v9 的操作数形态（立即数/槽下标/静态符号/
 * 常量下标/分支 s0）还没定，等定下来再换成真实编码。opid 取自
 * `runtimec/binrwir/runtimec_v9_binrwir_opids.yml`，临时硬编码在下面。
 *
 * 产出遵守 639 §5.1：body = 8 字节 × N（N 为格数），langtype 记在 attr 的
 * tag 1 记录里，写作 `[8N]uint8`（8N 是总字节数）。不执行、不断言、不计时——
 * 执行侧由解释器消费这个 key。
 */

#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kvspace/kvspace.h>
#include <kvspace/kvspace_xvalue.h>
#include <kvlangxvalue.h>
#include <kvlang/binrwir.h>

#include "binrwir/print.h"

/* 产物 key：`/lib/<pkg>·<func>.bin`。 */
#define BINRWIRGROUP_OUTPUT_KEY "/lib/test/iops.bin"

/* opid 取自 runtimec_v9_binrwir_opids.yml（临时硬编码，v9 落地后删掉）。 */
#define BINRWIR_OPID_ADD_INT64 37U
#define BINRWIR_OPID_LE_INT64 1005U
#define BINRWIR_OPID_MEMCPY 1810U
#define BINRWIR_OPID_RETURN 1817U
#define BINRWIR_OPID_GOTO 1818U
#define BINRWIR_OPID_BRANCH 1819U

/* 占位操作数：等 v9 的操作数形态定下来，替换成立即数/槽下标/静态符号/常量下标。 */
#define PLACEHOLDER_I 0xB1U
#define PLACEHOLDER_N 0xB2U
#define PLACEHOLDER_ONE 0xB3U
#define PLACEHOLDER_A 0xB4U
#define PLACEHOLDER_TMP 0xB5U
#define PLACEHOLDER_EIGHT 0xB6U
#define PLACEHOLDER_COND 0xB7U

/*
 * 夹具：iops 循环体。产物布局（639）：
 *
 *   cell 0        goto：next_distance = 入口指令格，跳过字面量区
 *   cell 1..n     字面量区：key 文本、常量、立即数的原始字节
 *   cell n+1..    指令流：读格在前、op 字居中、写格在后
 *
 * `next_distance` 从本 op 字数到下一个 op 字（单位 8B 格）。
 * 分支/跳转的操作数是**指令轴 s0**，载入期重定位成格下标。
 */
enum { LITERAL_CELLS = 3, ENTRY_CELL = 1 + LITERAL_CELLS + 2 };

static const binrwir_t FIXTURE[] = {
    /* cell 0：跳过字面量区，落到入口指令的 op 字。 */
    {.opcode = BINRWIR_OPID_GOTO, .read_count = 0, .write_count = 0,
     .next_distance = ENTRY_CELL},
    /* 字面量区：0、1、8（当前按整值放在格里；key 文本将来按字节铺开）。 */
    {.opcode = 0U},
    {.opcode = 1U},
    {.opcode = 8U},
    /* s0=0 le(i, n) -> cond */
    {.opcode = PLACEHOLDER_I},
    {.opcode = PLACEHOLDER_N},
    {.opcode = BINRWIR_OPID_LE_INT64, .read_count = 2, .write_count = 1,
     .next_distance = 5},
    {.opcode = PLACEHOLDER_COND},
    /* s0=1 branch(cond, 2, 6) —— 读 3 格：cond + 两个 s0 目标 */
    {.opcode = PLACEHOLDER_COND},
    {.opcode = 2U}, /* taken_s0：进入循环体 */
    {.opcode = 6U}, /* not_taken_s0：去 return */
    {.opcode = BINRWIR_OPID_BRANCH, .read_count = 3, .write_count = 0,
     .next_distance = 3},
    /* s0=2 add(a, one) -> tmp；one 是字面量区的 cell 2 */
    {.opcode = PLACEHOLDER_A},
    {.opcode = 2U},
    {.opcode = BINRWIR_OPID_ADD_INT64, .read_count = 2, .write_count = 1,
     .next_distance = 4},
    {.opcode = PLACEHOLDER_TMP},
    /* s0=3 C·memcpy(tmp, 8) -> a；8 是字面量区的 cell 3 */
    {.opcode = PLACEHOLDER_TMP},
    {.opcode = 3U},
    {.opcode = BINRWIR_OPID_MEMCPY, .read_count = 2, .write_count = 1,
     .next_distance = 4},
    {.opcode = PLACEHOLDER_A},
    /* s0=4 add(i, one) -> i */
    {.opcode = PLACEHOLDER_I},
    {.opcode = 2U},
    {.opcode = BINRWIR_OPID_ADD_INT64, .read_count = 2, .write_count = 1,
     .next_distance = 3},
    {.opcode = PLACEHOLDER_I},
    /* s0=5 goto 0 —— 读 1 格：目标 s0 */
    {.opcode = 0U},
    {.opcode = BINRWIR_OPID_GOTO, .read_count = 1, .write_count = 0,
     .next_distance = 1},
    /* s0=6 return */
    {.opcode = BINRWIR_OPID_RETURN, .read_count = 0, .write_count = 0,
     .next_distance = 1},
};

static void require(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

int main(int argc, char **argv) {
    require(argc == 2, "usage: binrwirgroup_test <dsn>");
    void *kvspace = kvspaceConnect(argv[1]);
    if (kvspace == NULL) {
        fprintf(stderr, "FAIL: cannot connect %s\n", argv[1]);
        return 1;
    }

    const uint64_t body_length =
        (uint64_t)(sizeof(FIXTURE) / sizeof(FIXTURE[0])) * BINRWIR_CELL_SIZE;
    char langtype[32];
    const int langtype_length =
        snprintf(langtype, sizeof(langtype), "[%llu]uint8",
                 (unsigned long long)body_length);
    require(langtype_length > 0 &&
                (size_t)langtype_length < sizeof(langtype),
            "langtype text does not fit");

    /* attr 记录：`[tag:u8][len:u16 LE][data]`，langtype 的 tag 是 1。 */
    const uint16_t attr_length =
        (uint16_t)(3U + (uint16_t)langtype_length);
    uint64_t value_offset = 0U;
    char error[128];
    error[0] = '\0';
    if (kvspaceRealloc(kvspace, BINRWIRGROUP_OUTPUT_KEY, body_length,
                       attr_length,
                       &value_offset, error, (uint32_t)sizeof(error)) != 0) {
        fprintf(stderr, "FAIL: cannot write %s: %s\n",
                BINRWIRGROUP_OUTPUT_KEY, error);
        return 1;
    }
    uint8_t *base = kvspaceXvalueInProcessAddress(kvspace);
    require(base != NULL, "kvspace has no process address");
    uint8_t *body = base + value_offset + KVSPACE_XVALUE_HEAD_LEN;
    require(((uintptr_t)body & (BINRWIR_CELL_SIZE - 1U)) == 0U,
            "body must be 8-byte aligned");
    memcpy(body, FIXTURE, (size_t)body_length);

    uint8_t *attr = body + body_length;
    attr[0] = (uint8_t)KVLANG_XVALUE_ATTR_TAG_LANGTYPE;
    attr[1] = (uint8_t)(langtype_length & 0xFF);
    attr[2] = (uint8_t)((langtype_length >> 8) & 0xFF);
    memcpy(attr + 3, langtype, (size_t)langtype_length);

    printf("wrote %s: cells=%zu bytes=%llu langtype=%s\n",
           BINRWIRGROUP_OUTPUT_KEY,
           sizeof(FIXTURE) / sizeof(FIXTURE[0]),
           (unsigned long long)body_length, langtype);
    const binrwir_group_t written = {
        .cells = (const binrwir_t *)(const void *)body,
        .cell_count = (uint32_t)(sizeof(FIXTURE) / sizeof(FIXTURE[0])),
        .entry_cell = (uint32_t)ENTRY_CELL, /* s0=0 的 le 的 op 字 */
    };
    binrwir_group_print(stdout, &written);
    kvspaceClose(kvspace);
    return 0;
}
