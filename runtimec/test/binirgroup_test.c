/*
 * 组件：test/binirgroup
 * 作用：**只生产 `.bin`**——临时造一份 iops 的 bin 产物，写进 kvspace。
 *
 *   binirgroup_test <dsn>
 *
 * 固定写到 `/lib/test/iops.bin`：这是 v9 的产物 key 形态（`/lib/<pkg>·<func>.bin`）。
 * 夹具按 benchmark/cases/iops/iops.kv 的循环形状摆成线性指令流；opid 取自
 * `runtimec/binir/runtimec_v9_binir_opids.yml`，临时硬编码在下面。
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
#include <kvlang/binir.h>

#include "binir/print.h"

/* 产物 key：`/lib/<pkg>·<func>.bin`。 */
#define BINIRGROUP_OUTPUT_KEY "/lib/test/iops.bin"

/* opid 取自 runtimec_v9_binir_opids.yml（临时硬编码，v9 落地后删掉）。 */
#define BINIR_OPID_ADD_INT64 37U
#define BINIR_OPID_LE_INT64 1005U
#define BINIR_OPID_MEMCPY 1810U
#define BINIR_OPID_RETURN 1817U
#define BINIR_OPID_GOTO 1818U
#define BINIR_OPID_BRANCH 1819U

/* 占位操作数：等 v9 的操作数形态定下来，替换成立即数/槽下标/静态符号/常量下标。 */
#define PLACEHOLDER_I 0xB1U
#define PLACEHOLDER_N 0xB2U
#define PLACEHOLDER_ONE 0xB3U
#define PLACEHOLDER_A 0xB4U
#define PLACEHOLDER_TMP 0xB5U
#define PLACEHOLDER_EIGHT 0xB6U
#define PLACEHOLDER_COND 0xB7U

/*
 * 夹具：iops 循环体的线性摆法。bin 层不再区分读写参，也不再需要跳字面量区：
 *
 *   [op: le_int64][i][n][cond]
 *   [op: branch][cond][taken][not_taken]
 *   [op: add_int64][a][one][tmp]
 *   [op: C·memcpy][tmp][byte_count][a]
 *   [op: add_int64][i][one][i]
 *   [op: return]
 *
 * 每条指令 = 1 个 op 字 + `payload_count` 个 payload 格；下一条 op 字在
 * `cell + 1 + payload_count`。payload 语义由 opcode 完全确定（生成表），
 * 当前都放占位值（0xB1 起），等 v9 的真实操作数形态落地再替换。
 */
enum { ENTRY_CELL = 0 };

static const binir_t FIXTURE[] = {
    {.opcode = BINIR_OPID_LE_INT64, .payload_count = 3, .kvpc_adder = 1},
    {.opcode = PLACEHOLDER_I},
    {.opcode = PLACEHOLDER_N},
    {.opcode = PLACEHOLDER_COND},
    {.opcode = BINIR_OPID_BRANCH, .payload_count = 3, .kvpc_adder = 1},
    {.opcode = PLACEHOLDER_COND},
    {.opcode = 8U},
    {.opcode = 20U},
    {.opcode = BINIR_OPID_ADD_INT64, .payload_count = 3, .kvpc_adder = 1},
    {.opcode = PLACEHOLDER_A},
    {.opcode = PLACEHOLDER_ONE},
    {.opcode = PLACEHOLDER_TMP},
    {.opcode = BINIR_OPID_MEMCPY, .payload_count = 3, .kvpc_adder = 1},
    {.opcode = PLACEHOLDER_TMP},
    {.opcode = PLACEHOLDER_EIGHT},
    {.opcode = PLACEHOLDER_A},
    {.opcode = BINIR_OPID_ADD_INT64, .payload_count = 3, .kvpc_adder = 1},
    {.opcode = PLACEHOLDER_I},
    {.opcode = PLACEHOLDER_ONE},
    {.opcode = PLACEHOLDER_I},
    {.opcode = BINIR_OPID_RETURN, .payload_count = 0, .kvpc_adder = 1},
};

static void require(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

int main(int argc, char **argv) {
    require(argc == 2, "usage: binirgroup_test <dsn>");
    void *kvspace = kvspaceConnect(argv[1]);
    if (kvspace == NULL) {
        fprintf(stderr, "FAIL: cannot connect %s\n", argv[1]);
        return 1;
    }

    const uint64_t body_length =
        (uint64_t)(sizeof(FIXTURE) / sizeof(FIXTURE[0])) * BINIR_CELL_SIZE;
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
    if (kvspaceRealloc(kvspace, BINIRGROUP_OUTPUT_KEY, body_length,
                       attr_length,
                       &value_offset, error, (uint32_t)sizeof(error)) != 0) {
        fprintf(stderr, "FAIL: cannot write %s: %s\n",
                BINIRGROUP_OUTPUT_KEY, error);
        return 1;
    }
    uint8_t *base = kvspaceXvalueInProcessAddress(kvspace);
    require(base != NULL, "kvspace has no process address");
    uint8_t *body = base + value_offset + KVSPACE_XVALUE_HEAD_LEN;
    require(((uintptr_t)body & (BINIR_CELL_SIZE - 1U)) == 0U,
            "body must be 8-byte aligned");
    memcpy(body, FIXTURE, (size_t)body_length);

    uint8_t *attr = body + body_length;
    attr[0] = (uint8_t)KVLANG_XVALUE_ATTR_TAG_LANGTYPE;
    attr[1] = (uint8_t)(langtype_length & 0xFF);
    attr[2] = (uint8_t)((langtype_length >> 8) & 0xFF);
    memcpy(attr + 3, langtype, (size_t)langtype_length);

    printf("wrote %s: cells=%zu bytes=%llu langtype=%s\n",
           BINIRGROUP_OUTPUT_KEY,
           sizeof(FIXTURE) / sizeof(FIXTURE[0]),
           (unsigned long long)body_length, langtype);
    const binir_group_t written = {
        .cells = (const binir_t *)(const void *)body,
        .cell_count = (uint32_t)(sizeof(FIXTURE) / sizeof(FIXTURE[0])),
        .entry_cell = (uint32_t)ENTRY_CELL, /* s0=0 的 le 的 op 字 */
    };
    binir_group_print(stdout, &written);
    kvspaceClose(kvspace);
    return 0;
}
