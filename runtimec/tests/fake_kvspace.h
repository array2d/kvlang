#ifndef RUNTIMEC_TESTS_FAKE_KVSPACE_H
#define RUNTIMEC_TESTS_FAKE_KVSPACE_H

/*
 * 测试替身：按 kvspace C 二进制接口的语义提供一个内存后端，供 host 适配器
 * 的单元测试在没有 libkvspace 时使用。
 * 覆盖范围只到适配器实际用到的行为：整值读写、定位写、删除、按目录枚举
 * （目录条目以尾斜杠结尾）、watch（立即满足或超时）、无 langtype 的
 * slack 编码。地址是替身缓冲的真实地址，因此「基址稳定 / 被搬移」都能被
 * 真实观察到。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "host/kvspace_binary_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fake_kvspace_entry {
    char key[256];
    uint8_t *wire;
    uint32_t wire_size;
    uint32_t wire_capacity;
    bool directory;
} fake_kvspace_entry_t;

typedef struct fake_kvspace_store {
    fake_kvspace_entry_t entries[64];
    size_t count;
    size_t handle_count;
    bool fail_next_get;
    bool fail_writes;
    /* 每次 get 前把该 key 的 box 搬到新缓冲：模拟 shm 里的槽搬移。 */
    const char *relocate_key;
} fake_kvspace_store_t;

/* 置一条值（wire 由调用方给出，替身复制一份）。返回 0 表示成功。 */
int fake_kvspace_put(fake_kvspace_store_t *store, const char *key,
                     const uint8_t *wire, uint32_t wire_size, bool directory);
/* 把整张表接到 binary interface；全部字段非空。 */
void fake_kvspace_bind(fake_kvspace_store_t *store,
                       runtimec_kvspace_binary_interface_t *output);
/* 该 key 当前 wire 的进程内地址；不存在返回 NULL。 */
uint8_t *fake_kvspace_address(fake_kvspace_store_t *store, const char *key);
void fake_kvspace_release(fake_kvspace_store_t *store);

#ifdef __cplusplus
}
#endif

#endif
