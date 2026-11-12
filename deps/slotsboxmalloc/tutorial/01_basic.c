#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <slotsboxmalloc/slotsboxobj.h>

int main(void) {
    size_t data_size = 8ULL * 64 * 64 * 64 * 64;
    size_t head_size = sbo_meta_size(data_size, 256 * 1024);

    uint8_t *meta = (uint8_t *)malloc(head_size);
    uint8_t *data = (uint8_t *)malloc(data_size);
    if (!meta || !data) { printf("malloc fail\n"); return 1; }

    if (sbo_init(meta, head_size, data_size) != 0) {
        printf("sbo_init fail\n"); return 1;
    }
    printf("sbo_init ok\n");

    uint64_t p5  = sbo_alloc(meta, 5);
    uint64_t p23 = sbo_alloc(meta, 23);
    printf("alloc 5B→+%lu  23B→+%lu\n", p5, p23);

    sbo_free(meta, p23);
    uint64_t p7 = sbo_alloc(meta, 7);
    printf("free 23, alloc 7B→+%lu\n", p7);

    printf("+%lu size=%lu  +%lu size=%lu\n",
           p5, sbo_allocated_size(meta, p5),
           p7, sbo_allocated_size(meta, p7));

    void *d5 = sbo_data_ptr(meta, data, p5);
    void *d7 = sbo_data_ptr(meta, data, p7);
    memset(d5, 0xAB, 5); memset(d7, 0xCD, 7);
    printf("data write: %02x %02x\n", *(uint8_t*)d5, *(uint8_t*)d7);

    sbo_free(meta, p5);
    sbo_free(meta, p7);
    free(meta); free(data);
    printf("done\n");
    return 0;
}
