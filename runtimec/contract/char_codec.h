#ifndef RUNTIMEC_CONTRACT_CHAR_CODEC_H
#define RUNTIMEC_CONTRACT_CHAR_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RUNTIMEC_CHAR_CODEC_UTF8_MAX_BYTES 4U

bool runtimec_char_utf32_to_utf8(const uint8_t *input, size_t input_size,
                                 char *output, size_t output_capacity,
                                 size_t *output_length);

bool runtimec_char_utf8_to_utf32(const uint8_t *input, size_t input_size,
                                 uint8_t *output, size_t output_capacity,
                                 size_t *output_length);

bool runtimec_char_ascii_to_utf8(const uint8_t *input, size_t input_size,
                                 char *output, size_t output_capacity,
                                 size_t *output_length);

#ifdef __cplusplus
}
#endif

#endif
