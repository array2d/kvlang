#pragma once

#ifndef blockmalloc_H
#define blockmalloc_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t total_size;
    uint64_t block_size;
    uint8_t  sizeof_block_head:4;
    uint64_t malloc_blocks:60;
    uint64_t used_blocks;
    int64_t  free_next_id;   
    int64_t  lock;            
} blocks_meta_t;

int     blocks_init(blocks_meta_t *meta, uint64_t total_size, uint64_t block_size);
int64_t blocks_alloc(blocks_meta_t *meta, void *block_start);
void    blocks_free(blocks_meta_t *meta, void *block_start, uint64_t block_id);

#ifdef BLOCKMALLOC_IMPLEMENTATION
#define BLOCKS_INLINE static inline
#else
#define BLOCKS_INLINE
#endif
BLOCKS_INLINE int64_t block_offset(const blocks_meta_t *meta, uint64_t block_id);
BLOCKS_INLINE int64_t blockdata_offset(const blocks_meta_t *meta, uint64_t block_id);
int64_t blockid_byblockoffset(const blocks_meta_t *meta, uint64_t block_offset);
int64_t blockid_bydataoffset(const blocks_meta_t *meta, uint64_t data_offset);

#endif

#ifdef BLOCKMALLOC_IMPLEMENTATION

#ifdef ENABLE_LOG
    #ifdef PLATFORM_LOG
        #define LOG(fmt, ...) PLATFORM_LOG("[blockmalloc] " fmt, ##__VA_ARGS__)
    #else
        #define LOG(fmt, ...) ((void)0)
    #endif
#else
    #define LOG(fmt, ...) ((void)0)
#endif

#if defined(_MSC_VER)
#include <windows.h>
#include <intrin.h>
#endif

#if defined(__x86_64__) || defined(__i386__)
static inline void cpu_relax(void) { __asm__ __volatile__("pause" ::: "memory"); }
#elif defined(__aarch64__) || defined(__arm__)
static inline void cpu_relax(void) { __asm__ __volatile__("yield" ::: "memory"); }
#elif defined(_MSC_VER)
static inline void cpu_relax(void) { YieldProcessor(); }
#else
static inline void cpu_relax(void) { (void)0; }
#endif

#ifdef _MSC_VER
static inline int64_t spin_atomic_exchange(int64_t *ptr, int64_t val)
{
    
    return (int64_t)InterlockedExchange64((volatile LONG64 *)ptr, (LONG64)val);
}
static inline void spin_atomic_store(int64_t *ptr, int64_t val)
{
    
    InterlockedExchange64((volatile LONG64 *)ptr, (LONG64)val);
}
#else
static inline int64_t spin_atomic_exchange(int64_t *ptr, int64_t val)
{
    return __atomic_exchange_n((int64_t *)ptr, val, __ATOMIC_ACQ_REL);
}
static inline void spin_atomic_store(int64_t *ptr, int64_t val)
{
    __atomic_store_n((int64_t *)ptr, val, __ATOMIC_RELEASE);
}
#endif

static inline void spin_lock(int64_t *lock)
{
    while (spin_atomic_exchange(lock, 1))
    {
        cpu_relax();
    }
}

static inline void spin_unlock(int64_t *lock)
{
    spin_atomic_store(lock, 0);
}

#ifdef _MSC_VER
#pragma pack(push, 1)
#endif

typedef struct
{
    uint8_t used : 1;
    uint8_t has_next_free : 1;
    uint16_t next_free_id : 14;
}
#ifdef __GNUC__
__attribute__((packed))
#endif
block16_t;

typedef struct
{
    uint8_t used : 1;
    uint8_t has_next_free : 1;
    uint32_t next_free_id : 30;
}
#ifdef __GNUC__
__attribute__((packed))
#endif
block32_t;

