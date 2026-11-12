
#include "float_format.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RUNTIMEC_FLOAT_FORMAT_MAX_FRACTION_DIGITS 17

#define RUNTIMEC_FLOAT_FORMAT_FIXED_EXPONENT_MIN (-4)
#define RUNTIMEC_FLOAT_FORMAT_FIXED_EXPONENT_MAX 16

static bool runtimec_float_format_non_finite(double value, char *output,
                                             size_t capacity,
                                             size_t *output_length) {
    const char *text;

    if (!isnan(value) && !isinf(value)) {
        return false;
    }
    
    if (isnan(value)) {
        text = "nan";
    } else {
        text = signbit(value) != 0 ? "-inf" : "inf";
    }
    {
        const size_t length = strlen(text);

        if (length >= capacity) {
            return false;
        }
        memcpy(output, text, length);
        output[length] = '\0';
        *output_length = length;
    }
    return true;
}

static bool runtimec_float_format_shortest_digits(
    double value, runtimec_float_domain_t domain, char *digits,
    size_t digits_capacity, int *exponent, bool *negative) {
    int fraction;
    bool found = false;

    for (fraction = 0; fraction <= RUNTIMEC_FLOAT_FORMAT_MAX_FRACTION_DIGITS;
         ++fraction) {
        char candidate[64];
        const int written = snprintf(candidate, sizeof(candidate), "%.*e",
                                     fraction, value);

        if (written <= 0 || (size_t)written >= sizeof(candidate)) {
            return false;
        }
        if (domain == RUNTIMEC_FLOAT_DOMAIN_FLOAT32) {
            found = strtof(candidate, NULL) == (float)value;
        } else {
            found = strtod(candidate, NULL) == value;
        }
        if (!found) {
            continue;
        }
        
        {
            
            const char *sign = candidate[0] == '-' ? candidate + 1 : candidate;

            *negative = candidate[0] == '-';
            const char *dot = strchr(sign, '.');
            const char *mark = strchr(sign, 'e');
            size_t integral = (size_t)((dot != NULL ? dot : mark) - sign);
            size_t count = 0U;

            if (integral >= digits_capacity) {
                return false;
            }
            digits[count++] = sign[0];
            if (dot != NULL && mark != NULL) {
                const size_t fractional = (size_t)(mark - dot - 1);

                if (count + fractional >= digits_capacity) {
                    return false;
                }
                memcpy(digits + count, dot + 1, fractional);
                count += fractional;
            }
            digits[count] = '\0';
            *exponent = mark != NULL ? atoi(mark + 1) : 0;
            return true;
        }
        return true;
    }
    
    return false;
}

static void runtimec_float_format_trim_digits(char *digits) {
    size_t length = strlen(digits);

    while (length > 1U && digits[length - 1U] == '0') {
        digits[length - 1U] = '\0';
        --length;
    }
}

static bool runtimec_float_format_use_fixed(int exponent) {
    return exponent >= RUNTIMEC_FLOAT_FORMAT_FIXED_EXPONENT_MIN &&
           exponent < RUNTIMEC_FLOAT_FORMAT_FIXED_EXPONENT_MAX;
}

static bool runtimec_float_format_write_exponential(const char *digits,
                                                    int exponent,
                                                    bool negative, char *output,
                                                    size_t capacity,
                                                    size_t *output_length) {
    char tail[32];
    int written;

    if (digits[1] != '\0') {
        written = snprintf(tail, sizeof(tail), "e%c%02d",
                           exponent < 0 ? '-' : '+',
                           exponent < 0 ? -exponent : exponent);
    } else {
        written = snprintf(tail, sizeof(tail), "e%c%02d",
                           exponent < 0 ? '-' : '+',
                           exponent < 0 ? -exponent : exponent);
    }
    if (written <= 0 || (size_t)written >= sizeof(tail)) {
        return false;
    }
    {
        const size_t digits_length = strlen(digits);
        const size_t tail_length = (size_t)written;
        const size_t separated = digits[1] != '\0' ? 1U : 0U; 
        const size_t sign_length = negative ? 1U : 0U;
        const size_t total = sign_length + digits_length + separated + tail_length;

        if (total >= capacity) {
            return false;
        }
        {
            size_t cursor = 0U;

            if (negative) {
                output[cursor++] = '-';
            }
            output[cursor++] = digits[0];

            if (separated != 0U) {
                output[cursor++] = '.';
                memcpy(output + cursor, digits + 1, digits_length - 1U);
                cursor += digits_length - 1U;
            }
            memcpy(output + cursor, tail, tail_length);
            cursor += tail_length;
            output[cursor] = '\0';
            *output_length = cursor;
        }
    }
    return true;
}

