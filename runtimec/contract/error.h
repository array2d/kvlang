#ifndef RUNTIMEC_CONTRACT_ERROR_H
#define RUNTIMEC_CONTRACT_ERROR_H

/*
 * 组件：contract/error
 * 作用：跨组件统一的错误形状：component status + 稳定错误域 + 稳定 code +
 *       借用消息视图 + 逻辑程序计数器。执行或编译错误必须保留稳定分类
 *       与对应的 source 程序计数器。
 * 联系：contract 层只依赖自身；host 用 HOST/STORAGE/CHECKPOINT/EXTENSION
 *       域包装 kvspace/扩展错误，vmkv（编译诊断）、vmbin（执行错误）、
 *       engine/route（提交错误）都通过本结构把错误交给上层。
 * 注意：message 与 logical_program_counter 都是借用视图，生命周期归调用链上的
 *       缓冲区所有；不得在这里分配或释放。
 */

#include <stdbool.h>
#include <stdint.h>

#include "contract/status.h"
#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 错误来源域；跨域报告便于上层区分契约错误、宿主错误与扩展错误。 */
typedef enum runtimec_error_domain {
    RUNTIMEC_ERROR_DOMAIN_NONE = 0,
    RUNTIMEC_ERROR_DOMAIN_CONTRACT = 1,
    RUNTIMEC_ERROR_DOMAIN_HOST = 2,
    RUNTIMEC_ERROR_DOMAIN_STORAGE = 3,
    RUNTIMEC_ERROR_DOMAIN_CHECKPOINT = 4,
    RUNTIMEC_ERROR_DOMAIN_EXTENSION = 5
} runtimec_error_domain_t;

/* 跨组件错误值；所有字段都是普通数据或借用视图，可安全拷贝。 */
struct runtimec_error {
    runtimec_status_t status;        /* 组件状态，见 contract/status.h */
    runtimec_error_domain_t domain;  /* 错误来源域 */
    int64_t code;                    /* 稳定错误码，禁止用 -1 复用多义 */
    runtimec_string_view_t message;  /* borrowed UTF-8 消息 */
    runtimec_string_view_t logical_program_counter;
};

/* 置为空错误（SUCCESS/NONE/0/空视图）；不得依赖调用方预清零。 */
void runtimec_error_reset(runtimec_error_t *error);
/* 写入一条错误；视图不复制，调用方须保证其生命周期覆盖本次调用链。 */
void runtimec_error_set(runtimec_error_t *error, runtimec_status_t status,
                        runtimec_error_domain_t domain, int64_t code,
                        runtimec_string_view_t message,
                        runtimec_string_view_t logical_program_counter);
/* 结构是否自洽（状态枚举、域范围、视图有效性）。 */
bool runtimec_error_is_valid(const runtimec_error_t *error);
/* 是否携带非 SUCCESS 状态。 */
bool runtimec_error_is_set(const runtimec_error_t *error);

#ifdef __cplusplus
}
#endif

#endif
