#ifndef RUNTIMEC_CONTRACT_SNAPSHOT_H
#define RUNTIMEC_CONTRACT_SNAPSHOT_H

/*
 * 组件：contract/snapshot
 * 作用：一次编译/执行所依据的权威快照：契约版本、store 实例、source
 *       generation、schema/指令集版本、函数键与逻辑程序计数器。kvspace
 *       明文是唯一持久语义事实源；snapshot 只用于判断派生状态是否仍对应
 *       当前事实，不能替代或修改事实源。
 * 联系：contract 层只依赖自身；host 提供原始事实，engine 在编译/执行前
 *       取快照，vmkv/vmbin/BinRWIR Compiler 只消费快照与 generation，不自己读
 *       kvspace；engine 用本结构构造 compilation repository key。
 * 约束：snapshot 不携带进程地址或 kvspace 内部句柄。
 */

#include <stdbool.h>
#include <stdint.h>

#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RUNTIMEC_CONTRACT_VERSION_MAJOR 1U
#define RUNTIMEC_CONTRACT_VERSION_MINOR 0U
#define RUNTIMEC_CONTRACT_VERSION_PATCH 0U
#define RUNTIMEC_CONTRACT_BINARY_INTERFACE_VERSION 1U

/* 契约二进制接口版本；跨进程或跨库边界时用于拒绝不一致的快照。 */
typedef struct runtimec_version {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
    uint16_t binary_interface;
} runtimec_version_t;

/* 权威 source 的世代：store 实例 + generation + schema/指令集版本。
 * 任一变化都必须让派生 Program、view、JIT plan 与机器码失效。 */
typedef struct runtimec_source_revision {
    uint64_t instance_identifier;
    uint64_t generation;
    uint32_t schema_version;
    uint32_t instruction_set_version;
} runtimec_source_revision_t;

/* 一次编译/执行的权威身份；逻辑位置与函数键均为借用视图。 */
typedef struct runtimec_snapshot {
    runtimec_version_t contract_version;
    runtimec_source_revision_t source_revision;
    runtimec_key_view_t function_key;
    runtimec_string_view_t logical_program_counter;
} runtimec_snapshot_t;

runtimec_version_t runtimec_contract_version(void);
/* 归零为「未知来源」；不是有效 source revision，但可作为默认构造值。 */
void runtimec_source_revision_reset(runtimec_source_revision_t *revision);
/* 置为空快照（版本 + 空视图）；不分配。 */
void runtimec_snapshot_reset(runtimec_snapshot_t *snapshot);
/* 形状校验：二进制接口版本匹配且两个视图有效；不查询外部事实。 */
bool runtimec_snapshot_is_valid(const runtimec_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif
