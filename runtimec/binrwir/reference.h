#ifndef BINRWIR_REFERENCE_H
#define BINRWIR_REFERENCE_H

/*
 * 组件：binrwir/reference
 * 作用：以 32 位紧凑值表示寄存器、字面量或符号引用。
 * 联系：instruction 按读取区和写入区保存引用，Program 提供只读视图。
 * 约束：引用不重复保存方向；非法引用使用全一位模式。
 */

#include <stdint.h>

#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t binrwir_reference_t;

typedef enum binrwir_reference_kind {
    BINRWIR_REFERENCE_KIND_INVALID = 0,
    BINRWIR_REFERENCE_KIND_REGISTER = 1,
    BINRWIR_REFERENCE_KIND_LITERAL = 2,
    BINRWIR_REFERENCE_KIND_SYMBOL = 3
} binrwir_reference_kind_t;

#define BINRWIR_REFERENCE_KIND_SHIFT 30U
#define BINRWIR_REFERENCE_KIND_MASK UINT32_C(0xC0000000)
#define BINRWIR_REFERENCE_IDENTIFIER_MASK UINT32_C(0x3FFFFFFF)
#define BINRWIR_REFERENCE_INVALID UINT32_MAX

runtimec_status_t binrwir_reference_create(
    binrwir_reference_kind_t kind, uint32_t identifier,
    binrwir_reference_t *output_reference);
runtimec_status_t binrwir_reference_validate(binrwir_reference_t reference);
runtimec_status_t binrwir_reference_get_kind(
    binrwir_reference_t reference, binrwir_reference_kind_t *output_kind);
runtimec_status_t binrwir_reference_get_identifier(
    binrwir_reference_t reference, uint32_t *output_identifier);

_Static_assert(sizeof(binrwir_reference_t) == 4U,
               "binrwir references must remain 4 bytes");

#ifdef __cplusplus
}
#endif

#endif
