/*
 * 组件：vmkv/parameter_type 的实现
 * 作用：langtype 文本 → 单值 TypeSet；并集、空、未知种类分别返回明确结果。
 * 约束：不做名称归一化；`known_kind` 之外的基种类一律 UNMAPPED。
 */

#include "vmkv/parameter_type.h"

#include <string.h>

#include "vmkv/rwir_manifest.h"

#define VMKV_PARAMETER_TYPE_UNION_SEPARATOR '|'
#define VMKV_PARAMETER_TYPE_MAP_SEPARATOR "·"

typedef struct vmkv_parameter_kind_name {
    const char *text;
    uint32_t type_set;
} vmkv_parameter_kind_name_t;

/* 19 位类别表里的基种类；顺序与位号无关，查找不看顺序。 */
static const vmkv_parameter_kind_name_t vmkv_parameter_kind_names[] = {
    {"int8", VMKV_RWIR_TYPE_SET_INT8},
    {"int16", VMKV_RWIR_TYPE_SET_INT16},
    {"int32", VMKV_RWIR_TYPE_SET_INT32},
    {"int64", VMKV_RWIR_TYPE_SET_INT64},
    {"uint8", VMKV_RWIR_TYPE_SET_UINT8},
    {"uint16", VMKV_RWIR_TYPE_SET_UINT16},
    {"uint32", VMKV_RWIR_TYPE_SET_UINT32},
    {"uint64", VMKV_RWIR_TYPE_SET_UINT64},
    {"float32", VMKV_RWIR_TYPE_SET_FLOAT32},
    {"float64", VMKV_RWIR_TYPE_SET_FLOAT64},
    {"bool", VMKV_RWIR_TYPE_SET_BOOLEAN},
    {"char/utf8", VMKV_RWIR_TYPE_SET_CHARACTER_UTF8},
    {"char/utf32", VMKV_RWIR_TYPE_SET_CHARACTER_UTF32},
    {"char/ascii", VMKV_RWIR_TYPE_SET_CHARACTER_ASCII},
    {"stringkeymap", VMKV_RWIR_TYPE_SET_STRING_KEY_MAP},
};

static bool vmkv_parameter_text_equals(runtimec_string_view_t text,
                                       const char *value) {
    const size_t size = strlen(value);
    return text.size == size &&
           (size == 0U || memcmp(text.data, value, size) == 0);
}

/* 子串查找：只依赖 C11，不引入 memmem。 */
static bool vmkv_parameter_contains(runtimec_string_view_t text,
                                    const char *needle) {
    const size_t needle_size = strlen(needle);
    if (needle_size == 0U || needle_size > text.size) {
        return false;
    }
    for (size_t offset = 0U; offset + needle_size <= text.size; ++offset) {
        if (memcmp(text.data + offset, needle, needle_size) == 0) {
            return true;
        }
    }
    return false;
}

/* 基种类 → 标量类别的位；未知返回 0。 */
static uint32_t vmkv_parameter_scalar_type_set(runtimec_string_view_t base) {
    const size_t count = sizeof(vmkv_parameter_kind_names) /
                         sizeof(vmkv_parameter_kind_names[0]);
    for (size_t ordinal = 0U; ordinal < count; ++ordinal) {
        if (vmkv_parameter_text_equals(base,
                                       vmkv_parameter_kind_names[ordinal].text)) {
            return vmkv_parameter_kind_names[ordinal].type_set;
        }
    }
    return VMKV_RWIR_TYPE_SET_NONE;
}

static bool vmkv_parameter_type_set_is_character(uint32_t type_set) {
    return (type_set & (VMKV_RWIR_TYPE_SET_CHARACTER_UTF8 |
                        VMKV_RWIR_TYPE_SET_CHARACTER_UTF32 |
                        VMKV_RWIR_TYPE_SET_CHARACTER_ASCII)) != 0U;
}

