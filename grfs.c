#include "io.h"
#include "grfs.h"
#include "vm.h"
#include "cache.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

spinlock_t fs_lock;
superblock_t* now_superblock;

static int check_fs_in_sd();
static void init_superblock();
static void init_inode(int parent_ino, int self_ino, int dir_tag);
static int inode_mapto_block(int ino, int block_index, int alloc);
static int alloc_inode();
static int release_inode(int ino);
static int alloc_block();
static int release_block(int block_id);
static int release_block_recursive(int block_id, int depth);
static inode_t* get_inode(int ino);
static int put_inode(int ino);
static sector_t* get_sector_of_block(int block_id, int sector_index);
static int put_sector_of_block(int block_id, int sector_index);
static int set_dentry(int ino, char* name, dentry_t* dentry);
static void init_dentry_arr(dentry_t* dentry, int parent_ino, int self_ino, int first);
static dentry_t* find_dentry_byname(char* name, int* count, dentry_t* dentrys, int dentry_num);
static dentry_t* find_dentry_byino(int ino, int* count, dentry_t* dentrys, int dentry_num);
static dentry_t* find_empty_dentry(dentry_t* dentrys, int dentry_num);
static int parentino_to_childino(int parent_ino, char* name);
static int walk_by_path(char* path, int origin_ino);
static int add_dir(int parent_ino, char* name);
static int del_dir(int parent_ino, char* name);
static int add_file(int parent_ino, char* name, int* ln_ino);
static int del_file(int parent_ino, char* name);
static char* get_memstr(char* str, uint64_t mem_size);
static char* get_name_and_ino_by_path(char* path, int* ret_ino);


static int check_fs_in_sd(){
    now_superblock = (superblock_t*)sector_read(FILE_SYSTEM_BEGIN_SECTOR + SUPERBLOCK_BEGIN_SECTOR);
    int ret = 1;
    if(now_superblock->magic!= SUPERBLOCK_MAGIC)
        ret = 0;
    return ret;
}

static void clear_map(int begin_sector, int occupied_sectors){
    for(int i = 0; i < occupied_sectors; i++){
        sector_t* sector = sector_read(begin_sector + i);
        memset(sector, 0, SECTOR_SIZE);
        sector_put(begin_sector + i);
    }
}

static void init_superblock(){
    assert(sizeof(superblock_t) == SECTOR_SIZE);
    assert(sizeof(inode_t) == INODE_SIZE);
    assert(sizeof(dentry_t) == DENTRY_SIZE);
    now_superblock = (superblock_t*)sector_read(FILE_SYSTEM_BEGIN_SECTOR + SUPERBLOCK_BEGIN_SECTOR);
    now_superblock->magic = SUPERBLOCK_MAGIC;
    now_superblock->superblock_sector = FILE_SYSTEM_BEGIN_SECTOR + SUPERBLOCK_BEGIN_SECTOR;
    now_superblock->begin_sector = FILE_SYSTEM_BEGIN_SECTOR;
    now_superblock->total_sectors = MAX_SECTOR_NUM;
    memcpy(now_superblock->name, FILE_SYSTEM_NAME, sizeof(FILE_SYSTEM_NAME) - 1);

    now_superblock->inodemap_begin_sector = FILE_SYSTEM_BEGIN_SECTOR + INODEMAP_BEGIN_SECTOR;
    now_superblock->inodemap_occupied_sectors = INODEMAP_OCCUPIED_SECTORS;

    now_superblock->blockmap_begin_sector = FILE_SYSTEM_BEGIN_SECTOR + BLOCKMAP_BEGIN_SECTOR;
    now_superblock->blockmap_occupied_sectors = BLOCKMAP_OCCUPIED_SECTORS;

    now_superblock->inode_table_begin_sector = FILE_SYSTEM_BEGIN_SECTOR + INODE_TABLE_BEGIN_SECTOR;
    now_superblock->inode_table_occupied_sectors = INODE_TABLE_OCCUPIED_SECTORS;
    now_superblock->inode_size = INODE_SIZE;
    now_superblock->inode_num = 0;
    now_superblock->inode_max_num = INODE_MAX_NUM;

    now_superblock->block_table_begin_sector = FILE_SYSTEM_BEGIN_SECTOR + BLOCK_TABLE_BEGIN_SECTOR;
    now_superblock->block_table_occupied_sectors = BLOCK_TABLE_OCCUPIED_SECTORS;
    now_superblock->block_size = BLOCK_SIZE;
    now_superblock->block_num = 0;
    now_superblock->block_max_num = BLOCK_MAX_NUM;

    clear_map(now_superblock->inodemap_begin_sector, now_superblock->inodemap_occupied_sectors);
    clear_map(now_superblock->blockmap_begin_sector, now_superblock->blockmap_occupied_sectors);
    cache_flush();

    now_superblock->root_ino = alloc_inode();
    init_inode(now_superblock->root_ino, now_superblock->root_ino, 1);

    sector_put(FILE_SYSTEM_BEGIN_SECTOR + SUPERBLOCK_BEGIN_SECTOR);
}

static void init_inode(int parent_ino, int self_ino, int dir_tag){
    // already hold the fs_lock
    assert(dir_tag == 1 || dir_tag == 0);
    inode_t* inode = (inode_t*)get_inode(self_ino);
    inode->mode = S_READ | S_WRITE | S_EXEC;
    if(dir_tag)
        inode->mode |= S_DIR;
    inode->nlinks = 1;
    inode->size = 0;
    // inode->ctime = 0;
    // inode->atime = 0;
    // inode->mtime = 0;
    for(int i = 0; i < INODE_DIRECT_BLOCK; i++)
        inode->block_ptr[i] = -1;
    inode->indirect1_ptr = -1;
    inode->indirect2_ptr = -1;
    inode->indirect3_ptr = -1;
    
    if(dir_tag){
        inode->size = 2;
        inode->block_ptr[0] = alloc_block();

        for(int i = 0; i < SECTOR_IN_BLOCK; i++){
            dentry_t* root_dentry = (dentry_t*)get_sector_of_block(inode->block_ptr[0], i);
            init_dentry_arr(root_dentry, parent_ino, self_ino, (i==0));
            put_sector_of_block(inode->block_ptr[0], i);
        }
    }
    put_inode(self_ino);
}

