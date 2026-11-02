#ifndef RUNTIMEC_CONTRACT_VIEW_H
#define RUNTIMEC_CONTRACT_VIEW_H

/*
 * 组件：contract/view
 * 作用：跨组件只读视图与可变字节缓冲的普通数据定义。视图只借用调用方
 *       内存，不拥有、不释放；缓冲的 data/capacity 也由调用方管理，
 *       任何函数都不得通过视图隐式分配。
 * 联系：contract 层只依赖自身；host port 的入参与 vmbin/vmkv/vmjit 的
 *       字符串/路径/字节输入都使用这些视图，避免在执行组件边界传递 owning
 *       std::string 或 kvspace 类型。
 * 约束：size 为 0 时 data 可以为 NULL；size 非 0 时 data 必须非 NULL。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RUNTIMEC_BYTES_VIEW_EMPTY { NULL, 0U }
#define RUNTIMEC_STRING_VIEW_EMPTY { NULL, 0U }
#define RUNTIMEC_KEY_VIEW_EMPTY { NULL, 0U }
#define RUNTIMEC_STRING_VIEW_LITERAL(value) { (value), sizeof(value) - 1U }
#define RUNTIMEC_KEY_VIEW_LITERAL(value) { (value), sizeof(value) - 1U }

/* 只读字节视图；不拥有内存。 */
typedef struct runtimec_bytes_view {
    const uint8_t *data;
    size_t size;
} runtimec_bytes_view_t;

/* 调用方提供的可写字节缓冲；size 为已用长度，capacity 为容量。 */
typedef struct runtimec_bytes_buffer {
    uint8_t *data;
    size_t size;
    size_t capacity;
} runtimec_bytes_buffer_t;

/* 只读字符串视图；不保证 NUL 结尾，长度以 size 为准。 */
typedef struct runtimec_string_view {
    const char *data;
    size_t size;
} runtimec_string_view_t;

/* kvspace 键视图；与字符串视图同形，语义上表示路径/函数键。 */
typedef struct runtimec_key_view {
    const char *data;
    size_t size;
} runtimec_key_view_t;

/* 由裸指针与长度构造视图；不做拷贝、不做 NUL 检查。 */
runtimec_bytes_view_t runtimec_bytes_view_make(const void *data, size_t size);
runtimec_string_view_t runtimec_string_view_make(const char *data, size_t size);
/* 由 NUL 结尾字符串构造视图；NULL 视为空视图。 */
runtimec_string_view_t runtimec_string_view_from_c_string(const char *value);
runtimec_key_view_t runtimec_key_view_make(const char *data, size_t size);
runtimec_key_view_t runtimec_key_view_from_c_string(const char *value);
/* 只把已用长度归零，不释放 data、不改变 capacity。 */
void runtimec_bytes_buffer_reset(runtimec_bytes_buffer_t *buffer);
/* 以下校验只检查形状（size/data 一致性），不检查内存可读性。 */
bool runtimec_bytes_view_is_valid(runtimec_bytes_view_t view);
bool runtimec_bytes_buffer_is_valid(const runtimec_bytes_buffer_t *buffer);
bool runtimec_string_view_is_valid(runtimec_string_view_t view);
bool runtimec_key_view_is_valid(runtimec_key_view_t view);

#ifdef __cplusplus
}
#endif

#endif