/* 形状 `[dims]kind`：按基种类归类，字符基优先于数组位。 */
static vmkv_parameter_type_result_t vmkv_parameter_classify_shape(
    runtimec_string_view_t langtype, uint32_t *output_type_set) {
    /* 连续形状段 `[a][b]…kind`：每一段都必须闭合，基种类在最后一段之后。 */
    size_t base_offset = 0U;
    while (base_offset < langtype.size && langtype.data[base_offset] == '[') {
        const char *closing = memchr(langtype.data + base_offset, ']',
                                     langtype.size - base_offset);
        if (closing == NULL || closing == langtype.data + base_offset) {
            return VMKV_PARAMETER_TYPE_RESULT_INVALID;
        }
        base_offset = (size_t)(closing - langtype.data) + 1U;
    }
    if (base_offset == 0U || base_offset >= langtype.size) {
        return VMKV_PARAMETER_TYPE_RESULT_INVALID;
    }
    const runtimec_string_view_t base = runtimec_string_view_make(
        langtype.data + base_offset, langtype.size - base_offset);
    if (base.data[0] == '[' || base.data[0] == '/' ||
        memchr(base.data, VMKV_PARAMETER_TYPE_UNION_SEPARATOR, base.size) !=
            NULL ||
        vmkv_parameter_contains(base, VMKV_PARAMETER_TYPE_MAP_SEPARATOR)) {
        return VMKV_PARAMETER_TYPE_RESULT_INVALID;
    }
    const uint32_t base_type_set = vmkv_parameter_scalar_type_set(base);
    if (base_type_set == VMKV_RWIR_TYPE_SET_NONE) {
        return VMKV_PARAMETER_TYPE_RESULT_UNMAPPED;
    }
    if (vmkv_parameter_type_set_is_character(base_type_set)) {
        *output_type_set = base_type_set;
        return VMKV_PARAMETER_TYPE_RESULT_SINGLE;
    }
    if ((base_type_set & VMKV_RWIR_TYPE_SET_STRING_KEY_MAP) != 0U) {
        return VMKV_PARAMETER_TYPE_RESULT_UNMAPPED;
    }
    *output_type_set = VMKV_RWIR_TYPE_SET_ARRAY;
    return VMKV_PARAMETER_TYPE_RESULT_SINGLE;
}

vmkv_parameter_type_result_t vmkv_parameter_type_classify(
    runtimec_string_view_t langtype, uint32_t *output_type_set) {
    if (output_type_set != NULL) {
        *output_type_set = VMKV_RWIR_TYPE_SET_NONE;
    }
    if (!runtimec_string_view_is_valid(langtype) || output_type_set == NULL) {
        return VMKV_PARAMETER_TYPE_RESULT_INVALID;
    }
    if (langtype.size == 0U || vmkv_parameter_text_equals(langtype, "None")) {
        return VMKV_PARAMETER_TYPE_RESULT_NONE;
    }
    if (memchr(langtype.data, VMKV_PARAMETER_TYPE_UNION_SEPARATOR,
               langtype.size) != NULL) {
        return VMKV_PARAMETER_TYPE_RESULT_UNION;
    }
    if (vmkv_parameter_contains(langtype, VMKV_PARAMETER_TYPE_MAP_SEPARATOR)) {
        *output_type_set = VMKV_RWIR_TYPE_SET_STRING_KEY_MAP;
        return VMKV_PARAMETER_TYPE_RESULT_SINGLE;
    }
    if (langtype.data[0] == '/') {
        *output_type_set = VMKV_RWIR_TYPE_SET_STRUCT_REFERENCE;
        return VMKV_PARAMETER_TYPE_RESULT_SINGLE;
    }
    if (langtype.data[0] == '[') {
        return vmkv_parameter_classify_shape(langtype, output_type_set);
    }
    const uint32_t type_set = vmkv_parameter_scalar_type_set(langtype);
    if (type_set == VMKV_RWIR_TYPE_SET_NONE) {
        return VMKV_PARAMETER_TYPE_RESULT_UNMAPPED;
    }
    *output_type_set = type_set;
    return VMKV_PARAMETER_TYPE_RESULT_SINGLE;
}

runtimec_string_view_t vmkv_parameter_type_name(uint32_t type_set) {
    const size_t count = sizeof(vmkv_parameter_kind_names) /
                         sizeof(vmkv_parameter_kind_names[0]);
    if (!vmkv_rwir_type_set_is_single(type_set)) {
        return runtimec_string_view_make(NULL, 0U);
    }
    for (size_t ordinal = 0U; ordinal < count; ++ordinal) {
        if (vmkv_parameter_kind_names[ordinal].type_set == type_set) {
            return runtimec_string_view_from_c_string(
                vmkv_parameter_kind_names[ordinal].text);
        }
    }
    /* array / structref / pointer 没有单段规范名，由调用方持有原文本。 */
    return runtimec_string_view_make(NULL, 0U);
}
