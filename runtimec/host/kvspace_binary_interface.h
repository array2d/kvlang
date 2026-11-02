#ifndef RUNTIMEC_HOST_KVSPACE_BINARY_INTERFACE_H
#define RUNTIMEC_HOST_KVSPACE_BINARY_INTERFACE_H

/*
 * 组件：host/kvspace_binary_interface
 * 作用：把 kvlang/include/kvspace 的 C 二进制接口逐符号抄成一张函数指针表。
 *       runtimec 因此不在链接期依赖 libkvspace：调用方按自己的方式（静态链接、
 *       dlopen、测试替身）解析符号，再把整张表交给 host。
 * 联系：host/kvspace_adapter 只经这张表调用 kvspace，不直接引用任何 kvspace
 *       符号；host/kvspace_binary_interface 定义表形状与整表绑定。
 * 约束：表的每一项都必须与 include/kvspace/kvspace.h 的声明逐字一致。绑定是
 *       全有或全无：缺任何一个符号都清零输出并失败，不存在部分可用的表。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "contract/error.h"
#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct runtimec_kvspace_binary_interface {
    /* void *kvspaceConnect(const char *data_source_name) */
    void *(*connect)(const char *data_source_name);
    /* void kvspaceClose(void *handle) */
    void (*close)(void *handle);
    /* int kvspaceGet(void *handle, const char *key, int resolve,
     *                uint8_t **output, uint32_t *output_size) */
    int (*get)(void *handle, const char *key, int resolve, uint8_t **output,
               uint32_t *output_size);
    /* int kvspaceSetValue(void *handle, const char *key, const uint8_t *value,
     *                     uint32_t value_size, char *error_text,
     *                     uint32_t error_text_capacity) */
    int (*set_value)(void *handle, const char *key, const uint8_t *value,
                     uint32_t value_size, char *error_text,
                     uint32_t error_text_capacity);
    /* int kvspaceSetPart(void *handle, const char *key, uint32_t offset,
     *                    const uint8_t *value, uint32_t value_size,
     *                    char *error_text, uint32_t error_text_capacity) */
    int (*set_part)(void *handle, const char *key, uint32_t offset,
                    const uint8_t *value, uint32_t value_size,
                    char *error_text, uint32_t error_text_capacity);
    /* int kvspaceDel(void *handle, const char *const *keys, uint32_t count,
     *                char *error_text, uint32_t error_text_capacity) */
    int (*del)(void *handle, const char *const *keys, uint32_t count,
               char *error_text, uint32_t error_text_capacity);
    /* int kvspaceListLen(void *handle, const char *prefix, int expand_extension,
     *                    int resolve, int32_t *output_count) */
    int (*list_size)(void *handle, const char *prefix, int expand_extension,
                     int resolve, int32_t *output_count);
    /* int kvspaceListAt(void *handle, const char *prefix, int expand_extension,
     *                   int resolve, int32_t index, uint8_t *buffer,
     *                   uint32_t buffer_capacity, uint32_t *output_size) */
    int (*list_at)(void *handle, const char *prefix, int expand_extension,
                   int resolve, int32_t index, uint8_t *buffer,
                   uint32_t buffer_capacity, uint32_t *output_size);
    /* int kvspaceWatch(void *handle, const char *key, const uint8_t *target,
     *                  uint32_t target_size, uint64_t tick_nanoseconds,
     *                  uint8_t **output, uint32_t *output_size) */
    int (*watch)(void *handle, const char *key, const uint8_t *target,
                 uint32_t target_size, uint64_t tick_nanoseconds,
                 uint8_t **output, uint32_t *output_size);
    /* int kvspaceViewQuiesce(void *handle, int enable) */
    int (*view_quiesce)(void *handle, int enable);
    /* int kvspaceViewAck(void *handle, uint32_t *epoch) */
    int (*view_ack)(void *handle, uint32_t *epoch);
    /* int kvspaceXvalueHeadNewSlack(const uint8_t *data, uint64_t data_size,
     *                               uint64_t capacity, const char *langtype,
     *                               uint8_t **output, uint64_t *output_size) */
    int (*new_slack)(const uint8_t *data, uint64_t data_size, uint64_t capacity,
                     const char *langtype, uint8_t **output,
                     uint64_t *output_size);
} runtimec_kvspace_binary_interface_t;

/*
 * 符号解析器：把已在内存中的库的一个符号名映射到地址；缺失返回 NULL。
 * 地址用「通用函数指针」类型承载——ISO C 不允许函数指针与 void * 互转，但允许
 * 在函数指针类型之间显式转换；绑定后每次都转回该符号的真实类型再调用。
 */
typedef void (*runtimec_kvspace_symbol_address_t)(void);
typedef runtimec_kvspace_symbol_address_t
    (*runtimec_kvspace_symbol_resolve_function_t)(void *context,
                                                  const char *symbol_name);
typedef struct runtimec_kvspace_symbol_resolver {
    void *context;
    runtimec_kvspace_symbol_resolve_function_t resolve;
} runtimec_kvspace_symbol_resolver_t;

/* 表里需要的符号名数量；名字与结构体字段按同一顺序一一对应。 */
size_t runtimec_kvspace_binary_interface_symbol_count(void);
const char *runtimec_kvspace_binary_interface_symbol_name(size_t index);

/* 置为空表：每一项都是 NULL。 */
void runtimec_kvspace_binary_interface_reset(
    runtimec_kvspace_binary_interface_t *binary_interface);
/* 每一项都非空才算完整。 */
bool runtimec_kvspace_binary_interface_is_complete(
    const runtimec_kvspace_binary_interface_t *binary_interface);

/*
 * 整表绑定：逐个符号调 resolver。任一符号缺失即清零输出并返回 UNSUPPORTED，
 * 错误里指出缺失的符号名（消息指向 binaries_interface 自己的静态文本缓冲）。
 */
runtimec_status_t runtimec_kvspace_binary_interface_bind(
    const runtimec_kvspace_symbol_resolver_t *resolver,
    runtimec_kvspace_binary_interface_t *output, runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
