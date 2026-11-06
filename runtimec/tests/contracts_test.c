/* Freeze only compact cross-component contracts selected by the design. */

#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <kvlang/runtimec.h>

#include "binrwir/argument.h"
#include "binrwir/execution_exit.h"
#include "binrwir/execution_frame.h"
#include "binrwir/instruction.h"
#include "binrwir/program.h"
#include "binrwir/signature.h"
#include "contract/kvspace_access.h"
#include "kvspace/xvalue_head.h"

_Static_assert(RUNTIMEC_STATUS_SUCCESS == 0, "success must remain zero");
_Static_assert(RUNTIMEC_STATUS_YIELD == 1, "yield status changed");
_Static_assert(RUNTIMEC_STATUS_WAIT == 2, "wait status changed");
_Static_assert(RUNTIMEC_STATUS_UNSUPPORTED == 3, "unsupported status changed");
_Static_assert(RUNTIMEC_STATUS_NOT_IMPLEMENTED == 4,
               "not-implemented status changed");
_Static_assert(RUNTIMEC_STATUS_INVALID == 5, "invalid status changed");
_Static_assert(RUNTIMEC_STATUS_OUT_OF_MEMORY == 6,
               "out-of-memory status changed");
_Static_assert(RUNTIMEC_STATUS_ERROR == 7, "error status changed");
_Static_assert(RUNTIMEC_STATUS_INSUFFICIENT_CAPACITY == 8,
               "insufficient-capacity status changed");

_Static_assert(RUNTIMEC_XVALUE_BODY_OFFSET_BYTES == 16U,
               "V.body must start sixteen bytes after the XValue base");
_Static_assert(sizeof(runtimec_xvalue_address_t) == 8U,
               "XValue addresses must remain 64 bits");
_Static_assert(sizeof(runtimec_value_body_address_t) == 8U,
               "V.body addresses must remain 64 bits");
_Static_assert(sizeof(binrwir_argument_t) == 16U,
               "BinRWIR arguments must remain sixteen bytes");
_Static_assert(offsetof(binrwir_argument_t, argument_index) == 0U,
               "BinRWIR argidx offset changed");
_Static_assert(offsetof(binrwir_argument_t, reference_kind) == 4U,
               "BinRWIR reference kind offset changed");
_Static_assert(offsetof(binrwir_argument_t, dereference) == 5U,
               "BinRWIR reserved byte offset changed");
_Static_assert(offsetof(binrwir_argument_t, slot) == 6U,
               "BinRWIR slot offset changed");
_Static_assert(offsetof(binrwir_argument_t, payload) == 8U,
               "BinRWIR payload offset changed");
_Static_assert(BINRWIR_BINARY_INTERFACE_GENERATION == 6U,
               "BinRWIR binary interface generation must be 6");
_Static_assert(sizeof(binrwir_alias_rule_t) == 12U,
               "alias rules must remain twelve bytes");
_Static_assert(sizeof(binrwir_instruction_t) == 2U,
               "BinRWIR instruction heads must contain only a 16-bit opid");
_Static_assert(offsetof(binrwir_instruction_t, operation_identifier) == 0U,
               "BinRWIR opid offset changed");
_Static_assert(sizeof(binrwir_program_identity_t) == 32U,
               "program identities must remain thirty-two bytes");
_Static_assert(sizeof(binrwir_execution_exit_t) == 24U,
               "structured exits must contain only control state");

static int failures;

#define RUNTIMEC_EXPECT(condition)                                                \
    do {                                                                          \
        if (!(condition)) {                                                        \
            ++failures;                                                            \
        }                                                                          \
    } while (0)

static void test_plain_data_copy_round_trip(void) {
    const binrwir_argument_t initial_argument = {
        .argument_index = 7U,
        .reference_kind = BINRWIR_REFERENCE_STATIC,
        .dereference = 0U,
        .slot = 0U,
        .payload = UINT64_C(0x1000),
    };
    const binrwir_execution_exit_t initial_exit = {0};
    binrwir_argument_t copied_argument;
    binrwir_execution_exit_t copied_exit;

    memcpy(&copied_argument, &initial_argument, sizeof(copied_argument));
    memcpy(&copied_exit, &initial_exit, sizeof(copied_exit));
    RUNTIMEC_EXPECT(memcmp(&copied_argument, &initial_argument,
                           sizeof(copied_argument)) == 0);
    RUNTIMEC_EXPECT(memcmp(&copied_exit, &initial_exit,
                           sizeof(copied_exit)) == 0);
}

