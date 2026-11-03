#ifndef BINRWIR_FRAME_BINDINGS_H
#define BINRWIR_FRAME_BINDINGS_H

/*
 * 组件：binrwir/frame_bindings
 * 作用：保存一次调用期间「参数槽/局部槽 → 值的位置」的绑定表。
 * 联系：vmbin 在调用入口建立并写入；vmbin 与 BinRWIR JIT 在执行时按槽查表。
 * 约束：绑定表是帧派生状态——可丢弃、可重建，不是值缓存，也不是第二事实源。
 *       地址变体只存 XValue 基址与 generation，不存 body 地址、不存值；body 地址
 *       只能由合同的唯一推导函数在访问时算出。内联变体只用于「实参是字面量」：
 *       它直接指向 Program 字面量池的一段只读字节，不拷贝、不参与别名判定。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "contract/allocator.h"
#include "contract/host_services.h"
#include "contract/status.h"
#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BINRWIR_FRAME_BINDING_SLOT_NONE UINT16_MAX
#define BINRWIR_FRAME_BINDING_GENERATION_NONE UINT64_C(0)

typedef enum binrwir_binding_kind {
    BINRWIR_BINDING_INVALID = 0,
    BINRWIR_BINDING_ADDRESS = 1, /* xvalue_address 有效 */
    BINRWIR_BINDING_INLINE = 2   /* bytes/length 指向 Program 字面量池 */
} binrwir_binding_kind_t;

typedef struct binrwir_frame_binding {
    runtimec_key_view_t slot_key;
    runtimec_key_view_t caller_key;
    runtimec_xvalue_address_t xvalue_address;
    /* 内联变体：实参是字面量时直接借出池内字节，Program 生命周期内有效。 */
    const uint8_t *bytes;
    size_t length;
    uint64_t generation;
    uint8_t kind; /* binrwir_binding_kind_t */
    bool valid;
} binrwir_frame_binding_t;

typedef struct binrwir_frame_bindings binrwir_frame_bindings_t;

/*
 * 建立一张可容纳 capacity 个绑定的空表。所有条目初始为无效。
 * 失败时 output_bindings 为 NULL；分配失败返回 OUT_OF_MEMORY。
 */
runtimec_status_t binrwir_frame_bindings_create(
    const runtimec_allocator_t *allocator, size_t capacity,
    binrwir_frame_bindings_t **output_bindings);
/* 销毁整张表；表内视图都是借用，不做释放。 */
runtimec_status_t binrwir_frame_bindings_destroy(
    binrwir_frame_bindings_t *bindings);
/* 把全部条目置为无效，保留容量与借用视图清零。 */
runtimec_status_t binrwir_frame_bindings_reset(
    binrwir_frame_bindings_t *bindings);
size_t binrwir_frame_bindings_get_capacity(
    const binrwir_frame_bindings_t *bindings);

/*
 * 绑定一个槽：写入 slot_key、caller_key 与解析到的 XValue 基址，并记下绑定时的
 * source generation。零地址、非法视图或槽号越界一律 INVALID；重复绑定同一槽是
 * 允许的，新绑定整体覆盖旧绑定。output_generation 可为 NULL。
 */
runtimec_status_t binrwir_frame_bindings_bind(
    binrwir_frame_bindings_t *bindings, uint16_t slot,
    runtimec_key_view_t slot_key, runtimec_key_view_t caller_key,
    runtimec_xvalue_address_t xvalue_address,
    uint64_t generation);
/*
 * 绑定一个内联值：字面量实参没有存储位置，直接把池内字节区间记在槽上。
 * 只在读参槽有意义（写槽不可能是字面量）；键视图可空，generation 仍必须非零。
 */
runtimec_status_t binrwir_frame_bindings_bind_inline(
    binrwir_frame_bindings_t *bindings, uint16_t slot,
    runtimec_key_view_t slot_key, runtimec_bytes_view_t bytes,
    uint64_t generation);
/*
 * 取一个槽的绑定：唯一访问器，地址变体与内联变体都走它。
 *
 * 槽未绑定、已失效、或绑定时的 generation 与 expected_generation 不一致时返回
 * UNSUPPORTED 并清空输出——**绝不交出旧绑定**。表为空或槽号越界返回 INVALID。
 * 调用方传的是自己帧的 snapshot_generation：generation 变了就说明存储被换过，
 * 旧绑定不再对应同一份值。
 */
runtimec_status_t binrwir_frame_bindings_get(
    const binrwir_frame_bindings_t *bindings, uint16_t slot,
    uint64_t expected_generation,
    const binrwir_frame_binding_t **output_binding);
/* 让一个槽失效；旧地址从此不得再用。 */
runtimec_status_t binrwir_frame_bindings_invalidate(
    binrwir_frame_bindings_t *bindings, uint16_t slot);


#ifdef __cplusplus
}
#endif

#endif
