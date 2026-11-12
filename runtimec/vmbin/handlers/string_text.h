#ifndef VMBIN_HANDLERS_STRING_TEXT_H
#define VMBIN_HANDLERS_STRING_TEXT_H

#include <stddef.h>
#include <stdint.h>

#include "binrwir/binvthread_frame.h"
#include "contract/char_codec.h"
#include "contract/error.h"
#include "contract/status.h"
#include "contract/view.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STRING_TEXT_CODEPOINT_CAPACITY 1024U
#define STRING_TEXT_BYTES_CAPACITY \
    (STRING_TEXT_CODEPOINT_CAPACITY * RUNTIMEC_CHAR_CODEC_UTF8_MAX_BYTES)

typedef struct string_text {
    uint32_t codepoints[STRING_TEXT_CODEPOINT_CAPACITY];
    size_t count;
} string_text_t;

runtimec_status_t string_text_decode_argument(
    const binrwir_binvthread_frame_t *frame, const binrwir_argument_t *argument,
    string_text_t *output, runtimec_error_t *error);

runtimec_status_t string_text_decode(runtimec_bytes_view_t bytes,
                                     runtimec_string_view_t langtype,
                                     string_text_t *output);

bool string_text_decode_utf8(const uint8_t *bytes, size_t size,
                             string_text_t *output);
bool string_text_decode_utf32(const uint8_t *bytes, size_t size,
                              string_text_t *output);
bool string_text_decode_ascii(const uint8_t *bytes, size_t size,
                              string_text_t *output);

bool string_text_encoding_is_fixed_width(runtimec_string_view_t langtype);

runtimec_status_t string_text_encode(runtimec_string_view_t langtype,
                                     const string_text_t *text,
                                     uint8_t *output, size_t capacity,
                                     size_t *output_size);

runtimec_status_t string_text_encode_utf8(const string_text_t *text,
                                          uint8_t *output, size_t capacity,
                                          size_t *output_size);

runtimec_status_t string_text_common_encoding(runtimec_string_view_t left,
                                              runtimec_string_view_t right,
                                              runtimec_string_view_t *output);

#ifdef __cplusplus
}
#endif

#endif