/* 构造失败必须清空输出；STATIC 的 payload 允许 NONE（写目标在编码期可未解析）。 */
static void test_reference_argument_failure_shape(void) {
    binrwir_argument_t argument = {
        .argument_index = UINT32_MAX,
        .reference_kind = UINT8_MAX,
        .dereference = UINT8_MAX,
        .slot = UINT16_MAX,
        .payload = UINT64_MAX,
    };

    /*
     * **2026-11-05 语义放宽**：STATIC + payload NONE 结构合法——写目标的 key 允许
     * 在编码期尚不存在，地址由执行期按符号池里的 key 解析/创建。读目标缺值是
     * 真实错误，由 vmbin 值访问层拒绝（见 `runtimec.engine.absolute_write`）。
     * 形状层只回答字段是否自洽，不回答语义是否可执行。
     */
    RUNTIMEC_EXPECT(binrwir_argument_create(
        3U, BINRWIR_REFERENCE_STATIC, 0U, BINRWIR_BODY_ADDRESS_NONE,
        &argument) == RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(argument.argument_index == 3U);
    RUNTIMEC_EXPECT(argument.reference_kind == BINRWIR_REFERENCE_STATIC);
    RUNTIMEC_EXPECT(argument.dereference == 0U);
    RUNTIMEC_EXPECT(argument.slot == 0U);
    RUNTIMEC_EXPECT(argument.payload == BINRWIR_BODY_ADDRESS_NONE);

    RUNTIMEC_EXPECT(binrwir_argument_create(
        BINRWIR_ARGUMENT_INDEX_NONE, BINRWIR_REFERENCE_PARAMETER_READ, 0U, 0U,
        &argument) == RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(argument.argument_index == 0U);

    /* 参数引用带 payload 视为把 payload 当地址用，必须拒绝。 */
    RUNTIMEC_EXPECT(binrwir_argument_create(
        3U, BINRWIR_REFERENCE_PARAMETER_READ, 0U, UINT64_C(0x1008),
        &argument) == RUNTIMEC_STATUS_INVALID);
    RUNTIMEC_EXPECT(argument.argument_index == 0U);

    RUNTIMEC_EXPECT(binrwir_argument_create(
        3U, BINRWIR_REFERENCE_PARAMETER_READ, 1U, 0U, &argument) ==
                    RUNTIMEC_STATUS_SUCCESS);
    RUNTIMEC_EXPECT(binrwir_argument_validate(&argument) ==
                    RUNTIMEC_STATUS_SUCCESS);

    argument.argument_index = BINRWIR_ARGUMENT_INDEX_NONE;
    RUNTIMEC_EXPECT(binrwir_argument_validate(&argument) ==
                    RUNTIMEC_STATUS_INVALID);

    argument.argument_index = 3U;
    argument.reference_kind = BINRWIR_REFERENCE_INVALID;
    RUNTIMEC_EXPECT(binrwir_argument_validate(&argument) ==
                    RUNTIMEC_STATUS_INVALID);

    argument.reference_kind = BINRWIR_REFERENCE_STATIC;
    argument.slot = 0U;
    argument.payload = UINT64_C(0x1008);
    RUNTIMEC_EXPECT(binrwir_argument_validate(&argument) ==
                    RUNTIMEC_STATUS_SUCCESS);

    argument.dereference = 2U;  /* 0/1 之外的值才非法 */
    RUNTIMEC_EXPECT(binrwir_argument_validate(&argument) ==
                    RUNTIMEC_STATUS_INVALID);
}

static void test_instruction_argument_count_overflow(void) {
    binrwir_instruction_build_request_t request = {
        .operation_identifier = 1U,
        .read_arguments = {.data = NULL, .count = UINT32_MAX},
        .write_arguments = {.data = NULL, .count = 1U},
    };

    RUNTIMEC_EXPECT(binrwir_instruction_validate_build_request(&request) ==
                    RUNTIMEC_STATUS_INVALID);
}

/*
 * opid 0 必须被拒。catalog 里没有任何一行分配 0，`0x0001` 起才是能力行。
 *
 * 为什么单列一条：0 是结构体 `memset(0)` 后的默认值，最容易因漏填被带进
 * Program；而读侧 `vmbin` 把 0 当非法指令头。写侧不拦、读侧才拦，错误会离
 * 出错点很远（表现为执行期的 detail=3，而不是编码期的拒绝）。
 */
static void test_instruction_rejects_zero_operation_identifier(void) {
    binrwir_instruction_build_request_t request = {
        .source_logical_program_counter = 1U,
        .operation_identifier = 0U,
        .read_arguments = {.data = NULL, .count = 0U},
        .write_arguments = {.data = NULL, .count = 0U},
    };

    RUNTIMEC_EXPECT(binrwir_instruction_validate_build_request(&request) ==
                    RUNTIMEC_STATUS_INVALID);

    /* 0x0001 起是合法能力行：这条判据不得误伤真实算子。 */
    request.operation_identifier = UINT16_C(0x0001);
    RUNTIMEC_EXPECT(binrwir_instruction_validate_build_request(&request) ==
                    RUNTIMEC_STATUS_SUCCESS);

    /* FOREIGN 仍是合法结构标记（零读写参），不因这条判据被拒。 */
    request.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_FOREIGN;
    RUNTIMEC_EXPECT(binrwir_instruction_validate_build_request(&request) ==
                    RUNTIMEC_STATUS_SUCCESS);

    /* NONE 仍被拒（它本来就在守卫里，这条防止改判据时把它弄丢）。 */
    request.operation_identifier = BINRWIR_OPERATION_IDENTIFIER_NONE;
    RUNTIMEC_EXPECT(binrwir_instruction_validate_build_request(&request) ==
                    RUNTIMEC_STATUS_INVALID);
}

/* V.body 地址推导是全系统唯一的 +8：零基址与溢出都必须失败，不回绕。 */
static void test_xvalue_body_address(void) {
    RUNTIMEC_EXPECT(runtimec_xvalue_body_address(RUNTIMEC_XVALUE_ADDRESS_NONE) ==
                    RUNTIMEC_VALUE_BODY_ADDRESS_NONE);
    RUNTIMEC_EXPECT(runtimec_xvalue_body_address(UINT64_C(0x1000)) ==
                    UINT64_C(0x1000) +
                        RUNTIMEC_XVALUE_BODY_OFFSET_BYTES);
    RUNTIMEC_EXPECT(runtimec_xvalue_body_address(UINT64_MAX) ==
                    RUNTIMEC_VALUE_BODY_ADDRESS_NONE);
    RUNTIMEC_EXPECT(runtimec_xvalue_body_address(
                        UINT64_MAX - RUNTIMEC_XVALUE_BODY_OFFSET_BYTES + 1U) ==
                    RUNTIMEC_VALUE_BODY_ADDRESS_NONE);
    /* 最大可表示基址：+8 恰好落在 UINT64_MAX，仍合法。 */
    RUNTIMEC_EXPECT(runtimec_xvalue_body_address(
                        UINT64_MAX - RUNTIMEC_XVALUE_BODY_OFFSET_BYTES) ==
                    UINT64_MAX);
}

/* 逆运算：从值体地址读回长度；零地址、下溢、坏 layout/padding 都返回 0。 */
static void test_value_body_length(void) {
    uint8_t storage[KVSPACE_XVALUE_HEAD_LEN + 8U];
    memset(storage, 0, sizeof(storage));
    kvspaceXvalueHead head;
    memset(&head, 0, sizeof(head));
    kvspaceXvalueHeadSetLayout(&head, KVSPACE_XVALUE_LAYOUT_LENGTH);
    kvspaceXvalueHeadSetLen(&head, 8U);
    memcpy(storage, &head, sizeof(head));
    const runtimec_value_body_address_t body =
        (runtimec_value_body_address_t)(uintptr_t)storage +
        RUNTIMEC_XVALUE_BODY_OFFSET_BYTES;
    RUNTIMEC_EXPECT(runtimec_value_body_length(body) == 8U);

    /* 零地址与下溢：不能算出长度。 */
    RUNTIMEC_EXPECT(runtimec_value_body_length(RUNTIMEC_VALUE_BODY_ADDRESS_NONE) ==
                    0U);
    RUNTIMEC_EXPECT(runtimec_value_body_length(
                        RUNTIMEC_XVALUE_BODY_OFFSET_BYTES - 1U) == 0U);

    /* layout 不可信（保留值）：地址前面不是自洽的值头。偏移经 offsetof，不写死。 */
    storage[offsetof(kvspaceXvalueHead, layout)] = 0x7fU;
    RUNTIMEC_EXPECT(runtimec_value_body_length(body) == 0U);
    storage[offsetof(kvspaceXvalueHead, layout)] = KVSPACE_XVALUE_LAYOUT_LENGTH;
    RUNTIMEC_EXPECT(runtimec_value_body_length(body) == 8U);

    /* padding 非零：ABI 校验，读路径必须先过这一关。 */
    storage[offsetof(kvspaceXvalueHead, padding)] = 1U;
    RUNTIMEC_EXPECT(runtimec_value_body_length(body) == 0U);
    storage[offsetof(kvspaceXvalueHead, padding)] = 0U;
    RUNTIMEC_EXPECT(runtimec_value_body_length(body) == 8U);

    /* 与 +8 互逆：基址 → 值体 → 长度，长度就是值头声明的 len。 */
    const runtimec_xvalue_address_t base = (runtimec_xvalue_address_t)(uintptr_t)storage;
    RUNTIMEC_EXPECT(runtimec_value_body_length(
                        runtimec_xvalue_body_address(base)) == 8U);
}

/* 逆入口：值体地址退回首部即基址；零地址与下溢都失败，绝不回绕到高地址。 */
static void test_xvalue_address_from_value_body(void) {
    RUNTIMEC_EXPECT(runtimec_value_body_xvalue_address(
                        RUNTIMEC_VALUE_BODY_ADDRESS_NONE) ==
                    RUNTIMEC_XVALUE_ADDRESS_NONE);
    RUNTIMEC_EXPECT(runtimec_value_body_xvalue_address(
                        RUNTIMEC_XVALUE_BODY_OFFSET_BYTES - 1U) ==
                    RUNTIMEC_XVALUE_ADDRESS_NONE);
    RUNTIMEC_EXPECT(runtimec_value_body_xvalue_address(
                        RUNTIMEC_XVALUE_BODY_OFFSET_BYTES) ==
                    RUNTIMEC_XVALUE_ADDRESS_NONE);
    RUNTIMEC_EXPECT(runtimec_value_body_xvalue_address(
                        UINT64_C(0x1000) +
                        RUNTIMEC_XVALUE_BODY_OFFSET_BYTES) ==
                    UINT64_C(0x1000));

    /* 与 +8 互逆：基址 → 值体 → 基址回到原值。 */
    const runtimec_xvalue_address_t base = UINT64_C(0x2000);
    RUNTIMEC_EXPECT(runtimec_value_body_xvalue_address(
                        runtimec_xvalue_body_address(base)) == base);
}


/*
 * 16 字节布局的常驻证据。四条核对逐字段证明 16 是唯一正确值：
 *   1. bodylen  在偏移 0，u64 小端，经 kvspaceXvalueHeadLen 反读回原值；
 *   2. attrslen 在偏移 8，u32 小端；
 *   3. storetype@12、ptr@13 在原位；
 *   4. padding@14-15 全零，且总长 == 16 + bodylen + attrslen。
 * 将来谁再动 head，这条会先红。
 */
static void test_xvalue_head_sixteen_byte_layout(void) {
    kvspaceXvalueHead head;

    memset(&head, 0, sizeof(head));
    kvspaceXvalueHeadSetLen(&head, 8U);
    kvspaceXvalueHeadSetAttrsLen(&head, 8U);
    kvspaceXvalueHeadSetLayout(&head, KVSPACE_XVALUE_LAYOUT_CAPACITY);

    RUNTIMEC_EXPECT(offsetof(kvspaceXvalueHead, bodylen) == 0U);
    RUNTIMEC_EXPECT(offsetof(kvspaceXvalueHead, attrslen) == 8U);
    RUNTIMEC_EXPECT(offsetof(kvspaceXvalueHead, layout) == 12U);
    /* padding 是 3 字节，从 13 起——head 只描述字节布局，没有 storetype/ptk 字段了。 */
    RUNTIMEC_EXPECT(offsetof(kvspaceXvalueHead, padding) == 13U);
    RUNTIMEC_EXPECT(sizeof(kvspaceXvalueHead) == 16U);
    RUNTIMEC_EXPECT(KVSPACE_XVALUE_HEAD_LEN == 16U);
    RUNTIMEC_EXPECT(RUNTIMEC_XVALUE_BODY_OFFSET_BYTES == KVSPACE_XVALUE_HEAD_LEN);

    /* 直接看字节：bodylen 是 u64 小端、attrslen 是 u32 小端。 */
    const uint8_t *bytes = (const uint8_t *)(const void *)&head;
    RUNTIMEC_EXPECT(bytes[0] == 8U);
    for (size_t index = 1U; index < 8U; ++index) {
        RUNTIMEC_EXPECT(bytes[index] == 0U);
    }
    RUNTIMEC_EXPECT(bytes[8] == 8U);
    for (size_t index = 9U; index < 12U; ++index) {
        RUNTIMEC_EXPECT(bytes[index] == 0U);
    }
    RUNTIMEC_EXPECT(bytes[12] == KVSPACE_XVALUE_LAYOUT_CAPACITY);
    RUNTIMEC_EXPECT(bytes[14] == 0U);
    RUNTIMEC_EXPECT(bytes[15] == 0U);

    /* helper 反读必须回到原值。 */
    RUNTIMEC_EXPECT(kvspaceXvalueHeadLen(&head) == 8U);
    RUNTIMEC_EXPECT(kvspaceXvalueHeadAttrsLen(&head) == 8U);
    RUNTIMEC_EXPECT(kvspaceXvalueHeadPaddingIsZero(&head));
    RUNTIMEC_EXPECT(kvspaceXvalueHeadTotalLen(&head) ==
           (uint64_t)KVSPACE_XVALUE_HEAD_LEN + 8U + 8U);

    /* 非零 padding 必须非法：helper 返回 0（0 不是合法总长）。 */
    kvspaceXvalueHead padded = head;
    padded.padding[0] = 1U;
    RUNTIMEC_EXPECT(!kvspaceXvalueHeadPaddingIsZero(&padded));
    RUNTIMEC_EXPECT(kvspaceXvalueHeadTotalLen(&padded) == 0U);
}


/* ── storetype 4（定容量变长）矩阵 ────────────────────────────────────
 *
 * 三方各实现一份（`kvspace-durable` Rust / `kvspace-c` / `runtimec`），没有共享
 * ABI 符号，一致性只能靠「同一组输入 → 同一组输出」。这张表就是那组输入输出。
 *
 * **`runtimec_value_body_length` 没有错误通道**：畸形 wire 与「长度本来就是 0」
 * 都返回 `0`。所以第 2 组（`reallen == 0` 合法）**不能靠返回值证明** —— 它和
 * 第 3/4/6/7 组在返回值上无法区分。这一点写成显式断言（`contracts_test_varlen_ambiguity`），
 * 因为它是 J-5 之前必须补一条带状态码入口的理由；不写进测试就没人看得见。
 */

/* 造一条 storetype 4 wire：`capacity` 字节体 + 可选 tag 2（reallen）+ 可选的
 * 重复 / 畸形 / 尾部残余。数值一律从 `KVSPACE_XVALUE_*` 取，不写死 2 或 4。 */
static uint8_t *contracts_make_varlen(uint64_t capacity, int64_t reallen,
                                      int duplicate, int malformed_length,
                                      size_t trailing_bytes) {
    /* 一条 tag 2 记录 = tag(1) + 长度(2) + 值(8) = 11；畸形那条只占 3+4=7。 */
    const size_t record_size = 3U + 8U;
    const size_t malformed_size = 3U + 4U;
    uint64_t attrs_length = 0U;
    if (reallen >= 0) {
        attrs_length += malformed_length ? malformed_size : record_size;
    }
    if (duplicate) {
        attrs_length += record_size;
    }
    attrs_length += trailing_bytes;
    const uint64_t total =
        KVSPACE_XVALUE_HEAD_LEN + capacity + attrs_length;
    uint8_t *wire = (uint8_t *)calloc(1, (size_t)total);

    if (wire == NULL) {
        return NULL;
    }
    kvspaceXvalueHead head;
    memset(&head, 0, sizeof(head));
    kvspaceXvalueHeadSetLayout(&head, KVSPACE_XVALUE_LAYOUT_CAPACITY);
    kvspaceXvalueHeadSetLen(&head, capacity);
    kvspaceXvalueHeadSetAttrsLen(&head, (uint32_t)attrs_length);
    memcpy(wire, &head, sizeof(head));
    uint8_t *attrs = wire + KVSPACE_XVALUE_HEAD_LEN + capacity;
    if (reallen >= 0) {
        attrs[0] = KVSPACE_XVALUE_ATTR_TAG_REALLEN;
        attrs[1] = (uint8_t)(malformed_length ? 4U : 8U);
        attrs[2] = 0U;
        if (!malformed_length) {
            for (size_t index = 0U; index < 8U; ++index) {
                attrs[3U + index] = (uint8_t)((uint64_t)reallen >> (8U * index));
            }
        }
        attrs += malformed_length ? malformed_size : record_size;
    }
    if (duplicate) {
        attrs[0] = KVSPACE_XVALUE_ATTR_TAG_REALLEN;
        attrs[1] = 8U;
        attrs[2] = 0U;
        for (size_t index = 0U; index < 8U; ++index) {
            attrs[3U + index] = (uint8_t)((uint64_t)reallen >> (8U * index));
        }
        attrs += record_size;
    }
    /* 尾部残余：填非零垃圾，证明它**被忽略**而不是恰好是零。 */
    for (size_t index = 0U; index < trailing_bytes; ++index) {
        attrs[index] = 0xABU;
    }
    return wire;
}

static runtimec_value_body_address_t contracts_varlen_body(const uint8_t *wire) {
    return (runtimec_value_body_address_t)(uintptr_t)wire +
           RUNTIMEC_XVALUE_BODY_OFFSET_BYTES;
}

/* 1 闭环：capacity=32、reallen=5 → 5。 */
static void contracts_test_varlen_reallen_below_capacity(void) {
    uint8_t *wire = contracts_make_varlen(32U, 5, 0, 0, 0U);
    RUNTIMEC_EXPECT(wire != NULL);
    RUNTIMEC_EXPECT(runtimec_value_body_length(contracts_varlen_body(wire)) ==
                    5U);
    free(wire);
}

/* 1b reallen == 容量：满了，没有余量 → 容量值。 */
static void contracts_test_varlen_reallen_equals_capacity(void) {
    uint8_t *wire = contracts_make_varlen(16U, 16, 0, 0, 0U);
    RUNTIMEC_EXPECT(wire != NULL);
    RUNTIMEC_EXPECT(runtimec_value_body_length(contracts_varlen_body(wire)) ==
                    16U);
    free(wire);
}

/* 2 reallen == 0：**合法空值**（容量已分配、无内容）→ 0。 */
static void contracts_test_varlen_reallen_zero_is_legal(void) {
    uint8_t *wire = contracts_make_varlen(16U, 0, 0, 0, 0U);
    RUNTIMEC_EXPECT(wire != NULL);
    RUNTIMEC_EXPECT(runtimec_value_body_length(contracts_varlen_body(wire)) ==
                    0U);
    free(wire);
}

/* 3 缺 tag 2 → 不可信 → 0。 */
static void contracts_test_varlen_missing_tag(void) {
    uint8_t *wire = contracts_make_varlen(8U, -1, 0, 0, 0U);
    RUNTIMEC_EXPECT(wire != NULL);
    RUNTIMEC_EXPECT(runtimec_value_body_length(contracts_varlen_body(wire)) ==
                    0U);
    free(wire);
}

/* 4 reallen > 容量 → 0。 */
static void contracts_test_varlen_reallen_over_capacity(void) {
    uint8_t *wire = contracts_make_varlen(8U, 9, 0, 0, 0U);
    RUNTIMEC_EXPECT(wire != NULL);
    RUNTIMEC_EXPECT(runtimec_value_body_length(contracts_varlen_body(wire)) ==
                    0U);
    free(wire);
}

/* 6 重复 tag 2 → 两个事实源 → 0。 */
static void contracts_test_varlen_duplicate_tag(void) {
    uint8_t *wire = contracts_make_varlen(8U, 3, 1, 0, 0U);
    RUNTIMEC_EXPECT(wire != NULL);
    RUNTIMEC_EXPECT(runtimec_value_body_length(contracts_varlen_body(wire)) ==
                    0U);
    free(wire);
}

/* 7 tag 2 记录长度 ≠ 8 → 畸形 → 0。 */
static void contracts_test_varlen_malformed_record_length(void) {
    uint8_t *wire = contracts_make_varlen(8U, 3, 0, 1, 0U);
    RUNTIMEC_EXPECT(wire != NULL);
    RUNTIMEC_EXPECT(runtimec_value_body_length(contracts_varlen_body(wire)) ==
                    0U);
    free(wire);
}

/* 8 尾部残余不足一条记录（2 字节）：**必须被忽略**，不改变结果。
 *
 * 判据是「扫描按 `left >= 3`」，所以 <3 的尾巴自然不参与。填 0xAB 而非零，
 * 是为了排除「恰好是 0 所以看起来没事」这种假绿。 */
static void contracts_test_varlen_trailing_bytes_ignored(void) {
    uint8_t *wire = contracts_make_varlen(32U, 5, 0, 0, 2U);
    RUNTIMEC_EXPECT(wire != NULL);
    RUNTIMEC_EXPECT(runtimec_value_body_length(contracts_varlen_body(wire)) ==
                    5U);
    free(wire);
}

/* 8b 非 storetype 4、无 tag 2 → 取 bodylen（与今天行为一致）。 */
static void contracts_test_non_varlen_uses_body_length(void) {
    uint8_t slack_wire[KVSPACE_XVALUE_HEAD_LEN + 8U];
    kvspaceXvalueHead head;

    memset(slack_wire, 0, sizeof(slack_wire));
    memset(&head, 0, sizeof(head));
    kvspaceXvalueHeadSetLayout(&head, KVSPACE_XVALUE_LAYOUT_LENGTH);
    kvspaceXvalueHeadSetLen(&head, 8U);
    memcpy(slack_wire, &head, sizeof(head));
    RUNTIMEC_EXPECT(runtimec_value_body_length(
                        (runtimec_value_body_address_t)(uintptr_t)slack_wire +
                        RUNTIMEC_XVALUE_BODY_OFFSET_BYTES) == 8U);
}

/*
 * `reallen == 0`（合法空值）与全部畸形组在返回值上**无法区分**——都返回 0。
 *
 * 这条不是重复上面任何一组，它断言的正是「分不出来」这件事本身。写成显式断言，
 * 是因为它是 J-5（`‥status` 成为 storetype 4 的第一个消费者）之前必须先补一条
 * 带状态码入口的理由。理由只写在头注释里没人看得见，写进测试里才有人会读。
 *
 * 若有一天本函数能区分「空」与「坏」，这条断言会**失败**——那时应当删掉它并
 * 更新 `kvspace_access.h` 的契约注释，而不是放宽它。
 */
static void contracts_test_varlen_ambiguity_is_known(void) {
    uint8_t *legal_empty = contracts_make_varlen(16U, 0, 0, 0, 0U);
    uint8_t *missing_tag = contracts_make_varlen(8U, -1, 0, 0, 0U);
    uint8_t *over_capacity = contracts_make_varlen(8U, 9, 0, 0, 0U);

    RUNTIMEC_EXPECT(legal_empty != NULL && missing_tag != NULL &&
                    over_capacity != NULL);
    const uint32_t legal_length =
        runtimec_value_body_length(contracts_varlen_body(legal_empty));
    RUNTIMEC_EXPECT(legal_length ==
                    runtimec_value_body_length(contracts_varlen_body(missing_tag)));
    RUNTIMEC_EXPECT(legal_length ==
                    runtimec_value_body_length(
                        contracts_varlen_body(over_capacity)));
    free(legal_empty);
    free(missing_tag);
    free(over_capacity);
}

/*
 * `layout=0` 却带 tag 2 —— **非法**（架构师裁决 2026-11-06）。
 *
 * 判据与 `kvspace/include/kvspace/xvalue_head.h` 的头注释一致：「tag 2 的归属是
 * **排他**的：`layout=1` 必须恰有一条、`layout=0` 必须不带」。
 *
 * 为什么严格而不是忽略：宽容解读等于把「写读双方对格式理解不一致」当作正常数据；
 * 这类畸形一旦放行，就会变成「看起来能工作」的输入。
 *
 * 这条是**反向对照**：若哪天实现改回「忽略多余 tag 2」，它会从「返回 0」变成
 * 「返回 bodylen」，断言立刻红。
 */
static void contracts_test_length_layout_rejects_reallen_tag(void) {
    /* 复用 varlen fixture（它建的是 layout=1 + tag 2），把 layout 改回 0。 */
    uint8_t *wire = contracts_make_varlen(16U, 8, 0, 0, 0U);

    RUNTIMEC_EXPECT(wire != NULL);
    if (wire == NULL) {
        return;
    }
    /* layout=1 + tag 2：合法，返回 reallen=8。 */
    RUNTIMEC_EXPECT(runtimec_value_body_length(contracts_varlen_body(wire)) == 8U);
    /* 同一份字节，只把 layout 改成 0：tag 2 变成不该有的记录 ⇒ 非法。 */
    wire[offsetof(kvspaceXvalueHead, layout)] = KVSPACE_XVALUE_LAYOUT_LENGTH;
    RUNTIMEC_EXPECT(runtimec_value_body_length(contracts_varlen_body(wire)) == 0U);
    free(wire);
}

/*
 * `layout >= 2` —— **非法**（保留值）。
 *
 * head 头注释：「layout 其余取值保留且必须为零，非零按非法处理」。这里断的是
 * 「读取方不会拿一个不认识的 layout 猜语义」。
 */
static void contracts_test_reserved_layout_is_rejected(void) {
    uint8_t storage[KVSPACE_XVALUE_HEAD_LEN + 8U];

    memset(storage, 0, sizeof(storage));
    {
        kvspaceXvalueHead head;

        memset(&head, 0, sizeof(head));
        kvspaceXvalueHeadSetLen(&head, 8U);
        kvspaceXvalueHeadSetLayout(&head, KVSPACE_XVALUE_LAYOUT_LENGTH);
        memcpy(storage, &head, sizeof(head));
    }
    {
        const runtimec_value_body_address_t body =
            (runtimec_value_body_address_t)(uintptr_t)storage +
            RUNTIMEC_XVALUE_BODY_OFFSET_BYTES;

        RUNTIMEC_EXPECT(runtimec_value_body_length(body) == 8U);
        for (uint8_t reserved = 2U; reserved <= 4U; ++reserved) {
            storage[offsetof(kvspaceXvalueHead, layout)] = reserved;
            RUNTIMEC_EXPECT(runtimec_value_body_length(body) == 0U);
        }
        /* 空 body 视图同理：`xvalue_body_from_wire` 也必须拒。 */
        storage[offsetof(kvspaceXvalueHead, layout)] = 3U;
        {
            const runtimec_string_view_t body_view =
                runtimec_xvalue_body_from_wire(
                    runtimec_bytes_view_make(storage, sizeof(storage)));

            RUNTIMEC_EXPECT(body_view.data == NULL && body_view.size == 0U);
        }
    }
}

/*
 * `*T` / `@T` 不是定宽（架构师裁决 2026-11-06）。
 *
 * 规范 `02-kvspace模型/06-XValueHead线格式.kv`：「指针**不是**定宽（空 body
 * 即 None），帧准备不物化指针局部，首次写创建」。
 *
 * 两条**必须分开验**，因为它们拒绝的理由不同（见 `runtimec_kvspace_zero_width`
 * 的判据顺序注释）：
 *   `*[2]int32` —— 指针（形状是定长的，但这个值是指针，body 是变长路径）
 *   `[*]int32`  —— 变长维（不是指针，但形状不定长）
 *
 * 若实现把顺序写反，`*[2]int32` 会先撞形状规则，**仍然返回 false**——
 * 所以仅看 false 分不出对错。这里额外钉住「基种类剥掉前缀后仍是合法标量」，
 * 说明拒绝确实发生在指针判据那一支。
 */
static void contracts_test_pointer_and_ext_are_variable_width(void) {
    static const char *const pointer_langtypes[] = {
        "*int64", "*[2]int32", "*[]int32", "@[]uint8", "@int64",
    };

    for (size_t ordinal = 0U;
         ordinal < sizeof(pointer_langtypes) / sizeof(pointer_langtypes[0]);
         ++ordinal) {
        uint64_t width = 123U;

        RUNTIMEC_EXPECT(!runtimec_kvspace_access_langtype_zero_width(
            runtimec_string_view_from_c_string(pointer_langtypes[ordinal]),
            &width));
        RUNTIMEC_EXPECT(width == 0U);
        RUNTIMEC_EXPECT(!runtimec_kvspace_access_langtype_has_fixed_zero_width(
            runtimec_string_view_from_c_string(pointer_langtypes[ordinal])));
    }
    /* 反向对照：去掉前缀后同一个类型**是**定宽——证明拒绝来自前缀判据。 */
    RUNTIMEC_EXPECT(runtimec_kvspace_access_langtype_has_fixed_zero_width(
        runtimec_string_view_from_c_string("[2]int32")));
    RUNTIMEC_EXPECT(runtimec_kvspace_access_langtype_has_fixed_zero_width(
        runtimec_string_view_from_c_string("int64")));
}

/*
 * 定宽零值宽度表的**三分类**（`runtimec/contract/kvspace_access.c` 的
 * `runtimec_kvspace_width_table`）。
 *
 * 这张表只回答一件事：**这个 langtype 的零值是几个字节**。它同时承担「帧准备该不该
 * 物化这个槽」的判据，所以三类必须分清——混成一条路径就是 p1 禁止的
 * 「一条路径表达三种语义」：
 *
 *   1. **定宽的字符标量**：`char/utf32` 是一个码位、恒 4 字节（C 的 `char32_t`）；
 *      `char/ascii` 是一个字符、恒 1 字节（C 的 `char`）。两者都必须在表里。
 *   2. **真正的变长**：`char/utf8` 是 1–4 字节的 UTF-8 序列；`[]char/utf32` 这类
 *      数组形态字符串按完整串匹配。两者都**不在**表里。
 *   3. 不认识的 langtype 同样不在表里——它与第 2 类共用 `false` 返回值，
 *      但原因不同（不知道 vs 知道它变长）。
 *
 * 测试名直接写清每一格，不叫 `misc`：这些断言是这张表的**规格**，不是抽查。
 */
static void contracts_test_character_scalars_are_fixed_width(void) {
    /* 第 1 类：定宽字符标量。 */
    RUNTIMEC_EXPECT(runtimec_kvspace_access_langtype_has_fixed_zero_width(
        runtimec_string_view_from_c_string("char/utf32")));
    RUNTIMEC_EXPECT(runtimec_kvspace_access_langtype_has_fixed_zero_width(
        runtimec_string_view_from_c_string("char/ascii")));
}

/*
 * 定长数组的零值宽度：`[N]K` 与 `[d0,d1,…]K`。
 *
 * 判据（架构师 2026-11-06 裁）：定长维 ⇒ 宽度 = 各维乘积 × 基种类宽度；
 * 变长维（`[]`/`[?]`/`[*]`/`[+]`）⇒ 不是定宽；未知基种类 ⇒ 不是定宽。
 *
 * 为什么这条重要：`vthread·pc()` 的 `result1` 是 `[2]int32`（spec
 * `05-runtime语义/02-执行模型-续.kv:26-40`）。槽不物化，第一个写就 UNBOUND_SLOT。
 */
static void contracts_test_fixed_array_zero_width(void) {
    struct {
        const char *langtype;
        uint64_t width;
    } cases[] = {
        {"[2]int32", 8U},
        {"[3]float64", 24U},
        {"[2,3]int32", 24U},
        {"[1]int8", 1U},
        {"[2][3]int32", 24U},   /* 嵌套形状逐层相乘 */
        {"[2]char/utf32", 8U},  /* 基种类是定宽字符 */
    };
    for (size_t ordinal = 0U; ordinal < sizeof(cases) / sizeof(cases[0]);
         ++ordinal) {
        uint64_t width = 0U;
        RUNTIMEC_EXPECT(runtimec_kvspace_access_langtype_zero_width(
            runtimec_string_view_from_c_string(cases[ordinal].langtype),
            &width));
        RUNTIMEC_EXPECT(width == cases[ordinal].width);
        RUNTIMEC_EXPECT(runtimec_kvspace_access_langtype_has_fixed_zero_width(
            runtimec_string_view_from_c_string(cases[ordinal].langtype)));
    }
}

/*
 * 反向对照：**变长数组不能被物化**。
 *
 * 这不是「暂未实现」——`[]int32` 的零值长度是 0，物化出一个零长槽会让第一次
 * 真实写必失败（写入要求等长）。所以它们必须走创建路径。
 */
static void contracts_test_variable_array_is_not_fixed_width(void) {
    static const char *const langtypes[] = {
        "[]int32",   /* 空维 */
        "[?]int32",  /* 问号 */
        "[*]int32",  /* 星号 */
        "[+]int32",  /* 加号 */
        "[2,]/lib/Node", /* 逗号后空 */
        "[2]/lib/Node",  /* 已知形状但基种类是容器 */
        "[]char/utf8",   /* 变长字符 */
    };
    for (size_t ordinal = 0U;
         ordinal < sizeof(langtypes) / sizeof(langtypes[0]); ++ordinal) {
        uint64_t width = 123U; /* 哨兵：失败时必须被清零 */

        RUNTIMEC_EXPECT(!runtimec_kvspace_access_langtype_zero_width(
            runtimec_string_view_from_c_string(langtypes[ordinal]), &width));
        RUNTIMEC_EXPECT(width == 0U);
        RUNTIMEC_EXPECT(!runtimec_kvspace_access_langtype_has_fixed_zero_width(
            runtimec_string_view_from_c_string(langtypes[ordinal])));
    }
}

static void contracts_test_utf8_character_is_variable_width(void) {
    /* 第 2 类：`char/utf8` 的宽度取决于内容（1–4 字节），不能进定宽表。 */
    RUNTIMEC_EXPECT(!runtimec_kvspace_access_langtype_has_fixed_zero_width(
        runtimec_string_view_from_c_string("char/utf8")));
}

static void contracts_test_character_array_is_not_fixed_width(void) {
    /* **变长**字符数组：`[]char/utf32` 的长度由内容决定，不是定宽。
     * 这一条是反向对照——若哪天把 `[]char/utf32` 也当成定宽，
     * 帧准备就会给它建一个错误的零长槽。 */
    RUNTIMEC_EXPECT(!runtimec_kvspace_access_langtype_has_fixed_zero_width(
        runtimec_string_view_from_c_string("[]char/utf32")));

    /*
     * **判据变更（2026-11-06）**：`[4]char/utf32` 从「不是定宽」改为「是定宽、16 字节」。
     *
     * 原判据是「数组一律不是定宽」，理由是数组元素个数可变。但那条理由只对
     * **变长维**（`[]`/`[?]`/`[*]`/`[+]`）成立——`[4]` 是定长维，元素个数由类型
     * 本身给出，零值宽度可算。
     *
     * 触发这条变更的是 `vthread·pc()`：它的 `result1` 是 `[2]int32`
     * （spec `05-runtime语义/02-执行模型-续.kv:26-40`），槽不物化则第一个写就
     * `UNBOUND_SLOT`。要支持它，定长数组必须物化；而既然物化，宽度就得算对。
     *
     * 这条改动是**跟上 spec**，不是放宽判据：变长的那一半仍然拒绝（见上）。
     */
    RUNTIMEC_EXPECT(runtimec_kvspace_access_langtype_has_fixed_zero_width(
        runtimec_string_view_from_c_string("[4]char/utf32")));
    {
        uint64_t width = 0U;

        RUNTIMEC_EXPECT(runtimec_kvspace_access_langtype_zero_width(
            runtimec_string_view_from_c_string("[4]char/utf32"), &width));
        RUNTIMEC_EXPECT(width == 16U);
    }
}

static void contracts_test_unknown_langtype_is_not_fixed_width(void) {
    /* 第 3 类：不认识 ⇒ 同样 false，但这是「不知道」，与第 2 类的「知道它变长」
     * 不同。共用返回值可接受，表的内容必须只由第 1 类构成。 */
    RUNTIMEC_EXPECT(!runtimec_kvspace_access_langtype_has_fixed_zero_width(
        runtimec_string_view_from_c_string("not-a-langtype")));
    RUNTIMEC_EXPECT(!runtimec_kvspace_access_langtype_has_fixed_zero_width(
        runtimec_string_view_make(NULL, 0U)));
}

int main(void) {
    kvlang_runtime_t *opaque_runtime = NULL;

    (void)opaque_runtime;
    test_plain_data_copy_round_trip();
    test_reference_argument_failure_shape();
    test_instruction_argument_count_overflow();
    test_instruction_rejects_zero_operation_identifier();
    test_xvalue_body_address();
    test_value_body_length();
    test_xvalue_address_from_value_body();
    test_xvalue_head_sixteen_byte_layout();
    contracts_test_varlen_reallen_below_capacity();
    contracts_test_varlen_reallen_equals_capacity();
    contracts_test_varlen_reallen_zero_is_legal();
    contracts_test_varlen_missing_tag();
    contracts_test_varlen_reallen_over_capacity();
    contracts_test_varlen_duplicate_tag();
    contracts_test_varlen_malformed_record_length();
    contracts_test_varlen_trailing_bytes_ignored();
    contracts_test_non_varlen_uses_body_length();
    contracts_test_varlen_ambiguity_is_known();
    contracts_test_length_layout_rejects_reallen_tag();
    contracts_test_reserved_layout_is_rejected();
    contracts_test_pointer_and_ext_are_variable_width();
    contracts_test_character_scalars_are_fixed_width();
    contracts_test_utf8_character_is_variable_width();
    contracts_test_fixed_array_zero_width();
    contracts_test_variable_array_is_not_fixed_width();
    contracts_test_character_array_is_not_fixed_width();
    contracts_test_unknown_langtype_is_not_fixed_width();
    return failures == 0 ? 0 : 1;
}
