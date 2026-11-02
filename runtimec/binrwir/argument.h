#ifndef BINRWIR_ARGUMENT_H
#define BINRWIR_ARGUMENT_H

/*
 * 组件：binrwir/argument
 * 作用：记录 RWIR 参数序号及其解引用后的 KVspace V.body 内存地址。
 * 联系：instruction 按读取区和写入区引用 Program 的 argument 池。
 * 约束：二进制语义字段严格按 argidx、argaddr 排列。中间 32 位只是必须为零的
 *       64 位地址对齐填充，不是第三个语义字段；argument 不拥有或缓存 V.body。
 */

#include <stddef.h>
#include <stdint.h>

#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BINRWIR_BODY_ADDRESS_NONE UINT64_C(0)
#define BINRWIR_ARGUMENT_INDEX_NONE UINT32_MAX

typedef struct binrwir_argument {
    uint32_t argument_index;
    uint32_t reserved_32_bits;
    uint64_t body_address;
} binrwir_argument_t;

runtimec_status_t binrwir_argument_create(
    uint32_t argument_index, uint64_t body_address,
    binrwir_argument_t *output_argument);
runtimec_status_t binrwir_argument_validate(
    const binrwir_argument_t *argument);

_Static_assert(sizeof(binrwir_argument_t) == 16U,
               "binrwir arguments must remain 16 bytes");
_Static_assert(offsetof(binrwir_argument_t, argument_index) == 0U,
               "binrwir argidx must begin each argument");
_Static_assert(offsetof(binrwir_argument_t, body_address) == 8U,
               "binrwir argaddr must follow canonical alignment padding");

#ifdef __cplusplus
}
#endif

#endif
