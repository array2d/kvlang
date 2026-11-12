#include <stdio.h>

#include "blockmalloc/blockmalloc.h"

void test_free(blocks_meta_t *blocks, void* block_start) {
    int64_t block_id = blocks_alloc(blocks, block_start);
    for(int i=0;i<10;i++){
         block_id = blocks_alloc(blocks, block_start);
    }
    blocks_free(blocks, block_start, 9);
    blocks_alloc(blocks, block_start);
    blocks_free(blocks, block_start, 9);
    blocks_free(blocks, block_start, 3);
    blocks_alloc(blocks, block_start);
    blocks_free(blocks, block_start, 5);
    blocks_free(blocks, block_start, 4);
    blocks_alloc(blocks, block_start);
}

int main(int argc, char *argv[]) {
 
    uint8_t pool[1024 * 4];
    blocks_meta_t blocks;
    blocks_init(&blocks, 1024 * 4, 64);
    printf("blocks inited\n");
    test_free(&blocks, pool);
    return 0;
}