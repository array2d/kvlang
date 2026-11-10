#include "contract/key_order.h"

#include <stdint.h>

/* 坐标段结构判定：`[` … `]`，内部不含方括号，长度 >= 3（至少 `[x]`）。 */
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

/* 整数坐标段：`[d(,d)*]`，每段非空且全为十进制数字。成功时把各段数值依次写入
 * `output`（最多 `capacity` 段），并返回段数；不可整数解析返回 0（合法空集）。 */
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

/* 坐标段最大维数：`[s0,s1,…]` 的段数上界。超过即按「非整数坐标」处理，
 * 因为整数坐标的维度在 kvlang 里是有限的（数组维数）。 */
#define RUNTIMEC_KEY_COORD_COMPONENT_LIMIT 8U

int runtimec_key_compare_coord(const char *left, size_t left_size,
                               const char *right, size_t right_size) {
    const int left_is_coord = runtimec_key_is_coord(left, left_size);
    const int right_is_coord = runtimec_key_is_coord(right, right_size);

    if (left_is_coord != right_is_coord) {
        /* 坐标段恒排在非坐标段之前。 */
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

    /* **不是「整数优先」**：只有两边都能整数解析才比数值；一边可解析一边不可
     * （或都不可）一律字典序。这与后端 `kvspaceCoordCmp` 的 `na < 0 || nb < 0`
     * 分支同语义——判据是「比较能不能做成数值比较」，不是「谁是整数」。 */
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
        /* row-major：前缀相同时维数少者在前。 */
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
    /* 坐标判等（`[01]` 与 `[1]`、`[1]` 与 `[1]/`）：退回原始名字的字节序。
     * 后端 `child_cmp` 的 `c ? c : strcmp(x, y)` 就是这一句——少了它，目录项与
     * 同名的值会彼此相等，排序不稳定，两次列举可能给出不同的第 i 项。 */
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
