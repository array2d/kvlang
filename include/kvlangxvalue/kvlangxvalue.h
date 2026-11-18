
#ifndef KVLANG_KVLANGXVALUE_H
#define KVLANG_KVLANGXVALUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kvspace/kvspace_xvalue.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KVLANG_XVALUE_ATTR_TAG_LANGTYPE 1u

static inline uint16_t kvlangXvalueAttrLangtype(const uint8_t *wire,
                                                const uint8_t **out) {
    if (out)
        *out = NULL;
    const kvspaceXvalueHead *h = (const kvspaceXvalueHead *)(const void *)wire;
    const uint64_t total = kvspaceXvalueHeadTotalLen(h);
    if (total == 0)
        return 0;
    const uint8_t *langtype = NULL;
    uint16_t langtype_len = 0;
    if (kvspaceXvalueGetAttr(wire, total, KVLANG_XVALUE_ATTR_TAG_LANGTYPE,
                             &langtype, &langtype_len) != 0)
        return 0;
    if (out)
        *out = langtype;
    return langtype_len;
}

static inline uint16_t kvlangXvalueAttrLangtypeBounded(const uint8_t *wire,
                                                       uint64_t wire_len,
                                                       const uint8_t **out) {
    if (out)
        *out = NULL;
    const uint8_t *langtype = NULL;
    uint16_t langtype_len = 0;
    if (kvspaceXvalueGetAttr(wire, wire_len, KVLANG_XVALUE_ATTR_TAG_LANGTYPE,
                             &langtype, &langtype_len) != 0)
        return 0;
    if (out)
        *out = langtype;
    return langtype_len;
}

uint8_t *kvlangxvalueNew(void *handle, const char *key, uint64_t body_len,
                         uint16_t attr_len);

int kvlangxvalueWriteBody(uint8_t *wire, const uint8_t *body,
                          uint64_t body_len);

int kvlangxvalueWriteLangtype(uint8_t *wire, uint64_t body_len, char prefix,
                              const char *langtype);

uint8_t *kvlangXvalueNewT(void *handle, const char *key,
                               const uint8_t *raw, uint32_t raw_len,
                               const char *langtype);

uint8_t *kvlangXvalueNewBufferArray(void *handle, const char *key,
                                    const uint8_t *data, uint64_t data_len,
                                    const char *langtype);

uint8_t *kvlangXvalueNewCompactArray(void *handle, const char *key,
                                     const uint64_t *dims, uint32_t ndim,
                                     const uint8_t *raw, uint64_t raw_len,
                                     const char *langtype);
uint8_t *kvlangXvalueNewPtr(void *handle, const char *key,
                            const uint8_t *path, uint32_t path_len,
                            const char *langtype);
uint8_t *kvlangXvalueNewCode(void *handle, const char *key,
                             const uint8_t *body, uint64_t body_len,
                             const char *langtype);

uint8_t *kvlangXvalueHeadReserve(void *handle, const char *key,
                                 uint64_t body_len, const char *langtype);

uint8_t *kvlangXvalueNewPtrFromKindexpr(void *handle, const char *key,
                                        const char *target_kindexpr,
                                        const char *target);
uint8_t *kvlangXvalueNewChar(void *handle, const char *key,
                             const uint8_t *bytes, uint32_t len,
                             const char *langtype);
uint8_t *kvlangXvalueNewBool(void *handle, const char *key, uint8_t v,
                             const char *langtype);
uint8_t *kvlangXvalueNewInt64(void *handle, const char *key, int64_t v,
                              const char *langtype);
uint8_t *kvlangXvalueNewFloat64(void *handle, const char *key, double v,
                                const char *langtype);

typedef enum kvlangXvalueTextFailure {
    KVLANG_XVALUE_TEXT_OK = 0,
    KVLANG_XVALUE_TEXT_BAD_LANGTYPE,
    KVLANG_XVALUE_TEXT_UNSUPPORTED_TYPE,
    KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT,
    KVLANG_XVALUE_TEXT_OUT_OF_RANGE,
    KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL
} kvlangXvalueTextFailure;

bool kvlangXvalueTextToBytes(const char *langtype, size_t langtype_length,
                             const char *value_text, size_t value_text_length,
                             uint8_t *output, size_t output_capacity,
                             size_t *output_length,
                             kvlangXvalueTextFailure *failure);

bool kvlangXvalueTextFromBytes(const char *langtype, size_t langtype_length,
                               const uint8_t *body, size_t body_length,
                               char *output, size_t output_capacity,
                               size_t *output_length,
                               kvlangXvalueTextFailure *failure);

const char *kvlangXvalueTextFailureText(kvlangXvalueTextFailure failure);