static int inode_mapto_block(int ino, int block_index, int alloc){
    // already hold the fs_lock
    assert(ino < now_superblock->inode_max_num && ino >= 0);
    inode_t* inode = (inode_t*)get_inode(ino);

    if(block_index < INODE_DIRECT_BLOCK){
        if(inode->block_ptr[block_index] == -1){
            if(!alloc)
                return -1;
            int new_block_id = alloc_block();
            if(new_block_id == -1)
                return -1;
            inode->block_ptr[block_index] = new_block_id;
            put_inode(ino);
        }
        return inode->block_ptr[block_index];
    } 

    block_index -= INODE_DIRECT_BLOCK;
    if (block_index < INODE_INDIRECT1_BLOCK){
        int* block_id;
        if(inode->indirect1_ptr == -1){
            if(!alloc)
                return -1;
            int new_block_id = alloc_block();
            if(new_block_id == -1)
                return -1;
            inode->indirect1_ptr = new_block_id;
            put_inode(ino);
            for(int i = 0; i < SECTOR_IN_BLOCK; i++){
                int* blockids = (int*)get_sector_of_block(inode->indirect1_ptr, i);
                for(int j = 0; j < SECTOR_SIZE / 4; j++)
                    blockids[j] = -1;
                put_sector_of_block(inode->indirect1_ptr, i);
            }
        }
        
        int sector = block_index/ (SECTOR_SIZE / 4);
        int* blockids = (int*)get_sector_of_block(inode->indirect1_ptr, sector);
        block_id = &blockids[block_index % (SECTOR_SIZE / 4)];
        if(*block_id == -1){
            if(!alloc)
                return -1;
            int new_block_id = alloc_block();
            if(new_block_id == -1)
                return -1;
            *block_id = new_block_id;
            put_sector_of_block(inode->indirect1_ptr, sector);
        }
        return *block_id;
    }

    block_index -= INODE_INDIRECT1_BLOCK;
    if (block_index < INODE_INDIRECT2_BLOCK){
        int *block_id;

        if(inode->indirect2_ptr == -1){
            if(!alloc)
                return -1;
            int new_block_id = alloc_block();
            if(new_block_id == -1)
                return -1;
            inode->indirect2_ptr = new_block_id;
            put_inode(ino);
            for(int i = 0; i < SECTOR_IN_BLOCK; i++){
                int* blockids = (int*)get_sector_of_block(inode->indirect2_ptr, i);
                for(int j = 0; j < SECTOR_SIZE / 4; j++)
                    blockids[j] = -1;
                put_sector_of_block(inode->indirect2_ptr, i);
            }
        }

        int sector = (block_index / INODE_INDIRECT1_BLOCK) / (SECTOR_SIZE / 4);
        int* blockids = (int*)get_sector_of_block(inode->indirect2_ptr, sector);
        block_id = &blockids[(block_index / INODE_INDIRECT1_BLOCK) % (SECTOR_SIZE / 4)];
        int block_id1 = *block_id;
        if(*block_id == -1){
            if(!alloc)
                return -1;
            int new_block_id = alloc_block();
            if(new_block_id == -1)
                return -1;
            *block_id = new_block_id;
            block_id1 = new_block_id;
            put_sector_of_block(inode->indirect2_ptr, sector);
            for(int i = 0; i < SECTOR_IN_BLOCK; i++){
                blockids = (int*)get_sector_of_block(block_id1, i);
                for(int j = 0; j < SECTOR_SIZE / 4; j++)
                    blockids[j] = -1;
                put_sector_of_block(block_id1, i);
            }
        }

        block_index %= INODE_INDIRECT1_BLOCK;
        sector = block_index / (SECTOR_SIZE / 4);
        blockids = (int*)get_sector_of_block(block_id1, sector);
        block_id = &blockids[block_index % (SECTOR_SIZE / 4)];
        if(*block_id == -1){
            if(!alloc)
                return -1;
            int new_block_id = alloc_block();
            if(new_block_id == -1)
                return -1;
            *block_id = new_block_id;
            put_sector_of_block(block_id1, sector);
        }
        return *block_id;
    }

    block_index -= INODE_INDIRECT2_BLOCK;
    if (block_index < INODE_INDIRECT3_BLOCK){
        int *block_id;

        if(inode->indirect3_ptr == -1){
            if(!alloc)
                return -1;
            int new_block_id = alloc_block();
            if(new_block_id == -1)
                return -1;
            inode->indirect3_ptr = new_block_id;
            put_inode(ino);
            for(int i = 0; i < SECTOR_IN_BLOCK; i++){
                int* blockids = (int*)get_sector_of_block(inode->indirect3_ptr, i);
                for(int j = 0; j < SECTOR_SIZE / 4; j++)
                    blockids[j] = -1;
                put_sector_of_block(inode->indirect3_ptr, i);
            }
        }
        
        int sector = (block_index / INODE_INDIRECT2_BLOCK) / (SECTOR_SIZE / 4);
        int* blockids = (int*)get_sector_of_block(inode->indirect3_ptr, sector);
        block_id = &blockids[(block_index / INODE_INDIRECT2_BLOCK) % (SECTOR_SIZE / 4)];
        int block_id1 = *block_id;
        if(*block_id == -1){
            if(!alloc)
                return -1;
            int new_block_id = alloc_block();
            if(new_block_id == -1)
                return -1;
            *block_id = new_block_id;
            block_id1 = new_block_id;
            put_sector_of_block(inode->indirect3_ptr, sector);
            for(int i = 0; i < SECTOR_IN_BLOCK; i++){
                blockids = (int*)get_sector_of_block(block_id1, i);
                for(int j = 0; j < SECTOR_SIZE / 4; j++)
                    blockids[j] = -1;
                put_sector_of_block(block_id1, i);
            }
        }

        block_index %= INODE_INDIRECT2_BLOCK;
        sector  = (block_index / INODE_INDIRECT1_BLOCK) / (SECTOR_SIZE / 4);
        blockids = (int*)get_sector_of_block(block_id1, sector);
        block_id = &blockids[(block_index / INODE_INDIRECT1_BLOCK)% (SECTOR_SIZE / 4)];
        int block_id2 = *block_id;
        if(*block_id == -1){
            if(!alloc)
                return -1;
            int new_block_id = alloc_block();
            if(new_block_id == -1)
                return -1;
            *block_id = new_block_id;
            block_id2 = new_block_id;
            put_sector_of_block(block_id1, sector);
            for(int i = 0; i < SECTOR_IN_BLOCK; i++){
                blockids = (int*)get_sector_of_block(block_id2, i);
                for(int j = 0; j < SECTOR_SIZE / 4; j++)
                    blockids[j] = -1;
                put_sector_of_block(block_id2, i);
            }
        }

        block_index %= INODE_INDIRECT1_BLOCK;
        sector = block_index / (SECTOR_SIZE / 4);
        blockids = (int*)get_sector_of_block(block_id2, sector);
        int* block_id3 = &blockids[block_index % (SECTOR_SIZE / 4)];
        if(*block_id3 == -1){
            if(!alloc)
                return -1;
            int new_block_id = alloc_block();
            if(new_block_id == -1)
                return -1;
            *block_id3 = new_block_id;
            put_sector_of_block(block_id2, sector);
        }
        return *block_id3;
    }
    return -1;
}

