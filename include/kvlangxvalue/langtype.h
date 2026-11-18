#ifndef KVLANG_XVALUE_LANGTYPE_H
#define KVLANG_XVALUE_LANGTYPE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct kvlangXvalueStringView {
    const char *data;
    size_t size;
} kvlangXvalueStringView;

static inline kvlangXvalueStringView kvlangXvalueStringViewMake(const char *data,
                                                               size_t size) {
    kvlangXvalueStringView view;
    view.data = data;
    view.size = size;
    return view;
}

#ifdef __cplusplus
extern "C" {
#endif

typedef enum kvlangXvalueLangtypeForm {
    KVLANG_XVALUE_FORM_SCALAR = 0,
    KVLANG_XVALUE_FORM_CHARACTER,
    KVLANG_XVALUE_FORM_STRING,
    KVLANG_XVALUE_FORM_ARRAY,
    KVLANG_XVALUE_FORM_TUPLE,

    KVLANG_XVALUE_FORM_MAPPING,
    KVLANG_XVALUE_FORM_UNRECOGNIZED
} kvlangXvalueLangtypeForm;

typedef enum kvlangXvalueLangtypeScalar {
    KVLANG_XVALUE_SCALAR_NONE = 0,
    KVLANG_XVALUE_SCALAR_SIGNED_INTEGER,
    KVLANG_XVALUE_SCALAR_UNSIGNED_INTEGER,
    KVLANG_XVALUE_SCALAR_FLOAT,
    KVLANG_XVALUE_SCALAR_BOOLEAN
} kvlangXvalueLangtypeScalar;

typedef enum kvlangXvalueCharEncoding {
    KVLANG_XVALUE_CHAR_ENCODING_NONE = 0,
    KVLANG_XVALUE_CHAR_ENCODING_UTF8,
    KVLANG_XVALUE_CHAR_ENCODING_UTF32,
    KVLANG_XVALUE_CHAR_ENCODING_ASCII
} kvlangXvalueCharEncoding;

typedef struct kvlangXvalueLangtype {

    kvlangXvalueStringView base;
    kvlangXvalueLangtypeForm form;
    kvlangXvalueLangtypeScalar scalar;
    uint32_t scalar_width;

    kvlangXvalueCharEncoding char_encoding;

    int32_t dimension_count;
} kvlangXvalueLangtype;

void kvlangXvalueLangtypeParse(kvlangXvalueStringView text,
                             kvlangXvalueLangtype *output);

#ifdef __cplusplus
}
#endif

#ifdef KVLANG_XVALUE_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>


#include <string.h>

typedef struct kvlangxvalue_langtype_scalar_entry {
    const char *text;
    kvlangXvalueLangtypeScalar scalar;
    uint32_t width;
} kvlangxvalue_langtype_scalar_entry_t;

static const kvlangxvalue_langtype_scalar_entry_t kvlangXvalueLangtypeScalarable[] = {
    {"bool", KVLANG_XVALUE_SCALAR_BOOLEAN, 1U},
    {"int8", KVLANG_XVALUE_SCALAR_SIGNED_INTEGER, 1U},
    {"int16", KVLANG_XVALUE_SCALAR_SIGNED_INTEGER, 2U},
    {"int32", KVLANG_XVALUE_SCALAR_SIGNED_INTEGER, 4U},
    {"int64", KVLANG_XVALUE_SCALAR_SIGNED_INTEGER, 8U},
    {"uint8", KVLANG_XVALUE_SCALAR_UNSIGNED_INTEGER, 1U},
    {"uint16", KVLANG_XVALUE_SCALAR_UNSIGNED_INTEGER, 2U},
    {"uint32", KVLANG_XVALUE_SCALAR_UNSIGNED_INTEGER, 4U},
    {"uint64", KVLANG_XVALUE_SCALAR_UNSIGNED_INTEGER, 8U},
    {"float32", KVLANG_XVALUE_SCALAR_FLOAT, 4U},
    {"float64", KVLANG_XVALUE_SCALAR_FLOAT, 8U},
};

static bool kvlangXvalueLangtypeext_is(kvlangXvalueStringView text,
                                      const char *literal) {
    const size_t size = strlen(literal);

    return text.data != NULL && text.size == size &&
           memcmp(text.data, literal, size) == 0;
}

static bool kvlangxvalue_langtype_scalar_lookup(kvlangXvalueStringView base,
                                            kvlangXvalueLangtype *output) {
    const size_t count = sizeof(kvlangXvalueLangtypeScalarable) /
                         sizeof(kvlangXvalueLangtypeScalarable[0]);

    for (size_t ordinal = 0U; ordinal < count; ++ordinal) {
        if (kvlangXvalueLangtypeext_is(base,
                                      kvlangXvalueLangtypeScalarable[ordinal].text)) {
            output->scalar = kvlangXvalueLangtypeScalarable[ordinal].scalar;
            output->scalar_width =
                kvlangXvalueLangtypeScalarable[ordinal].width;
            return true;
        }
    }
    return false;
}