#define KVLANG_KIND_NONE       "None"
#define KVLANG_KIND_BOOL       "bool"
#define KVLANG_KIND_INT8       "int8"
#define KVLANG_KIND_INT16      "int16"
#define KVLANG_KIND_INT32      "int32"
#define KVLANG_KIND_INT64      "int64"
#define KVLANG_KIND_UINT8      "uint8"
#define KVLANG_KIND_UINT16     "uint16"
#define KVLANG_KIND_UINT32     "uint32"
#define KVLANG_KIND_UINT64     "uint64"
#define KVLANG_KIND_FLOAT32    "float32"
#define KVLANG_KIND_FLOAT64    "float64"
#define KVLANG_KIND_CHAR       "char/utf32"
#define KVLANG_KIND_CHAR_UTF8  "char/utf8"
#define KVLANG_KIND_CHAR_ASCII "char/ascii"
#define KVLANG_KIND_MAP        "stringkeymap"
#define KVLANG_KIND_RWIR       "rwir"
#define KVLANG_KIND_RWFUNC     "rwfunc"
#define KVLANG_KIND_DEF_RWIR   "def rwir"
#define KVLANG_KIND_SCOPE      "scope"
#define KVLANG_KIND_TIME       "time"
#define KVLANG_KIND_DURATION   "duration"
#define KVLANG_RUNTIME_MEMBER_SEP "\xE2\x80\xA5"

#define KVLANG_KV(X) \
    X(KVLANG_KIND_NONE) \
    X(KVLANG_KIND_BOOL) \
    X(KVLANG_KIND_INT8) \
    X(KVLANG_KIND_INT16) \
    X(KVLANG_KIND_INT32) \
    X(KVLANG_KIND_INT64) \
    X(KVLANG_KIND_UINT8) \
    X(KVLANG_KIND_UINT16) \
    X(KVLANG_KIND_UINT32) \
    X(KVLANG_KIND_UINT64) \
    X(KVLANG_KIND_FLOAT32) \
    X(KVLANG_KIND_FLOAT64) \
    X(KVLANG_KIND_CHAR) \
    X(KVLANG_KIND_CHAR_UTF8) \
    X(KVLANG_KIND_CHAR_ASCII) \
    X(KVLANG_KIND_MAP) \
    X(KVLANG_KIND_RWIR) \
    X(KVLANG_KIND_RWFUNC) \
    X(KVLANG_KIND_DEF_RWIR) \
    X(KVLANG_KIND_SCOPE) \
    X(KVLANG_KIND_TIME) \
    X(KVLANG_KIND_DURATION) \
    X(KVLANG_RUNTIME_MEMBER_SEP)

const char *kvlangConstLanguage(const char *name);

#ifdef KVLANG_XVALUE_IMPLEMENTATION
#include "langtype.h"
#include "char_codec.h"


#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum kvlangXvalueFloatDomain {
    KVLANG_XVALUE_FLOAT_DOMAIN_FLOAT32 = 0,
    KVLANG_XVALUE_FLOAT_DOMAIN_FLOAT64 = 1
} kvlangXvalueFloatDomain;

#include "char_codec.h"
#include "langtype.h"

static bool float_shortest(double value, kvlangXvalueFloatDomain domain,
                           char *output, size_t capacity,
                           size_t *output_length);

static void write_little_endian(uint8_t *output, uint64_t value, uint32_t width) {
    for (uint32_t index = 0; index < width; index++) {
        output[index] = (uint8_t)(value >> (8 * index));
    }
}

static uint64_t read_little_endian(const uint8_t *input, uint32_t width) {
    uint64_t value = 0;
    for (uint32_t index = 0; index < width; index++) {
        value |= (uint64_t)input[index] << (8 * index);
    }
    return value;
}

static bool fail(kvlangXvalueTextFailure *failure,
                 kvlangXvalueTextFailure reason, size_t *output_length) {
    if (failure) {
        *failure = reason;
    }
    if (output_length) {
        *output_length = 0;
    }
    return false;
}

static bool signed_to_bytes(uint32_t width, kvlangXvalueStringView text,
                            uint8_t *output, size_t capacity,
                            size_t *output_length,
                            kvlangXvalueTextFailure *failure) {
    char buffer[32];
    if (text.size + 1 > sizeof buffer ||
        snprintf(buffer, sizeof buffer, "%.*s", (int)text.size, text.data) < 0) {
        return fail(failure, KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT, output_length);
    }
    char *end = NULL;
    errno = 0;
    const long long value = strtoll(buffer, &end, 10);
    if (end == buffer || *end != '\0' || errno == ERANGE) {
        return fail(failure, KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT, output_length);
    }

    if (width < 8) {
        const long long limit = 1LL << (width * 8 - 1);
        if (value < -limit || value >= limit) {
            return fail(failure, KVLANG_XVALUE_TEXT_OUT_OF_RANGE, output_length);
        }
    }
    if (output == NULL) {
        if (capacity != 0) {
            return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL, output_length);
        }
        *output_length = width;
        return true;
    }
    if (capacity < width) {
        return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL, output_length);
    }
    write_little_endian(output, (uint64_t)value, width);
    *output_length = width;
    if (failure) {
        *failure = KVLANG_XVALUE_TEXT_OK;
    }
    return true;
}

