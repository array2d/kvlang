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
#include "contract/kvspace_access.h"
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
/* 销毁整张表，并释放表自己拥有的槽键副本。 */
runtimec_status_t binrwir_frame_bindings_destroy(
    binrwir_frame_bindings_t *bindings);
/* 把全部条目置为无效，保留容量，并释放表拥有的槽键副本。 */
runtimec_status_t binrwir_frame_bindings_reset(
    binrwir_frame_bindings_t *bindings);
size_t binrwir_frame_bindings_get_capacity(
    const binrwir_frame_bindings_t *bindings);

/*
 * 绑定一个槽：写入 slot_key、caller_key 与解析到的 XValue 基址，并记下绑定时的
 * source generation。零地址、非法视图或槽号越界一律 INVALID；重复绑定同一槽是
 * 允许的，新绑定整体覆盖旧绑定。output_generation 可为 NULL。
 *
 * **两张键由表自己拥有**：入参是借用视图，本函数按 `create` 收进来的分配器复制
 * 一份，重绑/失效/销毁时释放。调用方可以立即释放自己那份 key 缓冲——绑定表比
 * 调用它的那一层活得久，存借用视图就是 use-after-free（帧准备曾这样悬空过）。
 *
 * 复制失败返回 OUT_OF_MEMORY，且**旧绑定保持原样**（先分配成功再覆盖）。
 */
runtimec_status_t binrwir_frame_bindings_bind(
    binrwir_frame_bindings_t *bindings, uint16_t slot,
    runtimec_key_view_t slot_key, runtimec_key_view_t caller_key,
    runtimec_xvalue_address_t xvalue_address,
    uint64_t generation);
/*
 * 绑定一个内联值：字面量实参没有存储位置，直接把池内字节区间记在槽上。
 * 只在读参槽有意义（写槽不可能是字面量）；键视图可空，generation 仍必须非零。
 * 键的拥有关系与 `binrwir_frame_bindings_bind` 相同：**表复制一份自己持有**；
 * 字节视图仍借用 Program 字面量池（Program 生命周期长于绑定表）。
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

/*
 * ── 帧内槽的 key 拼法：**唯一一处** ──────────────────────────────────
 *
 * 为什么这两个函数在这里而不是在调用方各自拼：产品侧的 `bind_parameters` 接收的是
 * **绝对 key**（`execution_window.h` 的 `runtimec_argument_description` 注释：
 * 「EXISTING_KEY / WRITE_TARGET —— 绝对槽键（`/vthread/...`），本层不拼不猜」），
 * 也就是拼 key 是**调用方的职责**。调用方不止一个（测试驱动、byteseek 等嵌入方），
 * 各拼各的就是「同一事实两个来源」——而拼错的症状是 `UNBOUND_SLOT` 或读到
 * 不存在的 key（`kvspaceGet` 对缺失 key 返回 `rc=0 + out=NULL`，**不报错**）。
 *
 * 两种键同形不同名，别混：
 *   - **局部槽**   `/vthread/<vid>/[<depth>]/<name>`         —— 名字是 key 的最后一段
 *   - **形参槽**   `/vthread/<vid>/[<depth>]/[0,<±j>]`      —— 坐标：读参 `-j`、写参 `+j`，j 从 1 起
 *
 * 形参用坐标而非名字：参数顺序由坐标显式记录（命名键不保序，见 spec
 * `03-类型系统/22-storetype-def族与签名行` 第 3 条）。写成名字会与局部同名时撞键。
 *
 * 返回值：写入 `output` 的字节数（不含结尾 NUL）；容量不足返回 `SIZE_MAX`，
 * **不截断**——截断的 key 会指向另一个槽，比失败难查得多。
 */
size_t binrwir_frame_local_key(runtimec_string_view_t frame_path,
                               runtimec_string_view_t name, char *output,
                               size_t capacity);

/*
 * 形参槽 key。`write_parameter` 为真取 `+j`（写参），否则 `-j`（读参）；
 * `parameter_ordinal` 是**该方向内的序号，从 0 起**（与 `binrwir_parameter` 的
 * argidx 一致），函数内部加 1 落成 `[0,±<ordinal+1>]`。
 */
size_t binrwir_frame_parameter_key(runtimec_string_view_t frame_path,
                                   uint16_t parameter_ordinal,
                                   bool write_parameter, char *output,
                                   size_t capacity);


#ifdef __cplusplus
}
#endif

#endif
