/*
 * 字面量池里**每条 wire 的起点必须 8 字节对齐**，于是 body（wire + 16）也是。
 *
 * 为什么这条要单独测：wire 布局是 `[16 字节头][body][attr]`，body 的对齐**继承自**
 * wire 起点。池子逐条紧排不填充时，第一条之后的总长只要是奇数，第二条的 body 就
 * 落在 8 的倍数之外——而特化后的 handler 会直接 `*(const int64_t *)body` 取标量。
 *
 * x86 容忍未对齐访问，所以这个 bug 在开发机上**看不出来**；aarch64 是 UB，JIT 出
 * 单条 `ldr` 就真崩。shm 侧没有这个问题（`kvspace-c` 自己强制 `off%8==0 &&
 * val%8==0`），所以它只在 Program 的私有字面量池里成立。
 *
 * 用例故意放一个**奇数字节**的字面量在前，逼出紧排时的错位：
 *   literal[0] 长度 17（16 头 + 1 body），紧排下 literal[1] 的 wire 就是 17，body 33
 *   —— 33 % 8 != 0，正是要防的情形。
 *
 * 反向控制：把 `binrwir_binfunc_copy_bytes` 里的补齐改回 `*bytes_offset += bytes.size`，
 * 本用例必须红。
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "binrwir/argument.h"
#include "binrwir/binfunc.h"
#include "contract/allocator.h"
#include "contract/status.h"
#include "kvspace/xvalue_head.h"

static int failures;

#define EXPECT(condition)                                                      \
    do {                                                                       \
        if (!(condition)) {                                                    \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #condition);        \
            failures += 1;                                                     \
        }                                                                      \
    } while (0)

static void *test_allocate(void *context, size_t size, size_t alignment) {
    (void)context;
    (void)alignment;
    return malloc(size);
}
static void test_deallocate(void *context, void *allocation, size_t size,
                            size_t alignment) {
    (void)context;
    (void)size;
    (void)alignment;
    free(allocation);
}

/* 造一条 wire：16 字节头 + body_size 字节 body，无 attr。 */
static size_t test_make_wire(uint8_t *wire, size_t body_size) {
    kvspaceXvalueHead head;

    memset(&head, 0, sizeof(head));
    kvspaceXvalueHeadSetLen(&head, body_size);
    head.layout = KVSPACE_XVALUE_LAYOUT_LENGTH;
    memcpy(wire, &head, KVSPACE_XVALUE_HEAD_LEN);
    memset(wire + KVSPACE_XVALUE_HEAD_LEN, 0, body_size);
    return KVSPACE_XVALUE_HEAD_LEN + body_size;
}

int main(void) {
    runtimec_allocator_t allocator = {NULL, test_allocate, test_deallocate};
    binrwir_binfunc_identity_t identity;
    binrwir_binfunc_builder_t *builder = NULL;
    binrwir_binfunc_t *program = NULL;
    /* 三个字面量：奇数字节在前，逼出紧排错位。 */
    static const size_t body_sizes[3] = {1U, 8U, 3U};
    uint8_t wires[3][KVSPACE_XVALUE_HEAD_LEN + 8U];
    size_t wire_sizes[3];
    size_t literal_count = 0U;

    memset(&identity, 0, sizeof(identity));
    identity.snapshot_generation = 1U;
    identity.program_generation = 1U;
    identity.binrwir_binary_interface_generation =
        BINRWIR_BINARY_INTERFACE_GENERATION;
    EXPECT(binrwir_binfunc_builder_create(&allocator, &identity, &builder) ==
           RUNTIMEC_STATUS_SUCCESS);

    for (size_t ordinal = 0U; ordinal < 3U; ++ordinal) {
        uint32_t index = BINRWIR_LITERAL_INDEX_NONE;

        wire_sizes[ordinal] = test_make_wire(wires[ordinal], body_sizes[ordinal]);
        EXPECT(binrwir_binfunc_builder_add_literal(
                   builder,
                   runtimec_bytes_view_make(wires[ordinal],
                                            wire_sizes[ordinal]),
                   runtimec_string_view_from_c_string("char/utf8"), &index) ==
               RUNTIMEC_STATUS_SUCCESS);
    }
    /*
     * finalize 要求至少一条指令（空 Program 是 INVALID）。加一条最小 COPY：
     * 字面量 0 读到 STATIC 槽——槽地址用字面量自己的 body，只为让指令合法，
     * 本用例只关心池的对齐。
     */
    {
        binrwir_argument_t arguments[2];
        binrwir_instruction_build_request_t request;

        EXPECT(binrwir_argument_create(0U, BINRWIR_REFERENCE_LITERAL, 0U, 0U,
                                       &arguments[0]) ==
               RUNTIMEC_STATUS_SUCCESS);
        /* 写参也用 LITERAL：STATIC 要求 slot 落在符号池内，而本用例不建符号池。
         * 只为让指令过校验，不参与断言。 */
        EXPECT(binrwir_argument_create(1U, BINRWIR_REFERENCE_LITERAL, 0U, 1U,
                                       &arguments[1]) ==
               RUNTIMEC_STATUS_SUCCESS);
        memset(&request, 0, sizeof(request));
        request.source_logical_program_counter = 1U;
        request.operation_identifier = 0x0001U; /* catalog: copy */
        request.read_arguments.data = &arguments[0];
        request.read_arguments.count = 1U;
        request.write_arguments.data = &arguments[1];
        request.write_arguments.count = 1U;
        {
            uint32_t identifier = BINRWIR_IDENTIFIER_NONE;
            EXPECT(binrwir_binfunc_builder_add_instruction(
                       builder, &request, &identifier) ==
                   RUNTIMEC_STATUS_SUCCESS);
        }
    }
    EXPECT(binrwir_binfunc_builder_finalize(builder, &program) ==
           RUNTIMEC_STATUS_SUCCESS);
    EXPECT(binrwir_binfunc_get_literal_count(program, &literal_count) ==
           RUNTIMEC_STATUS_SUCCESS);
    EXPECT(literal_count == 3U);

    for (uint32_t index = 0U; index < (uint32_t)literal_count; ++index) {
        runtimec_bytes_view_t wire;
        runtimec_string_view_t langtype;
        const uint8_t *body;

        EXPECT(binrwir_binfunc_get_literal(program, index, &wire, &langtype) ==
               RUNTIMEC_STATUS_SUCCESS);
        EXPECT(wire.data != NULL);
        EXPECT(wire.size == wire_sizes[index]);
        body = wire.data + KVSPACE_XVALUE_HEAD_LEN;
        /* 规格本体：每条 wire 起点 8 对齐，于是 body 也 8 对齐。 */
        if (wire.data != NULL) {
            EXPECT(((uintptr_t)wire.data % 8U) == 0U);
            EXPECT(((uintptr_t)body % 8U) == 0U);
        }
    }

    EXPECT(binrwir_binfunc_destroy(program) == RUNTIMEC_STATUS_SUCCESS);
    EXPECT(binrwir_binfunc_builder_destroy(builder) == RUNTIMEC_STATUS_SUCCESS);

    if (failures != 0) {
        printf("binrwir_literal_alignment_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("binrwir_literal_alignment_test: OK\n");
    return 0;
}
