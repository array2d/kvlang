#ifndef RUNTIMEC_CONTRACT_CHECKPOINT_H
#define RUNTIMEC_CONTRACT_CHECKPOINT_H

/*
 * 组件：contract/checkpoint
 * 作用：一次 durable 提交所需的逻辑身份与状态：契约版本、source
 *       revision、单调 sequence、runtime/vthread/frame 逻辑标识符、逻辑
 *       程序计数器、durable status、outcome 与错误。checkpoint 只携带
 *       持久逻辑身份；禁止写入进程指针、机器返回地址、intern 标识符或
 *       机器码地址。
 * 联系：contract 只依赖自身；engine/route 在每条成功指令后组织
 *       checkpoint，经 host port 的 checkpoint 函数提交；host 适配层负责
 *       把它映射到 kvspace 的程序计数器、状态和错误槽。vmbin/BinRWIR JIT
 *       不直接提交，只把结构化 outcome 交给 route。
 * 约束：所有字符串字段都是 borrowed view；序列号由调用方单调递增。
 */

#include <stdbool.h>
#include <stdint.h>

#include "contract/error.h"
#include "contract/snapshot.h"
#include "contract/status.h"
#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct runtimec_checkpoint {
    runtimec_version_t contract_version;
    runtimec_source_revision_t source_revision; /* 提交所对应的 source 世代 */
    uint64_t sequence;                          /* 单调提交序号，用于幂等/对账 */
    runtimec_string_view_t runtime_identifier;
    runtimec_string_view_t vthread_identifier;
    runtimec_string_view_t frame_identifier;
    runtimec_string_view_t logical_program_counter;
    runtimec_string_view_t persistent_status;   /* running/paused/waiting/done/error */
    runtimec_status_t outcome;                  /* 本次提交的组件结果 */
    runtimec_error_t error;                     /* 失败时的错误详情 */
} runtimec_checkpoint_t;

/* 构造空 checkpoint：版本置当前值、视图置空、结果为成功、错误清空。 */
void runtimec_checkpoint_reset(runtimec_checkpoint_t *checkpoint);
/* 形状校验：二进制接口版本、结果枚举、全部视图与错误自洽。 */
bool runtimec_checkpoint_is_valid(const runtimec_checkpoint_t *checkpoint);

#ifdef __cplusplus
}
#endif

#endif
