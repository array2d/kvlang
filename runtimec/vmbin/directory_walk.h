#ifndef RUNTIMEC_VMBIN_DIRECTORY_WALK_H
#define RUNTIMEC_VMBIN_DIRECTORY_WALK_H

/*
 * 组件：vmbin/directory_walk
 * 作用：handler 层列举一个目录时的**唯一入口**。存储接口的 `kvspaceList` 只给后端
 *       顺序，而 handler 需要「第 i 个有序子项」与「数个数」——这两件事都在这里，
 *       底层是 `contract/key_directory`（`cmp_coord` 排序 + generation 缓存）。
 *
 * 为什么不是每个 handler 自己调 `kvspaceListlen`/`kvspaceList`：
 *   `scatter.c` 里有 4 处、`host.c` 1 处、`cli` 2 处、`binrwir_dump` 2 处。每处自己
 *   拼「探测长度 → 分配 → 取回 → 排序」就等于有 9 份实现，其中必然有一两份忘了按
 *   坐标排。而「`scatter` 取最大坐标」和「`for x in m` 取第 i 个」对顺序的要求是
 *   同一句话：按 `cmp_coord`。
 *
 * 联系：依赖 `contract/key_directory` 与 `contract/storage_interface`。不 include
 *       旧 `kvspace.h`——handler 层仍在用它做读写，但枚举这一条已整体切到新接口。
 * 约束：
 *   - 每次调用都是**一次性**遍历，不复用调用者之间的缓存：缓存生命周期挂在这个
 *     结构体上，用完 `destroy`。跨调用复用需要一套失效策略，而 handler 之间没有
 *     共享 state 的地方可以放它。
 *   - 失败一律返回 `false` 并**不**写 `output_count`：调用方据此具名失败，不要
 *     「枚举失败 → 当作 0 个」——那会把「目录坏了」变成「目录是空的」。
 */

#include <stdbool.h>
#include <stdint.h>

#include "contract/allocator.h"
#include "contract/key_directory.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 解析后的目录路径上限：与 kvspace 的 key 上界同值（`storage.h` 拒绝 >= 1024 的 key）。 */
#define RUNTIMEC_DIRECTORY_WALK_KEY_CAPACITY 1024U

/* 一条 XValue 的最小字节数（head）。 */
#define KVSPACE_KEY_RESOLUTION_MIN_WIRE 16U

/* 一次目录遍历的上下文。**必须** Pair 使用：open 之后 walk/at/count，最后 close。 */
typedef struct runtimec_directory_walk {
    runtimec_key_directory_t directory;
    runtimec_allocator_t allocator;
    bool opened;
} runtimec_directory_walk_t;

/*
 * 打开并排好一个目录。
 *
 * `allocator` 可为 NULL：为 NULL 时用 libc 的 malloc/free。handler 层拿不到
 * `contract/allocator`（frame 里没有它），而目录枚举是**冷路径**——每次 handler
 * 调用一次，不是每条指令一次。同层的 `container.c` 早已这么用（读变长 body 时
 * `malloc` + `owned` 标记）。传 allocator 的入口留给 engine/cli 这类能拿到注入
 * 分配器的调用者。
 *
 * `resolve_pointer` 对应旧 ABI 的 `resolve=1`：存储接口不跟 Ptr，所以这里用
 * `contract/key_resolution` 先跟前缀值，跟到非 Ptr 或 16 跳上限。
 * `expand_extension` 对应旧的 `expand_ext`：**新接口不展开 extindex**，所以这个参数
 * 只能是 0；传 1 直接失败——「悄悄少列几个」比「明确不支持」坏得多。
 */
bool runtimec_directory_walk_open(runtimec_directory_walk_t *walk,
                                  const runtimec_allocator_t *allocator,
                                  void *kvspace_handle, const char *directory,
                                  bool resolve_pointer, bool expand_extension);

/* 子项数。未 open 或 open 失败返回 false。 */
bool runtimec_directory_walk_count(const runtimec_directory_walk_t *walk,
                                   int64_t *output_count);

/* 第 index 个有序子项（名字不含 NUL，`output_size` 是字节数）。 */
bool runtimec_directory_walk_at(const runtimec_directory_walk_t *walk,
                                int64_t index, const char **output_name,
                                uint32_t *output_size);

/* 释放并重新归零。返回状态而不是 void：生命周期函数失败必须能上报。 */
runtimec_status_t runtimec_directory_walk_close(runtimec_directory_walk_t *walk);

#ifdef __cplusplus
}
#endif

#endif
