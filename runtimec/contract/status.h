#ifndef RUNTIMEC_CONTRACT_STATUS_H
#define RUNTIMEC_CONTRACT_STATUS_H

/*
 * 组件：contract/status
 * 作用：声明跨组件统一的一次调用结果辅助函数。状态枚举定义在唯一公共头
 *       kvlang/runtimec.h；SUCCESS 只能表示请求真实完成。
 * 联系：host、vmbin、vmkv、vmjit 与 engine 共用同一状态词汇。
 *       它不是三类状态中的另外两类：
 *       - execution exit（binrwir/execution_exit.h）；
 *       - durable status（host/engine 通过 checkpoint 提交的 vthread 状态）。
 *       三者的转换只能由 engine/route.c 与 interface/public_interface.c
 *       显式完成，其他组件不得猜测或改写状态类别。
 * 约束：失败路径必须把输出参数清零或恢复到文档化状态；不得用同一个 -1
 *       同时表示 unsupported、yield、invalid 与 error。
 */

#include <stdbool.h>

#include <kvlang/runtimec.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct runtimec_error runtimec_error_t;

/* 枚举值是否落在已定义范围内（用于校验跨组件边界传入的状态）。 */
bool runtimec_status_is_valid(runtimec_status_t status);
/* 是否为失败类；YIELD 与 WAIT 是结构化控制流。 */
bool runtimec_status_is_failure(runtimec_status_t status);
/* 稳定诊断名；未定义值返回 "invalid_status"，不返回 NULL。 */
const char *runtimec_status_name(runtimec_status_t status);

#ifdef __cplusplus
}
#endif

#endif