static bool unsigned_to_bytes(uint32_t width, kvlangXvalueStringView text,
                              uint8_t *output, size_t capacity,
                              size_t *output_length,
                              kvlangXvalueTextFailure *failure) {
    char buffer[32];
    if (text.size + 1 > sizeof buffer ||
        snprintf(buffer, sizeof buffer, "%.*s", (int)text.size, text.data) < 0) {
        return fail(failure, KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT, output_length);
    }

    if (buffer[0] == '-') {
        return fail(failure, KVLANG_XVALUE_TEXT_OUT_OF_RANGE, output_length);
    }
    char *end = NULL;
    errno = 0;
    const unsigned long long value = strtoull(buffer, &end, 10);
    if (end == buffer || *end != '\0' || errno == ERANGE) {
        return fail(failure, KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT, output_length);
    }
    if (width < 8 && value >= (1ULL << (width * 8))) {
        return fail(failure, KVLANG_XVALUE_TEXT_OUT_OF_RANGE, output_length);
    }
    if (output == NULL) {
        if (capacity != 0) {
            return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL, output_length);
        }
        *output_length = width;
        return true;
    }
    if (capacity < width) {
        return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL, output_length);
    }
    write_little_endian(output, value, width);
    *output_length = width;
    if (failure) {
        *failure = KVLANG_XVALUE_TEXT_OK;
    }
    return true;
}

static bool bool_to_bytes(kvlangXvalueStringView text, uint8_t *output,
                          size_t capacity, size_t *output_length,
                          kvlangXvalueTextFailure *failure) {

    uint8_t value = 0;
    if (text.size == 4 && memcmp(text.data, "true", 4) == 0) {
        value = 1;
    } else if (text.size == 5 && memcmp(text.data, "false", 5) == 0) {
        value = 0;
    } else {
        return fail(failure, KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT, output_length);
    }
    if (output == NULL) {
        if (capacity != 0) {
            return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL, output_length);
        }
        *output_length = 1;
        return true;
    }
    if (capacity < 1) {
        return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL, output_length);
    }
    output[0] = value;
    *output_length = 1;
    if (failure) {
        *failure = KVLANG_XVALUE_TEXT_OK;
    }
    return true;
}

static bool float_to_bytes(kvlangXvalueStringView text, uint32_t width,
                           uint8_t *output, size_t capacity,
                           size_t *output_length,
                           kvlangXvalueTextFailure *failure) {
    char buffer[64];
    if (text.size + 1 > sizeof buffer ||
        snprintf(buffer, sizeof buffer, "%.*s", (int)text.size, text.data) < 0) {
        return fail(failure, KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT, output_length);
    }
    char *end = NULL;
    errno = 0;
    const double value = strtod(buffer, &end);
    if (end == buffer || *end != '\0' || errno == ERANGE) {
        return fail(failure, KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT, output_length);
    }
    if (output == NULL) {
        if (capacity != 0) {
            return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL, output_length);
        }
        *output_length = width;
        return true;
    }
    if (capacity < width) {
        return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL, output_length);
    }
    if (width == 4) {
        const float narrowed = (float)value;
        uint32_t bits = 0;
        memcpy(&bits, &narrowed, sizeof bits);
        write_little_endian(output, bits, 4);
    } else {
        uint64_t bits = 0;
        memcpy(&bits, &value, sizeof bits);
        write_little_endian(output, bits, 8);
    }
    *output_length = width;
    if (failure) {
        *failure = KVLANG_XVALUE_TEXT_OK;
    }
    return true;
}

