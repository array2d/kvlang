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

#include <stddef.h>
#include <stdint.h>

#include "binrwir/execution_exit.h"
#include "binrwir/execution_frame.h"
#include "binrwir/instruction.h"
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
    uint32_t operation_identifier;
    uint32_t flags;
    binrwir_registry_handler_t handler;
} binrwir_registry_entry_t;

/*
 * 条目上限：catalog 当前 130 行，留余量到 192（够标准库扩展）。
 * 生成器 `--check` 读取本宏并与实际行数比对；运行期的超限失败是兜底。
 * 192 × 16 字节 = 3072 字节，随 session 走，符合 no-malloc 基线。
 */
#define BINRWIR_REGISTRY_ENTRY_LIMIT 192U

typedef struct binrwir_registry {
    binrwir_registry_entry_t storage[BINRWIR_REGISTRY_ENTRY_LIMIT];
    size_t entry_count;
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
 *   - `operation_identifier == BINRWIR_IDENTIFIER_NONE`；
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
                            uint32_t operation_identifier,
                            const binrwir_registry_entry_t **output_entry);

#ifdef __cplusplus
}
#endif

#endif
