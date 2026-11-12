#ifndef BINRWIR_ARGUMENT_H
#define BINRWIR_ARGUMENT_H

#include <stddef.h>
#include <stdint.h>

#include "contract/status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BINRWIR_BODY_ADDRESS_NONE UINT64_C(0)
#define BINRWIR_ARGUMENT_INDEX_NONE UINT32_MAX

#define BINRWIR_BINARY_INTERFACE_GENERATION 6U

typedef enum binrwir_reference_kind {
    BINRWIR_REFERENCE_INVALID = 0,
    BINRWIR_REFERENCE_PARAMETER_READ = 1,  
    BINRWIR_REFERENCE_PARAMETER_WRITE = 2, 
    BINRWIR_REFERENCE_LOCAL = 3,           
    
    BINRWIR_REFERENCE_STATIC = 4,
    BINRWIR_REFERENCE_LITERAL = 5,         
    BINRWIR_REFERENCE_FUNCTION = 6         
} binrwir_reference_kind_t;

typedef struct binrwir_argument {
    uint32_t argument_index;
    uint8_t reference_kind;
    
    uint8_t dereference;
    uint16_t slot;
    uint64_t payload;
} binrwir_argument_t;

runtimec_status_t binrwir_argument_create(
    uint32_t argument_index, binrwir_reference_kind_t reference_kind,
    uint16_t slot, uint64_t payload, binrwir_argument_t *output_argument);
runtimec_status_t binrwir_argument_validate(
    const binrwir_argument_t *argument);

_Static_assert(sizeof(binrwir_argument_t) == 16U,
               "binrwir arguments must remain 16 bytes");
_Static_assert(offsetof(binrwir_argument_t, argument_index) == 0U,
               "binrwir argidx must begin each argument");
_Static_assert(offsetof(binrwir_argument_t, reference_kind) == 4U,
               "binrwir reference kind must follow argidx");
_Static_assert(offsetof(binrwir_argument_t, dereference) == 5U,
               "binrwir dereference flag must follow the reference kind");
_Static_assert(offsetof(binrwir_argument_t, slot) == 6U,
               "binrwir slot must follow the reserved byte");
_Static_assert(offsetof(binrwir_argument_t, payload) == 8U,
               "binrwir payload must follow canonical alignment padding");

#ifdef __cplusplus
}
#endif

#endif
