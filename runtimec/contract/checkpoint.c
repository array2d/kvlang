/*
 * 组件：contract/checkpoint 的实现
 * 作用：checkpoint 的空值构造与形状校验；不执行提交、不接触 kvspace。
 * 联系：engine/route 组装后交给 host 的 checkpoint 能力；host 只执行
 *       已组织好的提交，不自行决定 durable 状态。
 */

#include "contract/checkpoint.h"

void runtimec_checkpoint_reset(runtimec_checkpoint_t *checkpoint) {
    if (checkpoint != NULL) {
        checkpoint->contract_version = runtimec_contract_version();
        runtimec_source_revision_reset(&checkpoint->source_revision);
        checkpoint->sequence = 0U;
        checkpoint->runtime_identifier = runtimec_string_view_make(NULL, 0U);
        checkpoint->vthread_identifier = runtimec_string_view_make(NULL, 0U);
        checkpoint->frame_identifier = runtimec_string_view_make(NULL, 0U);
        checkpoint->logical_program_counter =
            RUNTIMEC_PROGRAM_COUNTER_NONE;
        checkpoint->persistent_status = runtimec_string_view_make(NULL, 0U);
        checkpoint->outcome = RUNTIMEC_STATUS_SUCCESS;
        runtimec_error_reset(&checkpoint->error);
    }
}

bool runtimec_checkpoint_is_valid(const runtimec_checkpoint_t *checkpoint) {
    return checkpoint != NULL &&
           checkpoint->contract_version.binary_interface ==
               RUNTIMEC_CONTRACT_BINARY_INTERFACE_VERSION &&
           runtimec_status_is_valid(checkpoint->outcome) &&
           runtimec_string_view_is_valid(checkpoint->runtime_identifier) &&
           runtimec_string_view_is_valid(checkpoint->vthread_identifier) &&
           runtimec_string_view_is_valid(checkpoint->frame_identifier) &&
           checkpoint->logical_program_counter.source_instruction_ordinal >=
               RUNTIMEC_PROGRAM_COUNTER_ORDINAL_NONE &&
           runtimec_string_view_is_valid(checkpoint->persistent_status) &&
           runtimec_error_is_valid(&checkpoint->error);
}
