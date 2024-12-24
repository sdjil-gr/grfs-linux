#include <assert.h>
#include "cache.h"
#include "type.h"
#include "vm.h"
#include "io.h"

cache_block_t cache_block[TOTAL_MAX_CACHE_SIZE / CACHE_BLOCK_SIZE];
cache_line_t cache_line[LINE_NUM];

int remain_free_block = TOTAL_MAX_CACHE_SIZE / CACHE_BLOCK_SIZE;

// 0:write-back, 1:write-through
int page_cache_policy = 0;

// seconds for write-back to flush cache
int write_back_freq = 30;

int fs_cache_init() {
    int i;
    for (i = 0; i < LINE_NUM; i++) {
        cache_line[i].head = cache_line[i].tail = NULL;
        cache_line[i].size = 0;
    }
    for (i = 0; i < TOTAL_MAX_CACHE_SIZE / CACHE_BLOCK_SIZE; i++) {
        cache_block[i].valid = 0;
        cache_block[i].dirty = 0;
        cache_block[i].data = NULL;
        cache_block[i].next = NULL;
    }
    return 0;
}

static cache_block_t* cache_block_alloc() {
    if(remain_free_block == 0)
        return NULL;
    cache_block_t* block = &cache_block[--remain_free_block];
    block->valid = 1;
    block->dirty = 0;
    block->data = (block_t*)allocPage();
    block->next = NULL;
    return block;
}

static cache_line_t* cache_find_maxline() {
    int maxsize = cache_line[0].size;
    cache_line_t* maxline = &cache_line[0];
    for(int i = 1; i < LINE_NUM; i++) {
        if(cache_line[i].size > maxsize) {
            maxsize = cache_line[i].size;
            maxline = &cache_line[i];
        }
    }
    return maxline;
}

static void cache_line_add(cache_block_t* block, uint32_t index) {
    block->next = cache_line[index].head;
    cache_line[index].head = block;
    if(cache_line[index].tail == NULL) {
        cache_line[index].tail = block;
    }
    cache_line[index].size++;
}

static void cache_line_remove(cache_block_t* block, uint32_t index) {
    cache_block_t* p = NULL;
    if(cache_line[index].head == block) {
        cache_line[index].head = block->next;
    } else {
        p = cache_line[index].head;
        while(p->next!= block) {
            p = p->next;
        }
        p->next = block->next;
    }
    if(cache_line[index].tail == block) {
        cache_line[index].tail = p;
    }
    block->next = NULL;
    cache_line[index].size--;
}

static void cache_line_float(cache_block_t* block, uint32_t index) {
    cache_line_remove(block, index);
    cache_line_add(block, index);
}

static cache_block_t* map_cache(uint32_t sector_id) {
    uint32_t index = GET_INDEX(sector_id);
    uint32_t tag = GET_TAG(sector_id);
    for(cache_block_t* p = cache_line[index].head; p != NULL; p = p->next) {
        if(p->tag == tag) {
            cache_line_float(p, index);
            return p;
        }
    }
    return NULL;
}

static cache_block_t* cache_lru_replace() {
    cache_line_t* maxline = cache_find_maxline();
    int index = maxline - cache_line;
    cache_block_t* block = maxline->tail;
    if(GET_SECTOR(block->tag, index) == (SUPERBLOCK_BEGIN_SECTOR & ~OFFSET_MASK)) {
        cache_line_float(block, index);
        block = maxline->tail;
    }
    cache_line_remove(block, index);
    if(block->dirty) {
        bios_sd_write(KVA2PA(block->data), 8, GET_SECTOR(block->tag, index));
        block->dirty = 0;
    }
    return block;
}

static void cache_flush_block(cache_block_t* block, uint32_t index) {
    if(block->dirty) {
        bios_sd_write(KVA2PA(block->data), 8, GET_SECTOR(block->tag, index));
        block->dirty = 0;
    }
}

sector_t* sector_read(uint32_t sector_id) {
    if(sector_id >= MAX_SECTOR_NUM || sector_id < 0)
        return NULL;
    cache_block_t* block = map_cache(sector_id);
    if(block != NULL) {
        return ((sector_t*)(block->data) + GET_OFFSET(sector_id));
    }
    if(remain_free_block > 0)
        block = cache_block_alloc();
    else{
        block = cache_lru_replace();
    }
    bios_sd_read(KVA2PA(block->data), 8, sector_id & ~OFFSET_MASK);
    uint32_t index = GET_INDEX(sector_id);
    block->tag = GET_TAG(sector_id);
    cache_line_add(block, index);
    return ((sector_t*)(block->data) + GET_OFFSET(sector_id));
}

void sector_put(uint32_t sector_id){
    if(sector_id >= now_superblock->total_sectors || sector_id < 0)
        return;
    int offset = GET_OFFSET(sector_id);
    cache_block_t* block = map_cache(sector_id);
    assert(block != NULL);
    block->dirty = 1;
    if(page_cache_policy == 1)
        cache_flush_block(block, GET_INDEX(sector_id));
}

void cache_flush() {
    if(page_cache_policy == 1)
        return;
    for(int i = 0; i < LINE_NUM; i++) {
        cache_block_t* p = cache_line[i].head;
        while(p != NULL) {
            if(p->dirty) {
                bios_sd_write(KVA2PA(p->data), 8, GET_SECTOR(p->tag, i));
                p->dirty = 0;
            }
            p = p->next;
        }
    }
}

void change_cache_policy(int policy) {
    if(page_cache_policy == 0 && policy == 1)
        cache_flush();
    page_cache_policy = policy;
}

void change_write_back_freq(int freq) {
    write_back_freq = freq;
}