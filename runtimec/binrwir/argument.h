#ifndef BINRWIR_ARGUMENT_H
#define BINRWIR_ARGUMENT_H

/*
 * 组件：binrwir/argument
 * 作用：记录 RWIR 参数在指令中的引用形状：参数序号、引用种类与槽号，
 *       而不是任何一次调用的具体地址。
 * 联系：instruction 按读取区和写入区引用 Program 的 argument 池；vmbin 在调用
 *       入口把引用绑定成帧绑定表里的 XValue 基址。
 * 约束：参数是「指针变量」——Program 只保存引用，不保存帧实参地址。只有
 *       STATIC 的 payload 是 body 地址（编译期成立、生命周期长于 compilation
 *       的静态 key）；其余 kind 的 payload 不得被当作地址使用。
 *       LOCAL 与 FUNCTION 的 slot 是 Program 私有符号池下标，LITERAL 的 payload
 *       是 Program 私有字面量池下标；池内容与下标的有效性由 program 层校验。
 */

#include <stddef.h>
#include <stdint.h>

#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BINRWIR_BODY_ADDRESS_NONE UINT64_C(0)
#define BINRWIR_ARGUMENT_INDEX_NONE UINT32_MAX

/*
 * BinRWIR 参数项二进制 generation：
 *   1 = argidx + 对齐填充 + body_address（地址化模型，已删除）；
 *   2 = argidx + 引用种类 + 槽号 + payload（指针变量模型）；
 *   3 = 布局同 2，但 LOCAL / FUNCTION 的 slot 是 Program 符号池下标、LITERAL 的
 *       payload 是 Program 字面量池下标（当前）。
 */
#define BINRWIR_BINARY_INTERFACE_GENERATION 3U

typedef enum binrwir_reference_kind {
    BINRWIR_REFERENCE_INVALID = 0,
    BINRWIR_REFERENCE_PARAMETER_READ = 1,  /* slot = 第 j 个读参 */
    BINRWIR_REFERENCE_PARAMETER_WRITE = 2, /* slot = 第 j 个写参 */
    BINRWIR_REFERENCE_LOCAL = 3,           /* slot = 符号池下标 */
    BINRWIR_REFERENCE_STATIC = 4,          /* payload = 静态 body 地址 */
    BINRWIR_REFERENCE_LITERAL = 5,         /* payload = 字面量池下标 */
    BINRWIR_REFERENCE_FUNCTION = 6         /* slot = 符号池下标（函数名） */
} binrwir_reference_kind_t;

typedef struct binrwir_argument {
    uint32_t argument_index;
    uint8_t reference_kind;
    uint8_t reserved_8_bits;
    uint16_t slot;
    uint64_t payload;
} binrwir_argument_t;

runtimec_status_t binrwir_argument_create(
    uint32_t argument_index, binrwir_reference_kind_t reference_kind,
    uint16_t slot, uint64_t payload, binrwir_argument_t *output_argument);
runtimec_status_t binrwir_argument_validate(
    const binrwir_argument_t *argument);

_Static_assert(sizeof(binrwir_argument_t) == 16U,
               "binrwir arguments must remain 16 bytes");
_Static_assert(offsetof(binrwir_argument_t, argument_index) == 0U,
               "binrwir argidx must begin each argument");
_Static_assert(offsetof(binrwir_argument_t, reference_kind) == 4U,
               "binrwir reference kind must follow argidx");
_Static_assert(offsetof(binrwir_argument_t, reserved_8_bits) == 5U,
               "binrwir reserved byte must follow the reference kind");
_Static_assert(offsetof(binrwir_argument_t, slot) == 6U,
               "binrwir slot must follow the reserved byte");
_Static_assert(offsetof(binrwir_argument_t, payload) == 8U,
               "binrwir payload must follow canonical alignment padding");

#ifdef __cplusplus
}
#endif

#endif