static bool characters_to_bytes(kvlangXvalueCharEncoding encoding,
                                kvlangXvalueStringView text, uint8_t *output,
                                size_t capacity, size_t *output_length,
                                kvlangXvalueTextFailure *failure) {
    switch (encoding) {
    case KVLANG_XVALUE_CHAR_ENCODING_ASCII:
        for (size_t index = 0; index < text.size; index++) {
            if ((unsigned char)text.data[index] >= 0x80U) {
                return fail(failure, KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT,
                            output_length);
            }
        }
        if (output == NULL) {
            if (capacity != 0) {
                return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL,
                            output_length);
            }
            *output_length = text.size;
            return true;
        }
        if (capacity < text.size) {
            return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL, output_length);
        }
        memcpy(output, text.data, text.size);
        *output_length = text.size;
        if (failure) {
            *failure = KVLANG_XVALUE_TEXT_OK;
        }
        return true;
    case KVLANG_XVALUE_CHAR_ENCODING_UTF8:
        if (output == NULL) {
            if (capacity != 0) {
                return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL,
                            output_length);
            }
            *output_length = text.size;
            return true;
        }
        if (capacity < text.size) {
            return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL, output_length);
        }

        {
            size_t checked = 0;

            uint8_t *scratch = (uint8_t *)malloc(text.size * 4U + 4U);
            if (scratch == NULL) {
                return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL,
                            output_length);
            }
            const bool valid = kvlangXvalueCharUtf8ToUtf32(
                (const uint8_t *)text.data, text.size, scratch,
                text.size * 4U + 4U, &checked);
            free(scratch);
            if (!valid) {
                return fail(failure, KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT,
                            output_length);
            }
        }
        memcpy(output, text.data, text.size);
        *output_length = text.size;
        if (failure) {
            *failure = KVLANG_XVALUE_TEXT_OK;
        }
        return true;
    case KVLANG_XVALUE_CHAR_ENCODING_UTF32: {

        uint8_t *codepoints = (uint8_t *)malloc(text.size * 4U + 4U);
        if (codepoints == NULL) {
            return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL, output_length);
        }
        size_t codepoint_bytes = 0;
        const bool valid = kvlangXvalueCharUtf8ToUtf32(
            (const uint8_t *)text.data, text.size, codepoints, text.size * 4U + 4U,
            &codepoint_bytes);
        if (!valid) {
            free(codepoints);
            return fail(failure, KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT, output_length);
        }
        if (output == NULL) {
            free(codepoints);
            if (capacity != 0) {
                return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL,
                            output_length);
            }
            *output_length = codepoint_bytes;
            return true;
        }
        if (capacity < codepoint_bytes) {
            free(codepoints);
            return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL, output_length);
        }
        memcpy(output, codepoints, codepoint_bytes);
        free(codepoints);
        *output_length = codepoint_bytes;
        if (failure) {
            *failure = KVLANG_XVALUE_TEXT_OK;
        }
        return true;
    }
    case KVLANG_XVALUE_CHAR_ENCODING_NONE:
        break;
    }
    return fail(failure, KVLANG_XVALUE_TEXT_UNSUPPORTED_TYPE, output_length);
}

static bool text_to_bytes_impl(kvlangXvalueStringView langtype,
                                  kvlangXvalueStringView value_text,
                                  uint8_t *output, size_t output_capacity,
                                  size_t *output_length,
                                  kvlangXvalueTextFailure *failure) {
    if (failure) {
        *failure = KVLANG_XVALUE_TEXT_OK;
    }
    if (output_length == NULL) {
        return fail(failure, KVLANG_XVALUE_TEXT_BAD_LANGTYPE, NULL);
    }
    *output_length = 0;

    kvlangXvalueLangtype parsed;
    kvlangXvalueLangtypeParse(langtype, &parsed);

    if (parsed.form == KVLANG_XVALUE_FORM_SCALAR) {
        switch (parsed.scalar) {
        case KVLANG_XVALUE_SCALAR_SIGNED_INTEGER:
            return signed_to_bytes(parsed.scalar_width, value_text, output,
                                   output_capacity, output_length, failure);
        case KVLANG_XVALUE_SCALAR_UNSIGNED_INTEGER:
            return unsigned_to_bytes(parsed.scalar_width, value_text, output,
                                     output_capacity, output_length, failure);
        case KVLANG_XVALUE_SCALAR_FLOAT:
            return float_to_bytes(value_text, parsed.scalar_width, output,
                                  output_capacity, output_length, failure);
        case KVLANG_XVALUE_SCALAR_BOOLEAN:
            return bool_to_bytes(value_text, output, output_capacity,
                                 output_length, failure);
        case KVLANG_XVALUE_SCALAR_NONE:
            break;
        }
        return fail(failure, KVLANG_XVALUE_TEXT_BAD_LANGTYPE, output_length);
    }

    if (parsed.form == KVLANG_XVALUE_FORM_CHARACTER ||
        parsed.form == KVLANG_XVALUE_FORM_STRING) {
        return characters_to_bytes(parsed.char_encoding, value_text, output,
                                   output_capacity, output_length, failure);
    }

    return fail(failure, KVLANG_XVALUE_TEXT_UNSUPPORTED_TYPE, output_length);
}

static bool finish_text(const char *source, size_t length, char *output,
                        size_t capacity, size_t *output_length,
                        kvlangXvalueTextFailure *failure) {
    if (output == NULL) {
        if (capacity != 0) {
            return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL, output_length);
        }
        *output_length = length;
        return true;
    }
    if (capacity <= length) {
        return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL, output_length);
    }
    memcpy(output, source, length);
    output[length] = '\0';
    *output_length = length;
    if (failure) {
        *failure = KVLANG_XVALUE_TEXT_OK;
    }
    return true;
}