static int alloc_inode(){
    // already hold the fs_lock
    int max_ino = now_superblock->inode_max_num;
    if(now_superblock->inode_num >= max_ino){
        return -1;
    }
    int sector_begin = now_superblock->inodemap_begin_sector;
    int sector_end = now_superblock->inodemap_begin_sector + now_superblock->inodemap_occupied_sectors;
    int ino = 0;

    for(int sector = sector_begin; sector < sector_end && ino < max_ino; sector++){
        uint16_t* inodemap = (uint16_t*)sector_read(sector);
        for(int index = 0; index < (SECTOR_BIT_SIZE / 16) && ino < max_ino; index++){
            uint16_t* now_map = &inodemap[index];
            for(int i = 0; i < 16 && ino < max_ino; i++){
                uint16_t mask = 1 << i;
                if((*now_map & mask) == 0){
                    *now_map |= mask;
                    sector_put(sector);
                    now_superblock->inode_num++;
                    sector_put(now_superblock->superblock_sector);
                    return ino;
                }
                ino++;
            }
        }
    }

    return -1;
}

static int release_inode(int ino){
    // already hold the fs_lock
    if(ino >= now_superblock->inode_max_num || ino < 0){
        return 0;
    }
    inode_t* inode = (inode_t*)get_inode(ino);
    for(int i = 0; i < INODE_DIRECT_BLOCK; i++){
        release_block_recursive(inode->block_ptr[i], 0);
        inode->block_ptr[i] = -1;
    }
    release_block_recursive(inode->indirect1_ptr, 1);
    inode->indirect1_ptr = -1;
    release_block_recursive(inode->indirect2_ptr, 2);
    inode->indirect2_ptr = -1;
    release_block_recursive(inode->indirect3_ptr, 3);
    inode->indirect3_ptr = -1;
    put_inode(ino);
    int sector = now_superblock->inodemap_begin_sector + (ino / SECTOR_BIT_SIZE);
    uint16_t* inodemap = (uint16_t*)sector_read(sector);
    inodemap[(ino % SECTOR_BIT_SIZE) / 16] &= ~(1 << (ino % 16));
    sector_put(sector);
    now_superblock->inode_num--;
    sector_put(now_superblock->superblock_sector);
    return 1;
}

static int alloc_block(){
    // already hold the fs_lock
    int max_id = now_superblock->block_max_num;
    if(now_superblock->block_num >= max_id){
        return -1;
    }
    int sector_begin = now_superblock->blockmap_begin_sector;
    int sector_end = now_superblock->blockmap_begin_sector + now_superblock->blockmap_occupied_sectors;
    int block_id = 0;

    for(int sector = sector_begin; sector < sector_end && block_id < max_id; sector++){
        uint16_t* blockmap = (uint16_t*)sector_read(sector);
        for(int index = 0; index < (SECTOR_BIT_SIZE / 16) && block_id < max_id; index++){
            uint16_t* now_map = &blockmap[index];
            for(int i = 0; i < 16 && block_id < max_id; i++){
                uint16_t mask = 1 << i;
                if((*now_map & mask) == 0){
                    *now_map |= mask;
                    sector_put(sector);
                    now_superblock->block_num++;
                    sector_put(now_superblock->superblock_sector);
                    return block_id;
                }
                block_id++;
            }
        }
    }

    return -1;
}

static int release_block(int block_id){
    // already hold the fs_lock
    if(block_id == -1)
        return 0;
    int sector = now_superblock->blockmap_begin_sector + (block_id / SECTOR_BIT_SIZE);
    uint16_t* blockmap = (uint16_t*)sector_read(sector);
    blockmap[(block_id % SECTOR_BIT_SIZE) / 16] &= ~(1 << (block_id % 16));
    sector_put(sector);
    now_superblock->block_num--;
    sector_put(now_superblock->superblock_sector);
    block_id = -1;
    return 1;
}

static int release_block_recursive(int block_id, int depth){
    // already hold the fs_lock
    if(block_id == -1)
        return 0;
    if(depth != 0)
        for(int i = 0; i < SECTOR_IN_BLOCK; i++){
            int* blockids = (int*)get_sector_of_block(block_id, i);
            for(int j = 0; j < SECTOR_SIZE / 4; j++){
                release_block_recursive(blockids[j], depth - 1);
                blockids[j] = -1;
            }
            put_sector_of_block(block_id, i);
        }
    return release_block(block_id);
}


static inode_t* get_inode(int ino){
    //already hold the fs_lock
    if(ino >= now_superblock->inode_max_num || ino < 0)
        return NULL;
    int sector = now_superblock->inode_table_begin_sector + (ino / INODES_IN_SECTOR);
    inode_t* tmp_inode = (inode_t*)sector_read(sector);
    return &tmp_inode[ino % INODES_IN_SECTOR];
}

static int put_inode(int ino){
    //already hold the fs_lock
    if(ino >= now_superblock->inode_max_num || ino < 0)
        return 0;
    int sector = now_superblock->inode_table_begin_sector + (ino / INODES_IN_SECTOR);
    sector_put(sector);
    return 1;
}