typedef struct
{
    uint8_t used : 1;
    uint8_t has_next_free : 1;
    uint64_t next_free_id : 62;
}
#ifdef __GNUC__
__attribute__((packed))
#endif
block64_t;

#ifdef _MSC_VER
#pragma pack(pop)
#endif

static inline void setblock_t(const blocks_meta_t *meta, void *block_ptr, uint8_t used, int64_t free_next_id)
{
    switch (meta->sizeof_block_head)
    {
    case 2:
    {
        block16_t *block = (block16_t *)block_ptr;
        block->used = used;
        block->has_next_free = (free_next_id != -1) ? 1 : 0;
        block->next_free_id = (free_next_id != -1) ? (uint16_t)free_next_id : 0;
        break;
    }
    case 4:
    {
        block32_t *block = (block32_t *)block_ptr;
       block->used = used;
        block->has_next_free = (free_next_id != -1) ? 1 : 0;
        block->next_free_id = (free_next_id != -1) ? (uint32_t)free_next_id : 0;
        break;
    }
    case 8:
    {
        block64_t *block = (block64_t *)block_ptr;
       block->used = used;
        block->has_next_free = (free_next_id != -1) ? 1 : 0;
        block->next_free_id = (free_next_id != -1) ? (uint64_t)free_next_id : 0;
        break;
    }
    default:
        LOG("[ERROR] Invalid sizeof_block_head: %d, returning default block", meta->sizeof_block_head);
        
        break;
    }
}

static inline block64_t getblock_t(const blocks_meta_t *meta, void *block_ptr)
{
    switch (meta->sizeof_block_head)
    {
    case 2:
    {
        block16_t *block = (block16_t *)block_ptr;
        return (block64_t){
            .used = block->used,
            .has_next_free = block->has_next_free,
            .next_free_id = block->next_free_id,
        };
    }
    case 4:
    {
        block32_t *block = (block32_t *)block_ptr;
        return (block64_t){
            .used = block->used,
            .has_next_free = block->has_next_free,
            .next_free_id = block->next_free_id,
        };
    }
    case 8:
    {
        block64_t *block = (block64_t *)block_ptr;
        return (block64_t){
            .used = block->used,
            .has_next_free = block->has_next_free,
            .next_free_id = block->next_free_id,
        };
    }
    default:
        
        LOG("[ERROR] Invalid sizeof_block_head: %d, returning default block", meta->sizeof_block_head);
        
        return (block64_t){0};
    }
}

#define BLOCK_SIZE(meta) (meta->sizeof_block_head + meta->block_size)

int blocks_init(blocks_meta_t *meta, const uint64_t total_size, const uint64_t block_size)
{
    if (meta == NULL)
    {
        LOG("[ERROR] meta is NULL");
        return -1;
    }
    *meta = (blocks_meta_t){
        .total_size = total_size,
        .block_size = block_size,
        .malloc_blocks = 0,
        .used_blocks = 0,
        .free_next_id = -1, 
        .lock = 0,          
    };
    uint64_t min_header_size = 2; 
    uint64_t max_blocks = meta->total_size / (min_header_size + meta->block_size);
    if (max_blocks <= (32767ULL / 4ULL))
        meta->sizeof_block_head = 2; 
    else if (max_blocks <= (2147483647ULL / 4ULL))
        meta->sizeof_block_head = 4; 
    else
        meta->sizeof_block_head = 8; 
    return 0;
}

BLOCKS_INLINE int64_t block_offset(const blocks_meta_t *meta, const uint64_t block_id)
{
    int64_t offset = block_id * BLOCK_SIZE(meta);
    return offset;
}
BLOCKS_INLINE int64_t blockdata_offset(const blocks_meta_t *meta, const uint64_t block_id)
{
    int64_t offset = block_offset(meta, block_id);
    return offset + meta->sizeof_block_head;
}

