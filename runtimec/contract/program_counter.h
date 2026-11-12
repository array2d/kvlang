#ifndef RUNTIMEC_CONTRACT_PROGRAM_COUNTER_H
#define RUNTIMEC_CONTRACT_PROGRAM_COUNTER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct runtimec_program_counter {
    int32_t stack_identifier;
    int32_t source_instruction_ordinal;
} runtimec_program_counter_t;

#define RUNTIMEC_PROGRAM_COUNTER_STACK_IDENTIFIER_NONE INT32_C(-1)
#define RUNTIMEC_PROGRAM_COUNTER_ORDINAL_NONE INT32_C(-1)

#define RUNTIMEC_PROGRAM_COUNTER_NONE                                    \
    ((runtimec_program_counter_t){                                       \
        RUNTIMEC_PROGRAM_COUNTER_STACK_IDENTIFIER_NONE,                  \
        RUNTIMEC_PROGRAM_COUNTER_ORDINAL_NONE})

#ifdef __cplusplus
}
#endif

#endif