static sector_t* get_sector_of_block(int block_id, int sector_index){
    //already hold the fs_lock
    if(block_id >= now_superblock->block_max_num || block_id < 0)
        return 0;
    if(sector_index >= SECTOR_IN_BLOCK || sector_index < 0)
        return 0;
    int sector = now_superblock->block_table_begin_sector + (block_id * SECTOR_IN_BLOCK) + sector_index;
    return sector_read(sector);
}

static int put_sector_of_block(int block_id, int sector_index){
    //already hold the fs_lock
    if(block_id >= now_superblock->block_max_num || block_id < 0)
        return 0;
    if(sector_index >= SECTOR_IN_BLOCK || sector_index < 0)
        return 0;
    int sector = now_superblock->block_table_begin_sector + (block_id * SECTOR_IN_BLOCK) + sector_index;
    sector_put(sector);
    return 1;
}

static int set_dentry(int ino, char* name, dentry_t* dentry){
    //already hold the fs_lock
    dentry->inode_num = ino;
    if(*name == '\0')
        dentry->name[0] = '\0';
    else
        strcpy(dentry->name, name);
    return 1;
}

static void init_dentry_arr(dentry_t* dentry, int parent_ino, int self_ino, int first){
    //already hold the fs_lock
    for(int i = 0; i < SECTOR_SIZE / DENTRY_SIZE; i++){
        set_dentry(-1, "", &dentry[i]);
    }
    if(first){
        set_dentry(self_ino, ".", &dentry[0]);
        set_dentry(parent_ino, "..", &dentry[1]);
    }
}


static dentry_t* find_dentry_byname(char* name, int* count, dentry_t* dentrys, int dentry_num){
    //already hold the fs_lock
    if(name == NULL || *name == '\0')
        return NULL;
    for(int i = 0; i < dentry_num; i++){
        if(dentrys[i].inode_num == -1)
            continue;
        if(count != NULL)
            *count += 1;
        if(strcmp(dentrys[i].name, name) == 0){
            return &dentrys[i];
        }
    }
    return NULL;
}

static dentry_t* find_dentry_byino(int ino, int* count, dentry_t* dentrys, int dentry_num){
    //already hold the fs_lock
    if(ino < 0 || ino >= now_superblock->inode_max_num)
        return NULL;
    for(int i = 0; i < dentry_num; i++){
        if(dentrys[i].inode_num == -1)
            continue;
        if(count != NULL)
            *count += 1;
        if(dentrys[i].inode_num == ino){
            return &dentrys[i];
        }
    }
    return NULL;
}

static dentry_t* find_empty_dentry(dentry_t* dentrys, int dentry_num){
    //already hold the fs_lock
    for(int i = 0; i < dentry_num; i++){
        if(dentrys[i].inode_num == -1)
            return &dentrys[i];
    }
    return NULL;
}

static int parentino_to_childino(int parent_ino, char* name){
    //already hold the fs_lock
    inode_t* parent_inode = get_inode(parent_ino);
    if((parent_inode->mode&S_DIR)==0) //parent is not a directory
        return -1;
    int child_ino = -1;
    int count = 0;
    for(int i = 0;; i++){
        int block_id = inode_mapto_block(parent_ino, i, 0);
        if(block_id == -1)
            return -1;
        for(int j = 0; j < SECTOR_IN_BLOCK; j++){
            dentry_t* dentrys = (dentry_t*)get_sector_of_block(block_id, j);
            dentry_t* child_dentry = find_dentry_byname(name, &count, dentrys, DENTRYS_IN_SECTOR);
            if(child_dentry != NULL){
                child_ino = child_dentry->inode_num;
                if(child_ino != -1)
                    return child_ino;
                else
                    assert(0);
            }
            if(count >= parent_inode->size)
                return -1;
        }
    }
}

static int walk_by_path(char* path, int origin_ino){
    // path should be in buffer

    int ino = origin_ino;
    char* tmp = path;
    while(1){
        if(*tmp == '/' || *tmp == '\0'){
            int stop = (*tmp == '\0');
            *tmp = '\0';
            if(*path == '\0'){
                if(!stop){
                    // printf("\"//\" in path is not allowed\n");
                    tmp++;
                    path = tmp;
                    continue;
                } 
                break;
            }
            ino = parentino_to_childino(ino, path);
            if(ino == -1){
                // printf("no such directory\n");
                return -1;
                break;
            }
            if(stop)
                break;
            path = tmp+1;
        } 
        tmp++;
    }
    return ino;
}

static int add_dir(int parent_ino, char* name){
    //already hold the fs_lock
    inode_t* parent_inode = get_inode(parent_ino);
    dentry_t* new_dentry;
    for(int i = 0;; i++){
        int block_id = inode_mapto_block(parent_ino, i, 1);
        assert(block_id != -1);
        for(int j = 0; j < SECTOR_IN_BLOCK; j++){
            dentry_t* dentrys = (dentry_t*)get_sector_of_block(block_id, j);
            new_dentry = find_empty_dentry(dentrys, DENTRYS_IN_SECTOR);
            if(new_dentry != NULL){
                int new_ino = alloc_inode();
                if(new_ino == -1)
                    return 0;
                set_dentry(new_ino, name, new_dentry);
                init_inode(parent_ino, new_ino, 1);
                parent_inode->size++;
                put_inode(parent_ino);
                put_sector_of_block(block_id, j);
                return 1;
            }
        }
    }
    return 0;
}

static int del_dir(int parent_ino, char* name){
    //already hold the fs_lock
    inode_t* parent_inode = get_inode(parent_ino);
    int count = 0;
    for(int i = 0;; i++){
        int block_id = inode_mapto_block(parent_ino, i, 0);
        if(block_id == -1)
            return 0;
        for(int j = 0; j < SECTOR_IN_BLOCK; j++){
            dentry_t* dentrys = (dentry_t*)get_sector_of_block(block_id, j);
            dentry_t* child_dentry = find_dentry_byname(name, &count, dentrys, DENTRYS_IN_SECTOR);
            if(child_dentry != NULL){
                int child_ino = child_dentry->inode_num;
                if(child_ino == now_superblock->root_ino || child_ino == now_ino)// root
                    return -2;
                inode_t* child_inode = get_inode(child_ino);
                if((child_inode->mode & S_DIR) == 0) // not a directory
                    return 0;
                if(child_inode->nlinks == 1 && child_inode->size > 2) // not empty
                    return -2;
                child_inode->nlinks--;
                if(child_inode->nlinks == 0) // no links left
                    release_inode(child_ino);
                else
                    put_inode(child_ino);
                set_dentry(-1, "", child_dentry);
                parent_inode->size--;
                put_sector_of_block(block_id, j);
                put_inode(parent_ino);
                return 1;
            }
            if(count == parent_inode->size)
                return 0;
        }
        if(count == parent_inode->size)
            return 0;
    }
    return 0;
}