static bool text_from_bytes_impl(kvlangXvalueStringView langtype,
                                 const uint8_t *body_data, size_t body_size,
                                 char *output,
                                 size_t output_capacity, size_t *output_length,
                                 kvlangXvalueTextFailure *failure) {
    if (failure) {
        *failure = KVLANG_XVALUE_TEXT_OK;
    }
    if (output_length == NULL) {
        return fail(failure, KVLANG_XVALUE_TEXT_BAD_LANGTYPE, NULL);
    }
    *output_length = 0;

    kvlangXvalueLangtype parsed;
    kvlangXvalueLangtypeParse(langtype, &parsed);

    if (parsed.form == KVLANG_XVALUE_FORM_SCALAR) {
        char buffer[64];
        int written = 0;
        switch (parsed.scalar) {
        case KVLANG_XVALUE_SCALAR_SIGNED_INTEGER: {
            if (body_size != parsed.scalar_width) {
                return fail(failure, KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT,
                            output_length);
            }
            const uint64_t raw = read_little_endian(body_data, parsed.scalar_width);
            int64_t value = (int64_t)raw;

            if (parsed.scalar_width < 8) {
                const uint32_t shift = 64U - parsed.scalar_width * 8U;
                value = ((int64_t)(raw << shift)) >> shift;
            }
            written = snprintf(buffer, sizeof buffer, "%lld", (long long)value);
            break;
        }
        case KVLANG_XVALUE_SCALAR_UNSIGNED_INTEGER: {
            if (body_size != parsed.scalar_width) {
                return fail(failure, KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT,
                            output_length);
            }
            const uint64_t value = read_little_endian(body_data, parsed.scalar_width);
            written = snprintf(buffer, sizeof buffer, "%llu",
                               (unsigned long long)value);
            break;
        }
        case KVLANG_XVALUE_SCALAR_FLOAT: {
            if (body_size != parsed.scalar_width) {
                return fail(failure, KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT,
                            output_length);
            }
            const uint64_t raw = read_little_endian(body_data, parsed.scalar_width);
            double value = 0.0;
            if (parsed.scalar_width == 4) {
                const uint32_t bits = (uint32_t)raw;
                float narrowed = 0.0f;
                memcpy(&narrowed, &bits, sizeof narrowed);
                value = (double)narrowed;
            } else {
                memcpy(&value, &raw, sizeof value);
            }
            size_t float_length = 0;
            const kvlangXvalueFloatDomain domain =
                parsed.scalar_width == 4 ? KVLANG_XVALUE_FLOAT_DOMAIN_FLOAT32
                                         : KVLANG_XVALUE_FLOAT_DOMAIN_FLOAT64;
            if (!float_shortest(value, domain, buffer,
                                                sizeof buffer, &float_length)) {
                return fail(failure, KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT,
                            output_length);
            }
            return finish_text(buffer, float_length, output, output_capacity,
                               output_length, failure);
        }
        case KVLANG_XVALUE_SCALAR_BOOLEAN: {
            if (body_size != 1) {
                return fail(failure, KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT,
                            output_length);
            }
            const char *text = body_data[0] ? "true" : "false";
            return finish_text(text, strlen(text), output, output_capacity,
                               output_length, failure);
        }
        case KVLANG_XVALUE_SCALAR_NONE:
            return fail(failure, KVLANG_XVALUE_TEXT_BAD_LANGTYPE, output_length);
        }
        if (written <= 0 || (size_t)written >= sizeof buffer) {
            return fail(failure, KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT, output_length);
        }
        return finish_text(buffer, (size_t)written, output, output_capacity,
                           output_length, failure);
    }

    if (parsed.form == KVLANG_XVALUE_FORM_CHARACTER ||
        parsed.form == KVLANG_XVALUE_FORM_STRING) {
        switch (parsed.char_encoding) {
        case KVLANG_XVALUE_CHAR_ENCODING_UTF8:
        case KVLANG_XVALUE_CHAR_ENCODING_ASCII:

            return finish_text((const char *)body_data, body_size, output,
                               output_capacity, output_length, failure);
        case KVLANG_XVALUE_CHAR_ENCODING_UTF32: {
            if (body_size % 4U != 0) {
                return fail(failure, KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT,
                            output_length);
            }
            if (output == NULL) {
                if (output_capacity != 0) {
                    return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL,
                                output_length);
                }

                *output_length = body_size;
                return true;
            }
            size_t encoded = 0;

            if (!kvlangXvalueCharUtf32ToUtf8(body_data, body_size, output,
                                             output_capacity, &encoded)) {
                return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL,
                            output_length);
            }
            if (encoded >= output_capacity) {
                return fail(failure, KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL,
                            output_length);
            }
            output[encoded] = '\0';
            *output_length = encoded;
            if (failure) {
                *failure = KVLANG_XVALUE_TEXT_OK;
            }
            return true;
        }
        case KVLANG_XVALUE_CHAR_ENCODING_NONE:
            break;
        }
    }

    return fail(failure, KVLANG_XVALUE_TEXT_UNSUPPORTED_TYPE, output_length);
}

