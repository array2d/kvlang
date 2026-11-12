#ifndef RUNTIMEC_CONTRACT_KEY_ORDER_H
#define RUNTIMEC_CONTRACT_KEY_ORDER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int runtimec_key_is_coord(const char *name, size_t name_size);

int runtimec_key_compare_coord(const char *left, size_t left_size,
                               const char *right, size_t right_size);

int runtimec_key_compare_directory_entry(const char *left, size_t left_size,
                                         const char *right, size_t right_size);

void runtimec_key_member_name(const char *name, size_t name_size,
                              const char **output_name, size_t *output_size);

#ifdef __cplusplus
}
#endif

#endif