static int add_file(int parent_ino, char* name, int* ln_ino){
    //already hold the fs_lock
    inode_t* parent_inode = get_inode(parent_ino);
    dentry_t* new_dentry;
    for(int i = 0;; i++){
        int block_id = inode_mapto_block(parent_ino, i, 1);
        assert(block_id != -1);
        for(int j = 0; j < SECTOR_IN_BLOCK; j++){
            dentry_t* dentrys = (dentry_t*)get_sector_of_block(block_id, j);
            new_dentry = find_empty_dentry(dentrys, DENTRYS_IN_SECTOR);
            if(new_dentry != NULL){
                int ino_to_set;
                if(ln_ino == NULL){
                    ino_to_set = alloc_inode();
                    if(ino_to_set<0)
                        return -1;
                    init_inode(parent_ino, ino_to_set, 0);
                } else {
                    ino_to_set = *ln_ino;
                    inode_t* ln_inode = get_inode(ino_to_set);
                    if(ln_inode->nlinks == 0)
                        return -1;
                    ln_inode->nlinks++;
                    put_inode(ino_to_set);
                }
                set_dentry(ino_to_set, name, new_dentry);
                parent_inode->size++;
                put_inode(parent_ino);
                put_sector_of_block(block_id, j);
                return ino_to_set;
            } else {
                return -1;
            }
        }
    }
    return -1;
}

static int del_file(int parent_ino, char* name){
    //already hold the fs_lock
    inode_t* parent_inode = get_inode(parent_ino);
    int count = 0;
    for(int i = 0;; i++){
        int block_id = inode_mapto_block(parent_ino, i, 0);
        if(block_id == -1)
            return 0;
        for(int j = 0; j < SECTOR_IN_BLOCK; j++){
            dentry_t* dentrys = (dentry_t*)get_sector_of_block(block_id, j);
            dentry_t* child_dentry = find_dentry_byname(name, &count, dentrys, DENTRYS_IN_SECTOR);
            if(child_dentry != NULL){
                int child_ino = child_dentry->inode_num;
                inode_t* child_inode = get_inode(child_ino);
                if((child_inode->mode & S_DIR) != 0) // not a file
                    return -2;
                child_inode->nlinks--;
                if(child_inode->nlinks == 0) // no links left
                    release_inode(child_ino);
                else
                    put_inode(child_ino);
                set_dentry(-1, "", child_dentry);
                parent_inode->size--;
                put_sector_of_block(block_id, j);
                put_inode(parent_ino);
                return 1;
            }
            if(count == parent_inode->size)
                return 0;
        }
        if(count == parent_inode->size)
            return 0;
    }
    return 0;
}


static char* get_memstr(char* str, uint64_t mem_size){
    char a[] = {' ', 'K', 'M', 'G', 'T'};
    int i = 0;
    while(mem_size >= 4096){
        mem_size /= 1024;
        i++;
    }
    memcpy(str, "     $B", 7);
    str[5] = a[i];
    int j = 3;
    while(j >= 0 && mem_size){
        str[j] = '0' + mem_size % 10;
        mem_size /= 10;
        j--;
    }
    return str + j + 1;
}

static char* get_name_and_ino_by_path(char* path, int* ret_ino){
    int len = strlen(path);
    while(path[len-1] == '/'){
        path[len-1] = '\0';
        len--;
    }
    if(len == 0){//path has only "/"
        *ret_ino = -1;
        return NULL;
    }

    int has_path = 0;
    for(int i = len-1; i >= 0; i--){
        len--;
        if(path[i] == '/'){
            has_path = 1;
            break;
        }
    }
    int ino = -1;
    char* name;
    if(has_path){//has path
        name = path + len + 1;
        path[len] = '\0';
        if(len == 0)//path is "/**"
            ino = now_superblock->root_ino;
        else{
            if(*path == '/')//path is "/*/**"
                ino = walk_by_path(path+1, now_superblock->root_ino);
            else//path is "*/**"
                ino = walk_by_path(path, now_ino);
        }
    } else {
        name = path;
        ino = now_ino;
    }
    *ret_ino = ino;
    return name;
}

int do_mkfs(){
    int ret;
    acquire(&fs_lock);
    if(check_fs_in_sd()){//already exist
        ret = 0;
    }
    else{//create new file system
        init_superblock();
        ret = 1;
    }
    now_ino = now_superblock->root_ino;
    release(&fs_lock);
    return ret;
}

int do_statfs(){
    if(now_superblock->magic != SUPERBLOCK_MAGIC){//no valid file system now
        // printf("No valid file system now!\n");
        return 0;
    }
    acquire(&fs_lock);
    printf("File system information:\n");
    printf(" - Type: %s\n", now_superblock->name);
    printf(" - Begin sector: %d\n", now_superblock->begin_sector);
    printf(" - Total sectors: %d\n", now_superblock->total_sectors);
    printf(" - Superblock sector: %d\n", now_superblock->begin_sector);
    printf(" - Block map begin sector: %d (occupied sectors: %d)\n", now_superblock->blockmap_begin_sector, now_superblock->blockmap_occupied_sectors);
    printf(" - Inode map begin sector: %d (occupied sectors: %d)\n", now_superblock->inodemap_begin_sector, now_superblock->inodemap_occupied_sectors);
    printf(" - Inode table begin sector: %d (occupied sectors: %d)\n", now_superblock->inode_table_begin_sector, now_superblock->inode_table_occupied_sectors);
    printf(" - Block table begin sector: %d (occupied sectors: %d)\n", now_superblock->block_table_begin_sector, now_superblock->block_table_occupied_sectors);
    printf(" - Inode size: %d ; Inode occupied: %d/%d (%d%%)\n", now_superblock->inode_size, now_superblock->inode_num, now_superblock->inode_max_num, now_superblock->inode_num * 100 / now_superblock->inode_max_num);
    printf(" - Block size: %d ; Block occupied: %d/%d (%d%%)\n", now_superblock->block_size, now_superblock->block_num, now_superblock->block_max_num, now_superblock->block_num * 100 / now_superblock->block_max_num);
    uint64_t used_size = (now_superblock->block_num * now_superblock->block_size);
    uint64_t total_size = (now_superblock->total_sectors * SECTOR_SIZE);
    char used_str[] = "     $B";
    char total_str[] = "     $B";
    char* u = get_memstr(used_str, used_size);
    char* t = get_memstr(total_str, total_size);
    printf(" - Used: %s / %s (%d%%)\n", u, t, now_superblock->block_num * 100 / now_superblock->block_max_num);
    release(&fs_lock);
    return 1;
}

