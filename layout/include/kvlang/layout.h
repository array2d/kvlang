
#ifndef KVLANG_LAYOUT_H
#define KVLANG_LAYOUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int kvlangLayoutDefineRwir(const char *dsn, const char *opcode,
                           const char *const *read_langtypes, uint32_t read_count,
                           const char *const *write_langtypes, uint32_t write_count,
                           const uint8_t *write_inplace,
                           int variadic, char *err, uint32_t err_cap);

#ifdef __cplusplus
}
#endif

#endif
