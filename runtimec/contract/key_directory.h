#ifndef RUNTIMEC_CONTRACT_KEY_DIRECTORY_H
#define RUNTIMEC_CONTRACT_KEY_DIRECTORY_H

/*
 * 组件：contract/key_directory
 * 作用：把存储层的「原始名字列表」变成「按 `cmp_coord` 有序的第 i 项」，即旧
 *       `kvspaceListAt(dir, i)` 的语义，但排序与缓存都在本进程内。
 *
 * 为什么上移：kvspace 第二阶段的 `include/kvspace/storage.h` 里 `kvspaceList`
 * 明确「后端顺序、不排序」，`listat` 原语被删除。存储层不再维护「第 i 个有序子项」，
 * 所以这个能力只能由消费者重建——而消费者有好几个（vmkv / vmbin / cli），必须共用
 * 一份实现，否则 `scatterarray·append` 和 `for x in m` 会对同一个目录取到不同的顺序。
 *
 * 联系：依赖 `contract/key_order`（比较器）与 `contract/allocator`（冷路径内存）。
 *       存储访问由调用方以函数指针传入（`runtimec_key_directory_source_t`），本模块
 *       不认识 kvspace 的句柄类型，也不 include `kvspace/storage.h`——这样它既能在
 *       新 ABI 上跑，也能在旧 ABI 适配层上跑。
 * 约束：
 *   - **不排序就不叫 ListAt**：任何直接暴露后端顺序的出口都不属于本模块。
 *   - 缓存以 `generation` 失效：`kvspaceList` 的 `out_generation` 在 key 集合变化时
 *     前进、值修改时不动，所以「改了值」不会让名字缓存失效。
 *   - 内存用调用方给的分配器，不用 malloc；热路径不得动态分配（见 allocator.h）。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "contract/allocator.h"
#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 一次 `kvspaceList` 的结果，由调用方（存储适配层）填充。
 *
 * `names` 是 `name\0name\0…` 的连续字节块，`length` 是总字节数，`count` 是项数，
 * `generation` 是该目录 key 集合的代号。`names` 的所有权仍归 `source` 的调用方，
 * 本模块只在其生命周期内读取。
 */
typedef struct runtimec_key_directory_listing {
    const char *names;
    uint32_t length;
    uint32_t count;
    uint64_t generation;
} runtimec_key_directory_listing_t;

/*
 * 取一个目录的直接子项原始列表（不排序）。
 *
 * `capacity == 0 && names == NULL` 表示「只探测长度/个数/generation」。
 * 缓冲区不足时返回 `RUNTIMEC_STATUS_INVALID` 并把 `required_length` 写出，
 * 调用方据此重新分配（与 `kvspaceList` 的 CAPACITY 语义对齐）。
 */
typedef runtimec_status_t (*runtimec_key_directory_source_t)(
    void *context, const char *directory, char *names, uint32_t capacity,
    uint32_t *output_length, uint32_t *output_count,
    uint64_t *output_generation);

/*
 * 一个目录的排序缓存。**只按指针传出**，不复制：它持有分配来的名字块与索引数组。
 *
 * `names` 是**已经排序后**的字节块（仍是 `name\0…` 形状，改名不改内容），
 * `offsets[i]` 是第 i 项在 `names` 里的起始偏移。排序只交换 offsets，不搬字节——
 * 搬字节会让「名字块」与「偏移」两份表示互相引用，改一处漏一处。
 */
typedef struct runtimec_key_directory {
    runtimec_allocator_t allocator;
    void *source_context;
    runtimec_key_directory_source_t source;

    char *names;
    uint32_t names_capacity;
    uint32_t names_length;
    uint32_t *offsets;
    uint32_t offsets_capacity;
    uint32_t count;
    uint64_t generation;
    bool valid;
} runtimec_key_directory_t;

/* 归零并记下 source/allocator；不分配。 */
void runtimec_key_directory_reset(runtimec_key_directory_t *directory,
                                  const runtimec_allocator_t *allocator,
                                  void *source_context,
                                  runtimec_key_directory_source_t source);

/* 释放持有的一切；之后可再次 refresh。返回状态而不是 void：生命周期函数的失败
 * 必须能上报（分配器不匹配时释放会失败），void 会把「没释放成功」伪装成正常。 */
runtimec_status_t runtimec_key_directory_destroy(
    runtimec_key_directory_t *directory);

/*
 * 按需重建：`generation` 未变则立即返回（O(1)）。返回后 `count` 是最新项数。
 * 分配失败 → `RUNTIMEC_STATUS_ERROR`；source 失败 → 透传其状态。
 */
runtimec_status_t runtimec_key_directory_refresh(
    runtimec_key_directory_t *directory, const char *key);

/* 第 i 个有序子项；越界或未 refresh → `RUNTIMEC_STATUS_INVALID`。
 * 返回值借用 `directory`，下一次 refresh/destroy 后失效。 */
runtimec_status_t runtimec_key_directory_at(const runtimec_key_directory_t *directory,
                                            uint32_t index, const char **output_name,
                                            uint32_t *output_size);

/* 直接按名取：二分查找（列表已排序）。找不到 → `RUNTIMEC_STATUS_INVALID`。 */
runtimec_status_t runtimec_key_directory_find(const runtimec_key_directory_t *directory,
                                              const char *name, uint32_t name_size,
                                              uint32_t *output_index);

#ifdef __cplusplus
}
#endif

#endif