int do_pwd(char* buf){
    if(now_ino == -1)
        return 0;
    if(now_ino == now_superblock->root_ino){
        *buf++ = '/';
        *buf = '\0';
        // printf("/\n");
        return 1;
    }
    acquire(&fs_lock);
    int parent_ino = now_ino;
    int child_ino = now_ino;
    char path[MAX_PATH_LEN];
    path[MAX_PATH_LEN-1] = '\0';
    char* p = path + MAX_PATH_LEN - 2;
    int path_len = 0;
    dentry_t* dentrys = (dentry_t*)get_sector_of_block(inode_mapto_block(now_ino, 0, 0), 0);
    while(parent_ino != now_superblock->root_ino){
        child_ino = parent_ino;
        parent_ino = find_dentry_byname("..", NULL, dentrys, DENTRYS_IN_SECTOR)->inode_num;
        dentrys = (dentry_t*)get_sector_of_block(inode_mapto_block(parent_ino, 0, 0), 0);
        char* name = find_dentry_byino(child_ino, NULL, dentrys, DENTRYS_IN_SECTOR)->name;
        p = p - strlen(name);
        path_len += strlen(name) + 1;
        if(p - path < 1){
            release(&fs_lock);
            return 0;
        }
        char* q = p + 1;
        while(*name != '\0')
            *q++ = *name++;
        *p-- = '/';
    }
    // printf("%s\n", p+1);
    strcpy(buf, p+1);
    release(&fs_lock);
    return 1;
}

int do_cd(char* path){
    if(path==NULL || *path == '\0')//invalid path
        return -1;

    char path_buf[MAX_PATH_LEN];
    int len = strlen(path);
    if(len >= MAX_PATH_LEN){//path too long
        // printf("path too long\n");
        return -1;
    }
    strcpy(path_buf, path);
    path = path_buf;

    int ino;
    acquire(&fs_lock);
    if(*path == '/'){
        ino = walk_by_path(path+1, now_superblock->root_ino);
    }
    else
        ino = walk_by_path(path, now_ino);
    
    inode_t* inode = get_inode(ino);
    int ret;
    if(ino == -1)//no such directory
        ret = 0;
    else if((inode->mode & S_DIR) == 0)//not a directory
        ret = 0;
    else{
        now_ino = ino;
        ret = 1;
    }
    release(&fs_lock);
    return ret;
}

int do_mkdir(char* path){
    if(path==NULL || *path == '\0')//invalid path
        return -1;

    char path_buf[MAX_PATH_LEN];
    int len = strlen(path);
    if(len >= MAX_PATH_LEN){//path too long
        // printf("path too long\n");
        return -1;
    }
    strcpy(path_buf, path);
    path = path_buf;

    acquire(&fs_lock);
    int ino;
    char* name = get_name_and_ino_by_path(path, &ino);

    int ret;
    inode_t* inode = get_inode(ino);
    if(ino == -1)//no such file or directory
        ret = 0;
    else if((inode->mode & S_DIR) == 0)//not a directory
        ret = 0;
    else if(parentino_to_childino(ino, name) != -1)//already exist
        ret = -2;
    else{
        ret = add_dir(ino, name);
    }
    release(&fs_lock);
    return ret;
}

int do_rmdir(char* path){
    if(path == NULL || *path == '\0' || strcmp(path, "..") == 0 || strcmp(path, ".") == 0)//invalid path
        return -1;

    char path_buf[MAX_PATH_LEN];
    int len = strlen(path);
    if(len >= MAX_PATH_LEN){//path too long
        // printf("path too long\n");
        return -1;
    }
    strcpy(path_buf, path);
    path = path_buf;

    acquire(&fs_lock);
    int ino;
    char* name = get_name_and_ino_by_path(path, &ino);
    
    int ret;
    if(ino == -1)//no such file or directory
        ret = 0;
    else{
        ret = del_dir(ino, name);
    }
    release(&fs_lock);
    return ret;
}

int do_ls(char* path, int option){
    int ino;
    if(path!=NULL){
        if(*path == '\0')//invalid path
            return -1;

        char path_buf[MAX_PATH_LEN];
        int len = strlen(path);
        if(len >= MAX_PATH_LEN){//path too long
            // printf("path too long\n");
            return -1;
        }
        strcpy(path_buf, path);
        path = path_buf;
        
        acquire(&fs_lock);
        if(*path == '/'){
            ino = walk_by_path(path+1, now_superblock->root_ino);
        }
        else
            ino = walk_by_path(path, now_ino);
    } else {
        ino = now_ino;
        acquire(&fs_lock);
    }
    int ret;
    inode_t* inode = get_inode(ino);
    if(ino == -1)//no such directory
        ret = 0;
    else if((inode->mode & S_DIR) == 0)//not a directory
        ret = 0;
    else {
        int count = 0;
        for(int i = 0;; i++){
            int block_id = inode_mapto_block(ino, i, 0);
            if(block_id == -1){
                ret = 1;
                break;
            }
            assert(block_id != -1);
            for(int j = 0; j < SECTOR_IN_BLOCK; j++){
                dentry_t* dentrys = (dentry_t*)get_sector_of_block(block_id, j);
                for(int k = 0; k < DENTRYS_IN_SECTOR; k++){
                    if(count >= inode->size){
                        ret = 1;
                        break;
                    }
                    if(dentrys[k].inode_num != -1){
                        count++;
                        if((option & LS_ALL) == 0 && dentrys[k].name[0] == '.') continue;
                        if((option & LS_LONG)){
                            inode_t* child_inode = get_inode(dentrys[k].inode_num);
                            char mode[] = "----";
                            for(int mode_offset = 0; mode_offset < 4; mode_offset++){
                                if(child_inode->mode & (1 << mode_offset))
                                    mode[3-mode_offset] = mode_offset["xwrd"];
                            }
                            int size = child_inode->size;
                            if(mode[0] == 'd')
                                size = 0;
                            printf("%s %5d  %s\n", mode, size, dentrys[k].name);
                        } else {//LS_NORMAL
                            printf("%s\n", dentrys[k].name);
                        }
                    }
                }
                if(count >= inode->size){
                    ret = 1;
                    break;
                }
            }
            if(count >= inode->size){
                ret = 1;
                break;
            }
        }
    }
    release(&fs_lock);
    return ret;
}

