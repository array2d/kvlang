#ifndef RUNTIMEC_CONTRACT_LANGTYPE_H
#define RUNTIMEC_CONTRACT_LANGTYPE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "contract/status.h"
#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum runtimec_langtype_form {
    RUNTIMEC_LANGTYPE_FORM_SCALAR = 0,
    RUNTIMEC_LANGTYPE_FORM_CHARACTER,
    RUNTIMEC_LANGTYPE_FORM_STRING,
    RUNTIMEC_LANGTYPE_FORM_ARRAY,
    RUNTIMEC_LANGTYPE_FORM_TUPLE,
    
    RUNTIMEC_LANGTYPE_FORM_MAPPING,
    RUNTIMEC_LANGTYPE_FORM_UNRECOGNIZED
} runtimec_langtype_form_t;

typedef enum runtimec_langtype_scalar {
    RUNTIMEC_LANGTYPE_SCALAR_NONE = 0, 
    RUNTIMEC_LANGTYPE_SCALAR_SIGNED_INTEGER,
    RUNTIMEC_LANGTYPE_SCALAR_UNSIGNED_INTEGER,
    RUNTIMEC_LANGTYPE_SCALAR_FLOAT,
    RUNTIMEC_LANGTYPE_SCALAR_BOOLEAN
} runtimec_langtype_scalar_t;

typedef enum runtimec_langtype_char_encoding {
    RUNTIMEC_LANGTYPE_CHAR_ENCODING_NONE = 0,
    RUNTIMEC_LANGTYPE_CHAR_ENCODING_UTF8,
    RUNTIMEC_LANGTYPE_CHAR_ENCODING_UTF32,
    RUNTIMEC_LANGTYPE_CHAR_ENCODING_ASCII
} runtimec_langtype_char_encoding_t;

typedef struct runtimec_langtype {
    
    runtimec_string_view_t base;
    runtimec_langtype_form_t form;
    runtimec_langtype_scalar_t scalar;               
    uint32_t scalar_width;                           
    
    runtimec_langtype_char_encoding_t char_encoding; 
    
    int32_t dimension_count;
} runtimec_langtype_t;

runtimec_status_t runtimec_langtype_parse(runtimec_string_view_t text,
                                          runtimec_langtype_t *output);

#ifdef __cplusplus
}
#endif

#endif
