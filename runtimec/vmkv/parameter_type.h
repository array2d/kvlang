#ifndef RUNTIMEC_VMKV_PARAMETER_TYPE_H
#define RUNTIMEC_VMKV_PARAMETER_TYPE_H

#include <stdint.h>

#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum vmkv_parameter_type_result {
    VMKV_PARAMETER_TYPE_RESULT_INVALID = 0,
    
    VMKV_PARAMETER_TYPE_RESULT_NONE = 1,
    
    VMKV_PARAMETER_TYPE_RESULT_SINGLE = 2,
    
    VMKV_PARAMETER_TYPE_RESULT_UNION = 3,
    
    VMKV_PARAMETER_TYPE_RESULT_UNMAPPED = 4
} vmkv_parameter_type_result_t;

vmkv_parameter_type_result_t vmkv_parameter_type_classify(
    runtimec_string_view_t langtype, uint32_t *output_type_set);

runtimec_string_view_t vmkv_parameter_type_name(uint32_t type_set);

#ifdef __cplusplus
}
#endif

#endif