fdesc_t fdescs[MAX_FD];

void init_fs(){
    spinlock_init(&fs_lock);
    for(int i = 0; i < MAX_FD; i++){
        fdescs[i].valid = 0;
        fdescs[i].inode_num = -1;
        fdescs[i].offset = 0;
        fdescs[i].occupid_pid = -1;
        fdescs[i].mode = 0;
    }
    fs_cache_init();
}

static fd_t get_free_fd(){
    for(int i = 0; i < MAX_FD; i++){
        if(fdescs[i].valid == 0){
            fdescs[i].valid = 1;
            return i;
        }
    }
    return -1;
}

static void release_fd(fd_t fd){
    assert(fd >= 0 && fd < MAX_FD);
    assert(fdescs[fd].valid == 1);
    fdescs[fd].valid = 0;
}

int do_find(char* path){
    if(path==NULL || *path == '\0')//invalid path
        return -1;
    char path_buf[MAX_PATH_LEN];
    int len = strlen(path);
    if(len >= MAX_PATH_LEN){//path too long
        // printf("path too long\n");
        return -1;
    }
    strcpy(path_buf, path);
    path = path_buf;

    acquire(&fs_lock);
    int ino;
    char* name = get_name_and_ino_by_path(path, &ino);
    
    int ret;
    inode_t* inode = get_inode(ino);
    if(ino == -1)//no such file or directory
        ret = 0;
    else if((inode->mode & S_DIR) == 0)//not a directory
        ret = -1;
    else{
        ret = parentino_to_childino(ino, name);
        if (ret != -1){
            inode_t* child_inode = get_inode(ret);
            if(child_inode->mode & S_DIR)//is a directory
                ret = 2;
            else
                ret = 1;
        } else 
            ret = 0;
    }
    release(&fs_lock);
    return ret;
}

fd_t do_open(char* path, int mode){
    if(path==NULL || *path == '\0')//invalid path
        return -1;

    char path_buf[MAX_PATH_LEN];
    int len = strlen(path);
    if(len >= MAX_PATH_LEN){//path too long
        // printf("path too long\n");
        return -1;
    }
    strcpy(path_buf, path);
    path = path_buf;

    acquire(&fs_lock);
    int ino;
    char* name = get_name_and_ino_by_path(path, &ino);
    
    int ret;
    inode_t* inode = get_inode(ino);
    int child_ino;
    if(ino == -1)//no such file or directory
        ret = -1;
    else if((inode->mode & S_DIR) == 0)//not a directory
        ret = -1;
    else if((child_ino = parentino_to_childino(ino, name)) != -1){//already exist
        ret = child_ino;
        inode_t* child_inode = get_inode(child_ino);
        if(child_inode->mode & S_DIR)//is a directory
            ret = -1;
    } else{
        ret = add_file(ino, name, NULL);
    }
    if(ret >= 0){
        fd_t fd = get_free_fd();
        if(fd == -1){
            ret = -1;
        } else {
            fdescs[fd].inode_num = ret;
            fdescs[fd].offset = 0;
            // fdescs[fd].occupid_pid = current_running->pid;
            fdescs[fd].mode = mode & 0x3;
            ret = fd;
        }
    }
    release(&fs_lock);
    return ret;
}

int do_read(int fd, char *buf, int len){
    acquire(&fs_lock);
    if(fd < 0 || fd >= MAX_FD || fdescs[fd].valid == 0){
        release(&fs_lock);
        return 0;
    }
    fdesc_t* fdesc = &fdescs[fd];
    if(fdesc->mode & O_RDONLY == 0){
        release(&fs_lock);
        return 0;
    }
    int ino = fdesc->inode_num;

    inode_t* inode = get_inode(ino);
    int size = inode->size;
    if(fdesc->offset >= size){
        release(&fs_lock);
        return 0;
    }
    
    int suc_len = len;
    if(fdesc->offset + len > size)
        suc_len = size - fdesc->offset;
    int suc_len_buf = suc_len;
    
    while(suc_len > 0){
        int block_index = fdesc->offset / BLOCK_SIZE;
        int block_offset = fdesc->offset % BLOCK_SIZE;
        int block_id = inode_mapto_block(ino, block_index, 0);
        if(block_id == -1){
            int this_len = (suc_len + block_offset > BLOCK_SIZE)? BLOCK_SIZE - block_offset : suc_len;
            memset(buf, 0, this_len);
            buf += this_len;
            suc_len -= this_len;
            fdesc->offset += this_len;
            continue;
        }
        int sector_index = block_offset / SECTOR_SIZE;
        int sector_offset = block_offset % SECTOR_SIZE;
        while(sector_index < SECTOR_IN_BLOCK && suc_len > 0){
            char* sector_buf = (char *)get_sector_of_block(block_id, sector_index);
            int this_len = (suc_len + sector_offset > SECTOR_SIZE)? SECTOR_SIZE - sector_offset : suc_len;
            memcpy(buf, sector_buf + sector_offset, this_len);
            sector_index++;
            sector_offset = 0;
            buf += this_len;
            suc_len -= this_len;
            fdesc->offset += this_len;
        }
    }
    release(&fs_lock);

    return suc_len_buf - suc_len;
}

