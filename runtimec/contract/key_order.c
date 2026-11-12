#include "contract/key_order.h"

#include <stdint.h>

int runtimec_key_is_coord(const char *name, size_t name_size) {
    if (name == NULL || name_size < 3U) {
        return 0;
    }
    if (name[0] != '[' || name[name_size - 1U] != ']') {
        return 0;
    }
    for (size_t index = 1U; index + 1U < name_size; ++index) {
        if (name[index] == '[' || name[index] == ']') {
            return 0;
        }
    }
    return 1;
}

static size_t parse_integer_coord(const char *name, size_t name_size,
                                  int64_t *output, size_t capacity) {
    size_t count = 0U;
    size_t index = 1U;
    const size_t end = name_size - 1U;

    if (end <= index) {
        return 0U;
    }
    while (index < end) {
        const size_t start = index;
        uint64_t value = 0U;
        int overflow = 0;

        while (index < end && name[index] != ',') {
            if (name[index] < '0' || name[index] > '9') {
                return 0U;
            }
            if (!overflow) {
                if (value > (UINT64_MAX - (uint64_t)(name[index] - '0')) / 10U) {
                    overflow = 1;
                } else {
                    value = value * 10U + (uint64_t)(name[index] - '0');
                }
            }
            ++index;
        }
        if (index == start) {
            return 0U;
        }
        if (count < capacity) {
            output[count] = overflow ? INT64_MAX : (int64_t)value;
        }
        ++count;
        if (index < end) {
            ++index;
            if (index == end) {
                return 0U;
            }
        }
    }
    return count;
}

#define RUNTIMEC_KEY_COORD_COMPONENT_LIMIT 8U

int runtimec_key_compare_coord(const char *left, size_t left_size,
                               const char *right, size_t right_size) {
    const int left_is_coord = runtimec_key_is_coord(left, left_size);
    const int right_is_coord = runtimec_key_is_coord(right, right_size);

    if (left_is_coord != right_is_coord) {
        
        return left_is_coord ? -1 : 1;
    }
    if (!left_is_coord) {
        const size_t common = left_size < right_size ? left_size : right_size;
        for (size_t index = 0U; index < common; ++index) {
            const unsigned char a = (unsigned char)left[index];
            const unsigned char b = (unsigned char)right[index];
            if (a != b) {
                return a < b ? -1 : 1;
            }
        }
        if (left_size == right_size) {
            return 0;
        }
        return left_size < right_size ? -1 : 1;
    }

    int64_t left_values[RUNTIMEC_KEY_COORD_COMPONENT_LIMIT];
    int64_t right_values[RUNTIMEC_KEY_COORD_COMPONENT_LIMIT];
    const size_t left_count = parse_integer_coord(left, left_size, left_values,
                                                 RUNTIMEC_KEY_COORD_COMPONENT_LIMIT);
    const size_t right_count = parse_integer_coord(
        right, right_size, right_values, RUNTIMEC_KEY_COORD_COMPONENT_LIMIT);

    if (left_count != 0U && right_count != 0U) {
        const size_t common = left_count < right_count ? left_count : right_count;
        for (size_t index = 0U; index < common; ++index) {
            if (left_values[index] != right_values[index]) {
                return left_values[index] < right_values[index] ? -1 : 1;
            }
        }
        if (left_count == right_count) {
            return 0;
        }
        
        return left_count < right_count ? -1 : 1;
    }

    {
        const size_t common = left_size < right_size ? left_size : right_size;
        for (size_t index = 0U; index < common; ++index) {
            const unsigned char a = (unsigned char)left[index];
            const unsigned char b = (unsigned char)right[index];
            if (a != b) {
                return a < b ? -1 : 1;
            }
        }
        if (left_size == right_size) {
            return 0;
        }
        return left_size < right_size ? -1 : 1;
    }
}

void runtimec_key_member_name(const char *name, size_t name_size,
                              const char **output_name, size_t *output_size) {
    size_t size = name_size;
    if (name != NULL && size != 0U && name[size - 1U] == '/') {
        --size;
    }
    if (output_name != NULL) {
        *output_name = name;
    }
    if (output_size != NULL) {
        *output_size = size;
    }
}

int runtimec_key_compare_directory_entry(const char *left, size_t left_size,
                                         const char *right, size_t right_size) {
    const char *left_member = NULL;
    const char *right_member = NULL;
    size_t left_member_size = 0U;
    size_t right_member_size = 0U;

    runtimec_key_member_name(left, left_size, &left_member, &left_member_size);
    runtimec_key_member_name(right, right_size, &right_member, &right_member_size);

    const int compared = runtimec_key_compare_coord(
        left_member, left_member_size, right_member, right_member_size);
    if (compared != 0) {
        return compared;
    }
    
    const size_t common = left_size < right_size ? left_size : right_size;
    for (size_t index = 0U; index < common; ++index) {
        const unsigned char a = (unsigned char)left[index];
        const unsigned char b = (unsigned char)right[index];
        if (a != b) {
            return a < b ? -1 : 1;
        }
    }
    if (left_size == right_size) {
        return 0;
    }
    return left_size < right_size ? -1 : 1;
}