int64_t blockid_byblockoffset(const blocks_meta_t *meta, const uint64_t block_offset)
{
    uint64_t block_size = BLOCK_SIZE(meta);
    if (block_offset % block_size != 0)
    {
        
        uint64_t suggested_offset = block_offset - (block_offset % block_size);
        LOG("[ERROR] block_offset %zu is not aligned with BLOCK_SIZE %zu, suggested offset: %zu",
            block_offset, block_size, suggested_offset);
        return -1;
    }
    uint64_t block_id = block_offset / block_size;
    return block_id;
}

int64_t blockid_bydataoffset(const blocks_meta_t *meta, const uint64_t data_offset)
{
    return blockid_byblockoffset(meta, data_offset - meta->sizeof_block_head);
}

int64_t blocks_alloc(blocks_meta_t *meta, void *block_start)
{
    if (meta == NULL || block_start == NULL)
    {
        LOG("[ERROR] meta or block_start is NULL");
        return -1;
    }
    spin_lock(&meta->lock);
    if (meta->free_next_id == -1)
    {
        uint64_t totalused_size = block_offset(meta, meta->malloc_blocks);
        if (totalused_size + BLOCK_SIZE(meta) > meta->total_size)
        {
            LOG("[ERROR] out of memory,%zu(used_size)= %zu(malloc_blocks)*(sizeof(block_head)=%d)+%zu(block_size)),when total_size %zu",
                totalused_size, (uint64_t)(meta->malloc_blocks), (uint8_t)(meta->sizeof_block_head), meta->block_size, meta->total_size);
            spin_unlock(&meta->lock);
            return -1;
        }
        else
        {
            meta->malloc_blocks++;
            meta->used_blocks++;

            uint64_t block_id = meta->malloc_blocks - 1;
            int64_t b_offset = block_offset(meta, block_id);
            setblock_t(meta, (uint8_t *)block_start + b_offset, 1, -1);
            LOG("[INFO] append block %zu,blocks usage: %zu/%zu", block_id, (uint64_t)(meta->used_blocks), (uint64_t)(meta->malloc_blocks));
            spin_unlock(&meta->lock);
            return block_id;
        }
    }
    else
    {
        uint64_t free_id = meta->free_next_id;
        int64_t b_offset = block_offset(meta, free_id);
        block64_t free_block = getblock_t(meta, (uint8_t *)block_start + b_offset);
        meta->free_next_id = free_block.has_next_free ? (int64_t)free_block.next_free_id : -1;
        meta->used_blocks++;
        setblock_t(meta, (uint8_t *)block_start + b_offset, 1, -1);

        LOG("[INFO] reusing block %zu, blocks usage: %zu/%zu",
            free_id, (uint64_t)(meta->used_blocks), (uint64_t)(meta->malloc_blocks));
        spin_unlock(&meta->lock);
        return free_id;
    }
}

void blocks_free(blocks_meta_t *meta, void *block_start, const uint64_t block_id)
{
    if (meta == NULL || block_start == NULL)
    {
        LOG("[ERROR] meta or block_start must not NULL");
        return;
    }
    spin_lock(&meta->lock);
    if (block_id >= meta->malloc_blocks)
    {
        LOG("[ERROR] block id %zu out of range", block_id);
        spin_unlock(&(meta->lock));
        return;
    }
    int64_t b_offset = block_offset(meta, block_id);
    block64_t free_block = getblock_t(meta, (uint8_t *)block_start + b_offset);

    switch (free_block.used)
    {
    case 0:
        LOG("[WARN] block id %zu already free", block_id);
        break;
    case 1:
        setblock_t(meta, (uint8_t *)block_start + b_offset, 0, meta->free_next_id);
        meta->free_next_id = block_id;
        meta->used_blocks--;
        break;
    default:
        LOG("[WARN] block id %zu status invalid %d,who make this?", block_id, free_block.used);
        break;
    }
    LOG("[INFO] block id %zu freed", block_id);
    spin_unlock(&(meta->lock));
}

#endif