static bool runtimec_float_format_write_fixed(const char *digits, int exponent,
                                              bool negative, char *output,
                                              size_t capacity,
                                              size_t *output_length) {
    const size_t digits_length = strlen(digits);
    const size_t sign_length = negative ? 1U : 0U;
    size_t cursor = 0U;
    size_t total;
    int ordinal;

    if (exponent >= 0) {
        const size_t integral = (size_t)exponent + 1U;
        const size_t fractional = digits_length > integral
                                      ? digits_length - integral
                                      : 0U;

        total = sign_length + integral +
                (fractional != 0U ? 1U + fractional : 2U);
    } else {
        const size_t leading_zeros = (size_t)(-exponent) - 1U;

        total = sign_length + 2U + leading_zeros + digits_length;
    }
    
    if (total >= capacity) {
        return false;
    }
    if (negative) {
        output[cursor++] = '-';
    }
    if (exponent >= 0) {
        const size_t integral = (size_t)exponent + 1U;

        for (ordinal = 0; ordinal < (int)integral; ++ordinal) {
            output[cursor++] = (size_t)ordinal < digits_length
                                   ? digits[ordinal]
                                   : '0';
        }
        if (digits_length > integral) {
            const size_t fractional = digits_length - integral;

            output[cursor++] = '.';
            memcpy(output + cursor, digits + integral, fractional);
            cursor += fractional;
        } else {
            
            output[cursor++] = '.';
            output[cursor++] = '0';
        }
    } else {
        const size_t leading_zeros = (size_t)(-exponent) - 1U;
        size_t pad;

        output[cursor++] = '0';
        output[cursor++] = '.';
        for (pad = 0U; pad < leading_zeros; ++pad) {
            output[cursor++] = '0';
        }
        memcpy(output + cursor, digits, digits_length);
        cursor += digits_length;
    }
    output[cursor] = '\0';
    *output_length = cursor;
    return true;
}

bool runtimec_float_format_core(double value, runtimec_float_domain_t domain,
                                char *digits, size_t digits_capacity,
                                int *exponent, bool *negative) {
    int computed_exponent = 0;
    bool computed_negative = false;

    if (digits == NULL || exponent == NULL || negative == NULL ||
        digits_capacity == 0U) {
        return false;
    }
    digits[0] = '\0';
    if (!runtimec_float_format_shortest_digits(value, domain, digits,
                                               digits_capacity,
                                               &computed_exponent,
                                               &computed_negative)) {
        return false;
    }
    
    runtimec_float_format_trim_digits(digits);
    *exponent = computed_exponent;
    *negative = computed_negative;
    return true;
}

bool runtimec_float_format_non_finite_text(double value, char *output,
                                           size_t capacity,
                                           size_t *output_length) {
    if (output_length == NULL) {
        return false;
    }
    *output_length = 0U;
    return runtimec_float_format_non_finite(value, output, capacity,
                                            output_length);
}

bool runtimec_float_format_shortest(double value, runtimec_float_domain_t domain,
                                    char *output, size_t capacity,
                                    size_t *output_length) {
    char digits[32];
    int exponent = 0;
    bool negative = false;

    if (output == NULL || output_length == NULL) {
        return false;
    }
    *output_length = 0U;
    if (capacity == 0U) {
        return false;
    }
    
    if (runtimec_float_format_non_finite(value, output, capacity,
                                         output_length)) {
        return true;
    }
    
    if (!runtimec_float_format_shortest_digits(value, domain, digits,
                                               sizeof(digits), &exponent,
                                               &negative)) {
        return false;
    }
    runtimec_float_format_trim_digits(digits);
    
    if (runtimec_float_format_use_fixed(exponent)) {
        
        return runtimec_float_format_write_fixed(digits, exponent, negative,
                                                  output, capacity,
                                                  output_length);
    }
    return runtimec_float_format_write_exponential(digits, exponent, negative,
                                                    output, capacity,
                                                    output_length);
}
