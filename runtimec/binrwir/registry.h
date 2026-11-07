#ifndef BINRWIR_REGISTRY_H
#define BINRWIR_REGISTRY_H

/*
 * 组件：binrwir/registry
 * 作用：将 builtin 标识符映射到唯一处理函数；条目由各 builtin 模块追加。
 * 联系：interpreter 查找并调用；处理函数经 vmbin/value_access 按地址读写操作数，
 *       与 core 规则共用同一条数据面。
 * 约束：
 *   - Registry **拥有条目存储**（定长 `storage`），模块的静态数组只在 append 期间被读；
 *   - 装载期校验：重复 opid 硬失败（不覆盖）、空 handler 硬失败、超限硬失败（不截断）；
 *   - 处理函数不得缓存 V.body 内容或建立寄存器文件。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "binrwir/execution_exit.h"
#include "binrwir/execution_frame.h"
#include "binrwir/instruction.h"
#include "binrwir/operation_identifiers.h"
#include "binrwir/program.h"
#include "contract/error.h"
#include "contract/status.h"
#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 处理函数经 vmbin/value_access 按地址读写操作数；它不拥有 frame、instruction
 * 与 output_exit，也不得保留它们的指针。
 */
typedef runtimec_status_t
(*binrwir_registry_handler_t)(
    binrwir_execution_frame_t *frame,
    const binrwir_instruction_view_t *instruction,
    binrwir_execution_exit_t *output_exit);

typedef enum binrwir_registry_flags {
    BINRWIR_REGISTRY_FLAG_NONE = 0,
    BINRWIR_REGISTRY_FLAG_EFFECT = 1U << 0,
    BINRWIR_REGISTRY_FLAG_MAY_YIELD = 1U << 1
} binrwir_registry_flags_t;

typedef struct binrwir_registry_entry {
    binrwir_operation_identifier_t operation_identifier;
    uint32_t flags;
    binrwir_registry_handler_t handler;
} binrwir_registry_entry_t;

/*
 * 条目上限：基础能力行（catalog 130 行）+ 特化条目（生成器算出，见
 * `binrwir/operation_identifiers.h` 的 `BINRWIR_SPECIALIZED_COUNT`）再加余量。
 *
 * 生成器 `--check` 校验「基础行 + 特化数 ≤ 本宏」，超了生成期红而不是运行期红。
 * 运行期的超限失败仍保留作兜底——注入的表若绕过生成器，这里是最后一道。
 *
 * 尺寸：1923 × 24 字节 ≈ 46 KB，随 session 走。可以用，但**不能**拿整个 16 位
 * opid 空间做稠密下标（65536 × 2 = 128 KB），那会让每个 session 先付 128 KB。
 */
#define BINRWIR_REGISTRY_ENTRY_LIMIT 2048U

/*
 * 查找用**两段式**，不是一条线性扫：
 *
 *   特化区 0x8000–0xFEFF 是**稠密**的（`0x8000 + 序号`），直接下标 O(1)；
 *   基础区是**稀疏**的（当前 87 个散布在 0x0001–0x0192），升序数组 + 二分。
 *
 * 为什么不统一用稠密表：基础区只用了 87 个位置却要占 32768 项（64 KB）；
 * 为什么不统一用二分：特化 1836 条是每次执行都查的热路径，二分要 11 次比较，
 * 而它们本来就是连续分配的，直接下标白拿 O(1)。
 *
 * 两张索引都在 `append` 末尾重建——装载期一次，运行期零维护。
 */
#define BINRWIR_REGISTRY_SPECIALIZED_INDEX_LIMIT \
    (BINRWIR_OPERATION_IDENTIFIER_RESERVED_BASE - \
     BINRWIR_SPECIALIZED_BASE)

typedef struct binrwir_registry {
    binrwir_registry_entry_t storage[BINRWIR_REGISTRY_ENTRY_LIMIT];
    size_t entry_count;
    /* 特化区：`specialized_index[opid - BINRWIR_SPECIALIZED_BASE]` → 条目下标 + 1
     * （0 表示该 opid 没有条目）。存下标 + 1 是为了让 0 天然表示「空」，
     * 不必另设哨兵。 */
    uint16_t specialized_index[BINRWIR_REGISTRY_SPECIALIZED_INDEX_LIMIT];
    /* 基础区：按 opid 升序的条目下标；查的时候二分。 */
    uint16_t base_index[BINRWIR_REGISTRY_ENTRY_LIMIT];
    size_t base_index_count;
    /* 特化区实际占用的槽数。**rebuild 时算好存下来**，不在 find 里数——
     * 每次分派都扫 2048 项的话，两段式索引省下的时间又被这条检查还回去了。 */
    size_t specialized_index_count;
    /* 索引是否已建。表被绕过 append 构造过时它是 false，find 会拒绝。 */
    bool index_ready;
} binrwir_registry_t;

/*
 * 一个 builtin 模块：名字 + 它拥有的静态条目数组。
 * 模块必须返回指向静态存储的指针，不得返回栈上或堆上分配的数组（C 无反射，
 * 这条只能靠约定与 code review）。
 */
typedef struct binrwir_registry_module {
    const char *module_name;
    const binrwir_registry_entry_t *entries;
    size_t entry_count;
} binrwir_registry_module_t;

/* 置空。零条目是合法状态（未装任何 builtin 的 session 依赖它）。 */
runtimec_status_t binrwir_registry_initialize(
    binrwir_registry_t *registry);
runtimec_status_t binrwir_registry_reset(
    binrwir_registry_t *registry);
runtimec_status_t binrwir_registry_destroy(
    binrwir_registry_t *registry);

/*
 * 追加一个模块的全部条目。
 *
 * **先全部校验、再一次性写入**：边校验边写会让失败时 registry 停在半装载状态，
 * 那比静默覆盖更难查。失败条件（任一命中即返回，registry 保持不变）：
 *   - 空 handler（`handler == NULL`）；
 *   - opid 是 `BINRWIR_OPERATION_IDENTIFIER_NONE` 或 `..._FOREIGN`（结构标记不是
 *     算子，它们由解释器直接分派，注册进来会造出第二条分派路径）；
 *   - 与本表已有条目重复（**不覆盖**，诊断信息点名模块）；
 *   - 超出 `BINRWIR_REGISTRY_ENTRY_LIMIT`（INSUFFICIENT_CAPACITY，**不截断**）。
 */
runtimec_status_t binrwir_registry_append(
    binrwir_registry_t *registry, const char *module_name,
    const binrwir_registry_entry_t *entries, size_t entry_count,
    runtimec_error_t *error);

/*
 * 查找。返回：
 *   - SUCCESS     命中，`*output_entry` 非空；
 *   - UNSUPPORTED 表是好的，只是没有这个 opid（外部能力走 FOREIGN，不从这里出）；
 *   - INVALID     表被绕过装载写坏了（条目 handler 为空），或参数非法。
 * 两种失败必须能区分：前者是正常的外部能力，后者是 bug。
 */
runtimec_status_t
binrwir_registry_find(const binrwir_registry_t *registry,
                            binrwir_operation_identifier_t operation_identifier,
                            const binrwir_registry_entry_t **output_entry);

#ifdef __cplusplus
}
#endif

#endif
