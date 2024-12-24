#ifndef CACHE_H
#define CACHE_H

#include "grfs.h"

#define TOTAL_MAX_CACHE_SIZE (128 * 1024 * 1024)

#define CACHE_BLOCK_SIZE BLOCK_SIZE
#define CACHE_BLOCK_SECTOR SECTOR_IN_BLOCK

#define WAY_NUM 4
#define LINE_NUM 64

#define INPUT_BITS 32
#define TAG_BITS 23
#define INDEX_BITS 6
#define OFFSET_BITS 3

#define OFFSET_MASK 0x7
#define INDEX_MASK 0x1F8
#define TAG_MASK 0xFFFFFE

#define GET_OFFSET(addr) ((addr) & OFFSET_MASK)
#define GET_INDEX(addr) (((addr) & INDEX_MASK) >> OFFSET_BITS)
#define GET_TAG(addr) (((addr) & TAG_MASK) >> (OFFSET_BITS + INDEX_BITS))

#define GET_SECTOR(tag, index) (tag << (INDEX_BITS + OFFSET_BITS) | index << OFFSET_BITS)

typedef struct {
    char data[SECTOR_SIZE];
} sector_t;

typedef struct {
    char data[CACHE_BLOCK_SIZE];
} block_t;

typedef struct cache_block {
    uint32_t tag : 30;
    unsigned char valid : 1;
    unsigned char dirty : 1;
    block_t* data;
    struct cache_block* next;
} cache_block_t;

typedef struct {
    cache_block_t *head, *tail;
    int size;
} cache_line_t;


int fs_cache_init();
sector_t* sector_read(uint32_t sector_id);
void sector_put(uint32_t sector_id);
void cache_flush();
void change_cache_policy(int policy);
void change_write_back_freq(int freq);


#endif /* CACHE_H */