int do_write(int fd, char *buf, int len){
    acquire(&fs_lock);
    if(fd < 0 || fd >= MAX_FD || fdescs[fd].valid == 0){
        release(&fs_lock);
        return 0;
    }
    fdesc_t* fdesc = &fdescs[fd];
    if(fdesc->mode & O_WRONLY == 0){
        release(&fs_lock);
        return 0;
    }
    int ino = fdesc->inode_num;

    inode_t* inode = get_inode(ino);
    if(fdesc->offset + len > inode->size){
        int new_size = fdesc->offset + len;
        inode->size = new_size;
        put_inode(ino);
    }

    int suc_len = len;
    int suc_len_buf = suc_len;

    while(suc_len > 0){
        int block_index = fdesc->offset / BLOCK_SIZE;
        int block_offset = fdesc->offset % BLOCK_SIZE;
        int block_id = inode_mapto_block(ino, block_index, 1);
        assert(block_id != -1);
        int sector_index = block_offset / SECTOR_SIZE;
        int sector_offset = block_offset % SECTOR_SIZE;
        while(sector_index < SECTOR_IN_BLOCK && suc_len > 0){
            char* sector_buf = (char*)get_sector_of_block(block_id, sector_index);
            int this_len = (suc_len + sector_offset > SECTOR_SIZE)? SECTOR_SIZE - sector_offset : suc_len;
            memcpy(sector_buf + sector_offset, buf, this_len);
            put_sector_of_block(block_id, sector_index);
            sector_index++;
            sector_offset = 0;
            buf += this_len;
            suc_len -= this_len;
            fdesc->offset += this_len;
        }
    }
    release(&fs_lock);
    return suc_len_buf - suc_len;
}

int do_close(int fd){
    acquire(&fs_lock);
    if(fd < 0 || fd >= MAX_FD || fdescs[fd].valid == 0){
        release(&fs_lock);
        return 0;
    }
    fdesc_t* fdesc = &fdescs[fd];
    // if(fdesc->occupid_pid != current_running->pid){
    //     release(&fs_lock);
    //     return 0;
    // }
    release_fd(fd);
    release(&fs_lock);
    return 1;
}

void fd_check_close(int pid){
    // already hold the pcb lock
    acquire(&fs_lock);
    for(int i = 0; i < MAX_FD; i++){
        if(fdescs[i].valid && fdescs[i].occupid_pid == pid){
            release_fd(i);
        }
    }
    release(&fs_lock);
}
    
int do_lseek(int fd, int offset, int whence){
    acquire(&fs_lock);
    if(fd < 0 || fd >= MAX_FD || fdescs[fd].valid == 0){
        release(&fs_lock);
        return -1;
    }
    fdesc_t* fdesc = &fdescs[fd];
    int ino = fdesc->inode_num;

    int ret = 0;
    inode_t* inode = get_inode(ino);
    int size = inode->size;
    if(whence == SEEK_SET){
        if(offset < 0)
            ret = -1;
        else
            fdesc->offset = offset;
    } else if(whence == SEEK_CUR){
        if(fdesc->offset + offset < 0)
            ret = -1;
        else
            fdesc->offset += offset;
    } else if(whence == SEEK_END){
        if(size + offset < 0)
            ret = -1;
        else
            fdesc->offset = size + offset;
    }
    release(&fs_lock);
    return ret;
}

int do_ln(char* src_path, char* dst_path){
    if(src_path == NULL || *src_path == '\0')//invalid src path
        return -1;
    if(dst_path == NULL || *dst_path == '\0')//invalid dst path
        return -3;

    char src_path_buf[MAX_PATH_LEN];
    int len = strlen(src_path);
    if(len >= MAX_PATH_LEN){//src path too long
        // printf("src path too long\n");
        return -1;
    }
    strcpy(src_path_buf, src_path);
    src_path = src_path_buf;

    char dst_path_buf[MAX_PATH_LEN];
    len = strlen(dst_path);
    if(len >= MAX_PATH_LEN){//dst path too long
        // printf("dst path too long\n");
        return -3;
    }
    strcpy(dst_path_buf, dst_path);
    dst_path = dst_path_buf;

    acquire(&fs_lock);
    int src_ino;
    char* src_name = get_name_and_ino_by_path(src_path, &src_ino);
    int dst_ino;
    char* dst_name = get_name_and_ino_by_path(dst_path, &dst_ino);
    
    int ret;
    inode_t* src_dir_inode = get_inode(src_ino);
    inode_t* dst_dir_inode = get_inode(dst_ino);
    while(1){
        if(src_ino == -1){//no such file or directory
            ret = 0;
            break;
        }
        if((src_dir_inode->mode & S_DIR) == 0){//not a directory
            ret = 0;
            break;
        }
        if(dst_ino == -1){//no such file or directory
            ret = -2;
            break;
        }
        if((dst_dir_inode->mode & S_DIR) == 0){//not a directory
            ret = -2;
            break;
        }
        int src_child_ino = parentino_to_childino(src_ino, src_name);
        if(src_child_ino == -1){//no such file or directory
            ret = 0;
            break;
        }
        inode_t* src_child_inode = get_inode(src_child_ino);
        if(src_child_inode->mode & S_DIR){//is a directory
            ret = -4;
            break;
        }
        if(parentino_to_childino(dst_ino, dst_name) != -1){//already exist
            ret = -5;
            break;
        }
        if(add_file(dst_ino, dst_name, &src_child_ino) == -1)
            ret = -6;
        else
            ret = 1;
        break;
    }
    release(&fs_lock);
    return ret;
}

int do_rmnod(char* path){
    if(path == NULL || *path == '\0' || strcmp(path, "..") == 0 || strcmp(path, ".") == 0)//invalid path
        return -1;

    char path_buf[MAX_PATH_LEN];
    int len = strlen(path);
    if(len >= MAX_PATH_LEN){//path too long
        // printf("path too long\n");
        return -1;
    }
    strcpy(path_buf, path);
    path = path_buf;

    acquire(&fs_lock);
    int ino;
    char* name = get_name_and_ino_by_path(path, &ino);
    
    int ret;
    if(ino == -1)//no such file or directory
        ret = 0;
    else{
        ret = del_file(ino, name);
    }
    release(&fs_lock);
    return ret;
}

int do_rm(char* path){
    int ret1 = do_rmnod(path);
    if(ret1 == 1 || ret1 == 0 || ret1 == -1)
        return ret1;
    int ret2 = do_rmdir(path);
    return ret2;
}
