
#include <stdio.h>

#include "contract/langtype.h"

#include <string.h>

typedef struct runtimec_langtype_scalar_entry {
    const char *text;
    runtimec_langtype_scalar_t scalar;
    uint32_t width;
} runtimec_langtype_scalar_entry_t;

static const runtimec_langtype_scalar_entry_t runtimec_langtype_scalar_table[] = {
    {"bool", RUNTIMEC_LANGTYPE_SCALAR_BOOLEAN, 1U},
    {"int8", RUNTIMEC_LANGTYPE_SCALAR_SIGNED_INTEGER, 1U},
    {"int16", RUNTIMEC_LANGTYPE_SCALAR_SIGNED_INTEGER, 2U},
    {"int32", RUNTIMEC_LANGTYPE_SCALAR_SIGNED_INTEGER, 4U},
    {"int64", RUNTIMEC_LANGTYPE_SCALAR_SIGNED_INTEGER, 8U},
    {"uint8", RUNTIMEC_LANGTYPE_SCALAR_UNSIGNED_INTEGER, 1U},
    {"uint16", RUNTIMEC_LANGTYPE_SCALAR_UNSIGNED_INTEGER, 2U},
    {"uint32", RUNTIMEC_LANGTYPE_SCALAR_UNSIGNED_INTEGER, 4U},
    {"uint64", RUNTIMEC_LANGTYPE_SCALAR_UNSIGNED_INTEGER, 8U},
    {"float32", RUNTIMEC_LANGTYPE_SCALAR_FLOAT, 4U},
    {"float64", RUNTIMEC_LANGTYPE_SCALAR_FLOAT, 8U},
};

static bool runtimec_langtype_text_is(runtimec_string_view_t text,
                                      const char *literal) {
    const size_t size = strlen(literal);

    return text.data != NULL && text.size == size &&
           memcmp(text.data, literal, size) == 0;
}

static bool runtimec_langtype_scalar_lookup(runtimec_string_view_t base,
                                            runtimec_langtype_t *output) {
    const size_t count = sizeof(runtimec_langtype_scalar_table) /
                         sizeof(runtimec_langtype_scalar_table[0]);

    for (size_t ordinal = 0U; ordinal < count; ++ordinal) {
        if (runtimec_langtype_text_is(base,
                                      runtimec_langtype_scalar_table[ordinal].text)) {
            output->scalar = runtimec_langtype_scalar_table[ordinal].scalar;
            output->scalar_width =
                runtimec_langtype_scalar_table[ordinal].width;
            return true;
        }
    }
    return false;
}

static bool runtimec_langtype_char_lookup(runtimec_string_view_t base,
                                         runtimec_langtype_t *output) {
    if (runtimec_langtype_text_is(base, "char/utf8")) {
        output->char_encoding = RUNTIMEC_LANGTYPE_CHAR_ENCODING_UTF8;
        return true;
    }
    if (runtimec_langtype_text_is(base, "char/utf32")) {
        output->char_encoding = RUNTIMEC_LANGTYPE_CHAR_ENCODING_UTF32;
        return true;
    }
    if (runtimec_langtype_text_is(base, "char/ascii")) {
        output->char_encoding = RUNTIMEC_LANGTYPE_CHAR_ENCODING_ASCII;
        return true;
    }
    return false;
}

static bool runtimec_langtype_dimension_is_variable(
    runtimec_string_view_t dimension) {
    if (dimension.size == 0U) {
        return true;
    }
    if (dimension.size != 1U) {
        return false;
    }
    return dimension.data[0] == '.' || dimension.data[0] == '?' ||
           dimension.data[0] == '*' || dimension.data[0] == '+';
}

static bool runtimec_langtype_tuple_matches(runtimec_string_view_t dimension) {
    size_t start = 0U;

    for (size_t cursor = 0U; cursor <= dimension.size; ++cursor) {
        runtimec_langtype_t element;

        if (cursor != dimension.size && dimension.data[cursor] != ',') {
            continue;
        }
        if (cursor == start) {
            return false; 
        }
        memset(&element, 0, sizeof(element));
        if (!runtimec_langtype_scalar_lookup(
                runtimec_string_view_make(dimension.data + start,
                                          cursor - start),
                &element)) {
            return false;
        }
        start = cursor + 1U;
    }
    return true;
}