const char *kvlangXvalueTextFailureText(kvlangXvalueTextFailure failure) {
    switch (failure) {
    case KVLANG_XVALUE_TEXT_OK:
        return "ok";
    case KVLANG_XVALUE_TEXT_BAD_LANGTYPE:
        return "langtype 语法畸形";
    case KVLANG_XVALUE_TEXT_UNSUPPORTED_TYPE:
        return "该类型不支持文本解释";
    case KVLANG_XVALUE_TEXT_BAD_ESCAPE_FORMAT:
        return "文本不是该类型的合法表示";
    case KVLANG_XVALUE_TEXT_OUT_OF_RANGE:
        return "数值超出该类型的表示范围";
    case KVLANG_XVALUE_TEXT_BUFFER_TOO_SMALL:
        return "输出缓冲不足（不截断）";
    }
    return "未知失败";
}

bool kvlangXvalueTextToBytes(const char *langtype, size_t langtype_length,
                             const char *value_text, size_t value_text_length,
                             uint8_t *output, size_t output_capacity,
                             size_t *output_length,
                             kvlangXvalueTextFailure *failure) {
    return text_to_bytes_impl(
        kvlangXvalueStringViewMake(langtype, langtype_length),
        kvlangXvalueStringViewMake(value_text, value_text_length), output,
        output_capacity, output_length, failure);
}

bool kvlangXvalueTextFromBytes(const char *langtype, size_t langtype_length,
                               const uint8_t *body, size_t body_length,
                               char *output, size_t output_capacity,
                               size_t *output_length,
                               kvlangXvalueTextFailure *failure) {
    return text_from_bytes_impl(
        kvlangXvalueStringViewMake(langtype, langtype_length),
        body, body_length, output, output_capacity,
        output_length, failure);
}

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KVLANG_XVALUE_FLOAT_FORMAT_MAX_FRACTION_DIGITS 17

#define KVLANG_XVALUE_FLOAT_FORMAT_FIXED_EXPONENT_MIN (-4)
#define KVLANG_XVALUE_FLOAT_FORMAT_FIXED_EXPONENT_MAX 16