static bool kvlangxvalue_langtype_char_lookup(kvlangXvalueStringView base,
                                         kvlangXvalueLangtype *output) {
    if (kvlangXvalueLangtypeext_is(base, "char/utf8")) {
        output->char_encoding = KVLANG_XVALUE_CHAR_ENCODING_UTF8;
        return true;
    }
    if (kvlangXvalueLangtypeext_is(base, "char/utf32")) {
        output->char_encoding = KVLANG_XVALUE_CHAR_ENCODING_UTF32;
        return true;
    }
    if (kvlangXvalueLangtypeext_is(base, "char/ascii")) {
        output->char_encoding = KVLANG_XVALUE_CHAR_ENCODING_ASCII;
        return true;
    }
    return false;
}

static bool kvlangxvalue_langtype_dimension_is_variable(
    kvlangXvalueStringView dimension) {
    if (dimension.size == 0U) {
        return true;
    }
    if (dimension.size != 1U) {
        return false;
    }
    return dimension.data[0] == '.' || dimension.data[0] == '?' ||
           dimension.data[0] == '*' || dimension.data[0] == '+';
}

static bool kvlangXvalueLangtypeuple_matches(kvlangXvalueStringView dimension) {
    size_t start = 0U;

    for (size_t cursor = 0U; cursor <= dimension.size; ++cursor) {
        kvlangXvalueLangtype element;

        if (cursor != dimension.size && dimension.data[cursor] != ',') {
            continue;
        }
        if (cursor == start) {
            return false;
        }
        memset(&element, 0, sizeof(element));
        if (!kvlangxvalue_langtype_scalar_lookup(
                kvlangXvalueStringViewMake(dimension.data + start,
                                          cursor - start),
                &element)) {
            return false;
        }
        start = cursor + 1U;
    }
    return true;
}

void kvlangXvalueLangtypeParse(kvlangXvalueStringView text,
                             kvlangXvalueLangtype *output) {
    size_t offset = 0U;
    int32_t dimension_count = 0;
    kvlangXvalueStringView first_dimension = kvlangXvalueStringViewMake(NULL, 0U);
    bool malformed = false;

    memset(output, 0, sizeof(*output));
    if (text.size == 0U || text.data == NULL) {
        abort();
    }

    output->form = KVLANG_XVALUE_FORM_UNRECOGNIZED;

    if (text.data[0] == '\'') {

        const char *closing =
            memchr(text.data + 1U, '\'', text.size - 1U);

        if (closing == NULL) {
            abort();
        }
        {
            const size_t value_offset =
                (size_t)(closing - text.data) + 1U;

            if (value_offset >= text.size) {
                abort();
            }
            if (memchr(text.data + value_offset, '\'',
                       text.size - value_offset) != NULL) {
                abort();
            }
            output->form = KVLANG_XVALUE_FORM_MAPPING;
            output->dimension_count = 0;
            output->base = kvlangXvalueStringViewMake(
                text.data + value_offset, text.size - value_offset);
        }
        return;
    }
    if (text.data[0] == '*' || text.data[0] == '@' || text.data[0] == '/') {
        return;
    }

    {
        size_t scan = 0U;
        bool has_member_separator = false;

        while (scan + 1U < text.size) {

            if ((uint8_t)text.data[scan] == 0xC2U &&
                (uint8_t)text.data[scan + 1U] == 0xB7U) {
                has_member_separator = true;
                break;
            }
            ++scan;
        }
        if (has_member_separator) {
            abort();
        }
    }
    while (offset < text.size && text.data[offset] == '[') {
        const char *closing =
            memchr(text.data + offset, ']', text.size - offset);

        if (closing == NULL) {
            malformed = true;
            break;
        }
        if (dimension_count == 0) {
            first_dimension =
                kvlangXvalueStringViewMake(text.data + offset + 1U,
                                          (size_t)(closing - (text.data + offset)) - 1U);
        }
        offset = (size_t)(closing - text.data) + 1U;
        ++dimension_count;
    }
    if (malformed) {
        abort();
    }
    output->dimension_count = dimension_count;
    output->base = kvlangXvalueStringViewMake(text.data + offset,
                                             text.size - offset);
    if (output->base.size == 0U) {

        if (dimension_count == 1 && memchr(first_dimension.data, ',',
                                           first_dimension.size) != NULL &&
            kvlangXvalueLangtypeuple_matches(first_dimension)) {
            output->form = KVLANG_XVALUE_FORM_TUPLE;
            return;
        }
        abort();
    }
    if (dimension_count == 0) {
        if (memchr(output->base.data, '[', output->base.size) != NULL ||
            memchr(output->base.data, ']', output->base.size) != NULL) {
            abort();
        }
        if (kvlangxvalue_langtype_scalar_lookup(output->base, output)) {
            output->form = KVLANG_XVALUE_FORM_SCALAR;
            return;
        }
        if (kvlangxvalue_langtype_char_lookup(output->base, output)) {
            output->form = KVLANG_XVALUE_FORM_CHARACTER;
            return;
        }
        return;
    }

    if (dimension_count == 1 &&
        kvlangxvalue_langtype_dimension_is_variable(first_dimension) &&
        kvlangxvalue_langtype_char_lookup(output->base, output)) {
        output->form = KVLANG_XVALUE_FORM_STRING;
        return;
    }
    output->form = KVLANG_XVALUE_FORM_ARRAY;
    return;
}
#endif

#endif