runtimec_status_t runtimec_langtype_parse(runtimec_string_view_t text,
                                          runtimec_langtype_t *output) {
    size_t offset = 0U;
    int32_t dimension_count = 0;
    runtimec_string_view_t first_dimension = runtimec_string_view_make(NULL, 0U);
    bool malformed = false;

    if (output == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    memset(output, 0, sizeof(*output));
    if (text.size == 0U || text.data == NULL) {
        return RUNTIMEC_STATUS_INVALID;
    }
    
    output->form = RUNTIMEC_LANGTYPE_FORM_UNRECOGNIZED;
    
    if (text.data[0] == '\'') {
        
        const char *closing =
            memchr(text.data + 1U, '\'', text.size - 1U);

        if (closing == NULL) {
            return RUNTIMEC_STATUS_INVALID; 
        }
        {
            const size_t value_offset =
                (size_t)(closing - text.data) + 1U;

            if (value_offset >= text.size) {
                return RUNTIMEC_STATUS_INVALID; 
            }
            if (memchr(text.data + value_offset, '\'',
                       text.size - value_offset) != NULL) {
                return RUNTIMEC_STATUS_INVALID; 
            }
            output->form = RUNTIMEC_LANGTYPE_FORM_MAPPING;
            output->dimension_count = 0;
            output->base = runtimec_string_view_make(
                text.data + value_offset, text.size - value_offset);
        }
        return RUNTIMEC_STATUS_SUCCESS;
    }
    if (text.data[0] == '*' || text.data[0] == '@' || text.data[0] == '/') {
        return RUNTIMEC_STATUS_SUCCESS; 
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
            return RUNTIMEC_STATUS_INVALID;
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
                runtimec_string_view_make(text.data + offset + 1U,
                                          (size_t)(closing - (text.data + offset)) - 1U);
        }
        offset = (size_t)(closing - text.data) + 1U;
        ++dimension_count;
    }
    if (malformed) {
        return RUNTIMEC_STATUS_INVALID;
    }
    output->dimension_count = dimension_count;
    output->base = runtimec_string_view_make(text.data + offset,
                                             text.size - offset);
    if (output->base.size == 0U) {
        
        if (dimension_count == 1 && memchr(first_dimension.data, ',',
                                           first_dimension.size) != NULL &&
            runtimec_langtype_tuple_matches(first_dimension)) {
            output->form = RUNTIMEC_LANGTYPE_FORM_TUPLE;
            return RUNTIMEC_STATUS_SUCCESS;
        }
        return RUNTIMEC_STATUS_INVALID;
    }
    if (dimension_count == 0) {
        if (memchr(output->base.data, '[', output->base.size) != NULL ||
            memchr(output->base.data, ']', output->base.size) != NULL) {
            return RUNTIMEC_STATUS_INVALID; 
        }
        if (runtimec_langtype_scalar_lookup(output->base, output)) {
            output->form = RUNTIMEC_LANGTYPE_FORM_SCALAR;
            return RUNTIMEC_STATUS_SUCCESS;
        }
        if (runtimec_langtype_char_lookup(output->base, output)) {
            output->form = RUNTIMEC_LANGTYPE_FORM_CHARACTER;
            return RUNTIMEC_STATUS_SUCCESS;
        }
        return RUNTIMEC_STATUS_SUCCESS; 
    }
    
    if (dimension_count == 1 &&
        runtimec_langtype_dimension_is_variable(first_dimension) &&
        runtimec_langtype_char_lookup(output->base, output)) {
        output->form = RUNTIMEC_LANGTYPE_FORM_STRING;
        return RUNTIMEC_STATUS_SUCCESS;
    }
    output->form = RUNTIMEC_LANGTYPE_FORM_ARRAY;
    return RUNTIMEC_STATUS_SUCCESS;
}