static bool kvlangxvalue_float_format_non_finite(double value, char *output,
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

static bool kvlangXvalueFloatShortest_digits(
    double value, kvlangXvalueFloatDomain domain, char *digits,
    size_t digits_capacity, int *exponent, bool *negative) {
    int fraction;
    bool found = false;

    for (fraction = 0; fraction <= KVLANG_XVALUE_FLOAT_FORMAT_MAX_FRACTION_DIGITS;
         ++fraction) {
        char candidate[64];
        const int written = snprintf(candidate, sizeof(candidate), "%.*e",
                                     fraction, value);

        if (written <= 0 || (size_t)written >= sizeof(candidate)) {
            return false;
        }
        if (domain == KVLANG_XVALUE_FLOAT_DOMAIN_FLOAT32) {
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

static void kvlangxvalue_float_format_trim_digits(char *digits) {
    size_t length = strlen(digits);

    while (length > 1U && digits[length - 1U] == '0') {
        digits[length - 1U] = '\0';
        --length;
    }
}

static bool kvlangxvalue_float_format_use_fixed(int exponent) {
    return exponent >= KVLANG_XVALUE_FLOAT_FORMAT_FIXED_EXPONENT_MIN &&
           exponent < KVLANG_XVALUE_FLOAT_FORMAT_FIXED_EXPONENT_MAX;
}

static bool kvlangxvalue_float_format_write_exponential(const char *digits,
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

static bool kvlangxvalue_float_format_write_fixed(const char *digits, int exponent,
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

static bool float_shortest(double value, kvlangXvalueFloatDomain domain,
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

    if (kvlangxvalue_float_format_non_finite(value, output, capacity,
                                         output_length)) {
        return true;
    }

    if (!kvlangXvalueFloatShortest_digits(value, domain, digits,
                                               sizeof(digits), &exponent,
                                               &negative)) {
        return false;
    }
    kvlangxvalue_float_format_trim_digits(digits);

    if (kvlangxvalue_float_format_use_fixed(exponent)) {

        return kvlangxvalue_float_format_write_fixed(digits, exponent, negative,
                                                  output, capacity,
                                                  output_length);
    }
    return kvlangxvalue_float_format_write_exponential(digits, exponent, negative,
                                                    output, capacity,
                                                    output_length);
}


#include <kvspace/kvspace.h>

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint64_t langtype_payload_len(const char *langtype, char prefix) {
    const uint64_t text = langtype ? (uint64_t)strlen(langtype) : 0;
    return text + (prefix ? 1u : 0u);
}

static uint64_t langtype_attr_len(const char *langtype, char prefix) {
    const uint64_t payload = langtype_payload_len(langtype, prefix);
    return payload ? 3 + payload : 0;
}

uint8_t *kvlangxvalueNew(void *handle, const char *key, uint64_t body_len,
                         uint16_t attr_len) {
    if (!handle || !key)
        return NULL;
    uint64_t offset = 0;
    const int status = kvspaceRealloc(handle, key, body_len, attr_len, &offset);
    if (status != KVSPACE_OK)
        return NULL;
    uint8_t *base = kvspaceXvalueInProcessAddress(handle);
    if (!base)
        return NULL;
    uint8_t *wire = base + offset;
    kvspaceXvalueHead head = {.padding = {0u, 0u, 0u, 0u, 0u, 0u}};
    kvspaceXvalueHeadSetLen(&head, body_len);
    kvspaceXvalueHeadSetAttrsLen(&head, attr_len);
    memcpy(wire, &head, sizeof head);
    return wire;
}

int kvlangxvalueWriteBody(uint8_t *wire, const uint8_t *body,
                          uint64_t body_len) {
    if (!wire)
        return -1;
    const kvspaceXvalueHead *head = (const kvspaceXvalueHead *)(const void *)wire;
    if (kvspaceXvalueHeadLen(head) != body_len)
        return -1;
    if (body && body_len)
        memcpy(wire + KVSPACE_XVALUE_HEAD_LEN, body, (size_t)body_len);
    return 0;
}

int kvlangxvalueWriteLangtype(uint8_t *wire, uint64_t body_len, char prefix,
                              const char *langtype) {
    if (!wire)
        return -1;
    const kvspaceXvalueHead *head = (const kvspaceXvalueHead *)(const void *)wire;
    if (kvspaceXvalueHeadLen(head) != body_len)
        return -1;
    const uint64_t payload = langtype_payload_len(langtype, prefix);
    const uint64_t attr_len = payload ? 3 + payload : 0;
    if (kvspaceXvalueHeadAttrsLen(head) != attr_len)
        return -1;
    if (attr_len == 0)
        return 0;
    uint8_t *record = wire + KVSPACE_XVALUE_HEAD_LEN + body_len;
    record[0] = KVLANG_XVALUE_ATTR_TAG_LANGTYPE;
    record[1] = (uint8_t)payload;
    record[2] = (uint8_t)(payload >> 8);
    uint64_t at = 3;
    if (prefix)
        record[at++] = (uint8_t)prefix;
    const uint64_t text = langtype ? (uint64_t)strlen(langtype) : 0;
    if (text)
        memcpy(record + at, langtype, (size_t)text);
    return 0;
}

uint8_t *kvlangXvalueNewT(void *handle, const char *key,
                          const uint8_t *raw, uint32_t raw_len,
                          const char *langtype) {
    const uint64_t attr_len = langtype_attr_len(langtype, 0);
    if (attr_len > UINT16_MAX)
        return NULL;
    uint8_t *wire = kvlangxvalueNew(handle, key, raw_len, (uint16_t)attr_len);
    if (!wire)
        return NULL;
    if (kvlangxvalueWriteBody(wire, raw, raw_len) != 0 ||
        kvlangxvalueWriteLangtype(wire, raw_len, 0, langtype) != 0)
        return NULL;
    return wire;
}

static int is_base_langtype(const char *langtype) {
    static const char *const base[] = {
        "bool",   "int8",      "int16",     "int32",     "int64",
        "uint8",  "uint16",    "uint32",    "uint64",    "float32",
        "float64", "char/utf8", "char/utf32", "char/ascii",
    };
    if (!langtype)
        return 0;
    for (size_t i = 0; i < sizeof base / sizeof base[0]; i++)
        if (strcmp(langtype, base[i]) == 0)
            return 1;
    return 0;
}

static int is_buffer_array_langtype(const char *langtype) {
    if (!langtype || langtype[0] != '[')
        return 0;
    const char *p = langtype + 1;
    int digits = 0;
    for (; *p && *p != ']'; p++) {
        if (*p >= '0' && *p <= '9') {
            digits = 1;
        } else if (*p == ',') {
            if (!digits) return 0;
            digits = 0;
        } else if (*p != '-' && *p != '.' && *p != '?' && *p != '*' &&
                   *p != '+') {

            return 0;
        }
    }
    if (*p != ']')
        return 0;
    return is_base_langtype(p + 1);
}

static int is_compact_array_element_langtype(const char *langtype) {
    return is_base_langtype(langtype) && strcmp(langtype, "char/utf8") != 0;
}

static int is_compact_array_langtype(const char *langtype) {
    if (!langtype || langtype[0] != '[')
        return 0;
    const char *p = langtype + 1;
    int dimension_count = 0;
    int digits = 0;
    for (; *p && *p != ']'; p++) {
        if (*p >= '0' && *p <= '9') {
            digits = 1;
        } else if (*p == ',' && digits) {
            dimension_count++;
            digits = 0;
        } else {
            return 0;
        }
    }
    if (*p != ']' || !digits)
        return 0;
    dimension_count++;
    if (dimension_count > 64)
        return 0;
    return is_compact_array_element_langtype(p + 1);
}

uint8_t *kvlangXvalueNewBufferArray(void *handle, const char *key,
                                    const uint8_t *data, uint64_t data_len,
                                    const char *langtype) {
    if (!is_buffer_array_langtype(langtype))
        return NULL;
    if (data_len > UINT32_MAX || (data_len && !data))
        return NULL;
    return kvlangXvalueNewT(handle, key, data, (uint32_t)data_len, langtype);
}

uint8_t *kvlangXvalueNewCompactArray(void *handle, const char *key,
                                     const uint64_t *dims, uint32_t ndim,
                                     const uint8_t *raw, uint64_t raw_len,
                                     const char *langtype) {

    if (!dims || ndim == 0 || ndim > 64)
        return NULL;
    if (!is_compact_array_langtype(langtype))
        return NULL;
    if (raw_len > UINT32_MAX || (raw_len && !raw))
        return NULL;
    return kvlangXvalueNewT(handle, key, raw, (uint32_t)raw_len, langtype);
}

uint8_t *kvlangXvalueNewPtr(void *handle, const char *key,
                            const uint8_t *path, uint32_t path_len,
                            const char *langtype) {
    if (path_len && !path)
        return NULL;
    if (langtype && (langtype[0] == '*' || langtype[0] == '@'))
        return NULL;
    const size_t text = langtype ? strlen(langtype) : 0;
    char *want = (char *)malloc(text + 2);
    if (!want)
        return NULL;
    want[0] = '*';
    if (text)
        memcpy(want + 1, langtype, text);
    want[text + 1] = '\0';
    uint8_t *wire = kvlangXvalueNewT(handle, key, path, path_len, want);
    free(want);
    return wire;
}

uint8_t *kvlangXvalueNewCode(void *handle, const char *key,
                             const uint8_t *body, uint64_t body_len,
                             const char *langtype) {
    if (body_len > UINT32_MAX || (body_len && !body))
        return NULL;
    return kvlangXvalueNewT(handle, key, body, (uint32_t)body_len, langtype);
}

uint8_t *kvlangXvalueHeadReserve(void *handle, const char *key,
                                 uint64_t body_len, const char *langtype) {

    if (body_len > UINT32_MAX)
        return NULL;
    return kvlangXvalueNewT(handle, key, NULL, (uint32_t)body_len, langtype);
}

uint8_t *kvlangXvalueNewPtrFromKindexpr(void *handle, const char *key,
                                        const char *target_kindexpr,
                                        const char *target) {
    if (!target_kindexpr || !target)
        return NULL;

    if (strcmp(target_kindexpr, "any") == 0)
        return NULL;

    if (*target == '\0')
        return NULL;

    return kvlangXvalueNewPtr(handle, key, (const uint8_t *)target,
                              (uint32_t)strlen(target), target_kindexpr);
}

uint8_t *kvlangXvalueNewChar(void *handle, const char *key,
                             const uint8_t *bytes, uint32_t len,
                             const char *langtype) {
    if (len && !bytes)
        return NULL;

    return kvlangXvalueNewT(handle, key, bytes, len, langtype);
}

uint8_t *kvlangXvalueNewBool(void *handle, const char *key, uint8_t v,
                             const char *langtype) {
    uint8_t b = v ? 1 : 0;
    return kvlangXvalueNewT(handle, key, &b, 1, langtype);
}

uint8_t *kvlangXvalueNewInt64(void *handle, const char *key, int64_t v,
                              const char *langtype) {
    uint8_t raw[8];
    for (int i = 0; i < 8; i++)
        raw[i] = (uint8_t)((uint64_t)v >> (8 * i));
    return kvlangXvalueNewT(handle, key, raw, 8, langtype);
}

uint8_t *kvlangXvalueNewFloat64(void *handle, const char *key, double v,
                                const char *langtype) {
    uint64_t bits;
    memcpy(&bits, &v, sizeof bits);
    uint8_t raw[8];
    for (int i = 0; i < 8; i++)
        raw[i] = (uint8_t)(bits >> (8 * i));
    return kvlangXvalueNewT(handle, key, raw, 8, langtype);
}

const char *kvlangConstLanguage(const char *name) {
    static const struct { const char *key; const char *val; } table[] = {
#define KVLANG_CONST_ENTRY(name) { #name, name },
        KVLANG_KV(KVLANG_CONST_ENTRY)
#undef KVLANG_CONST_ENTRY
    };
    if (!name)
        return NULL;
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++)
        if (strcmp(table[i].key, name) == 0)
            return table[i].val;
    return NULL;
}
#endif

#ifdef __cplusplus
}
#endif

#endif
