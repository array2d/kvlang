#include <stdio.h>
#include <stdlib.h>
#include "blockmalloc/blockmalloc.h"

void test_alloc(blocks_meta_t *blocks, void *block_start) {
    for (size_t i = 0; i < 1000; i++) {
        int64_t block_id= blocks_alloc(blocks, block_start);
        if (block_id == -1) {
            printf("Alloc failed at iteration %zu\n", i);
            return;
        }
    }
}

int main() {
    uint64_t totalsize=1024 * 4;
    uint8_t *pool=malloc(totalsize);
    blocks_meta_t blocks;
    blocks_init(&blocks,totalsize, 64);
    test_alloc(&blocks, pool);
    return 0;
}