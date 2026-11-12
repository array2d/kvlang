#ifndef RUNTIMEC_CONTRACT_FLOAT_FORMAT_H
#define RUNTIMEC_CONTRACT_FLOAT_FORMAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum runtimec_float_domain {
    RUNTIMEC_FLOAT_DOMAIN_FLOAT32 = 0, 
    RUNTIMEC_FLOAT_DOMAIN_FLOAT64 = 1  
} runtimec_float_domain_t;

bool runtimec_float_format_shortest(double value, runtimec_float_domain_t domain,
                                    char *output, size_t capacity,
                                    size_t *output_length);

bool runtimec_float_format_core(double value, runtimec_float_domain_t domain,
                                char *digits, size_t digits_capacity,
                                int *exponent, bool *negative);

bool runtimec_float_format_non_finite_text(double value, char *output,
                                           size_t capacity,
                                           size_t *output_length);

#ifdef __cplusplus
}
#endif

#endif
