#ifndef GDFS_H
#define GDFS_H

#include "type.h"
#include "spinlock.h"

#define FILE_SYSTEM_SIZE 512*1024*1024// 512MB

#define SECTOR_SIZE 512
#define BLOCK_SIZE 4096

#define SECTOR_BIT_SIZE (SECTOR_SIZE * 8)
#define SECTOR_IN_BLOCK (BLOCK_SIZE / SECTOR_SIZE)

#define MAX_SECTOR_NUM (FILE_SYSTEM_SIZE/SECTOR_SIZE)
#define MAX_BLOCK_NUM (FILE_SYSTEM_SIZE/BLOCK_SIZE)

#define SECTORID2BLOCKID(sector_id) ((sector_id) * SECTOR_SIZE / BLOCK_SIZE)
#define BLOCKID2SECTORID(block_id) ((block_id) * BLOCK_SIZE / SECTOR_SIZE)

#define MAX_PATH_LEN 256

/* macros of file system */
#define FILE_SYSTEM_BEGIN_SECTOR 0

// | superblock | reserves |  blockmap | inodemap |inode table | data_blocks | 
// 0            1          8           40         41          72            1024*1024

#define SUPERBLOCK_MAGIC 0xDF4C4459
#define FILE_SYSTEM_NAME "grfs"
#define SUPERBLOCK_BEGIN_SECTOR 0


#define BLOCKMAP_BEGIN_SECTOR 8
#define BLOCKMAP_OCCUPIED_SECTORS 32


#define INODEMAP_BEGIN_SECTOR 40
#define INODEMAP_OCCUPIED_SECTORS 1

#define INODE_TABLE_BEGIN_SECTOR 41
#define INODE_TABLE_OCCUPIED_SECTORS 31
#define INODE_SIZE 64
#define INODE_MAX_NUM (INODE_TABLE_OCCUPIED_SECTORS * SECTOR_SIZE / INODE_SIZE)

#define DENTRY_SIZE 32

#define BLOCK_TABLE_BEGIN_SECTOR 72
#define BLOCK_TABLE_OCCUPIED_SECTORS (MAX_SECTOR_NUM - BLOCK_TABLE_BEGIN_SECTOR)
#define BLOCK_SIZE 4096
#define BLOCK_MAX_NUM (BLOCK_TABLE_OCCUPIED_SECTORS * SECTOR_SIZE / BLOCK_SIZE)

#define INODES_IN_SECTOR (SECTOR_SIZE / INODE_SIZE)
#define DENTRYS_IN_SECTOR (SECTOR_SIZE / DENTRY_SIZE)
#define DENTRYS_IN_BLOCK (BLOCK_SIZE / DENTRY_SIZE)

/* data structures of file system */
typedef struct  __attribute__((aligned(SECTOR_SIZE))) superblock {
    uint32_t magic;
    uint32_t begin_sector;
    uint32_t superblock_sector;
    uint32_t total_sectors;
    uint32_t root_ino;
    char name[32];

    uint32_t inodemap_begin_sector;
    uint32_t inodemap_occupied_sectors;

    uint32_t blockmap_begin_sector;
    uint32_t blockmap_occupied_sectors;

    uint32_t inode_table_begin_sector;
    uint32_t inode_table_occupied_sectors;
    uint32_t inode_size;
    uint32_t inode_num;
    uint32_t inode_max_num;

    uint32_t dentry_size;

    uint32_t block_table_begin_sector;
    uint32_t block_table_occupied_sectors;
    uint32_t block_size;
    uint32_t block_num;
    uint32_t block_max_num;
} superblock_t;

typedef struct dentry {
    char name[28];
    uint32_t inode_num;
} dentry_t;

#define INODE_DIRECT_BLOCK 10
#define INODE_INDIRECT1_BLOCK (BLOCK_SIZE / 4)
#define INODE_INDIRECT2_BLOCK ((BLOCK_SIZE / 4) * (BLOCK_SIZE / 4))
#define INODE_INDIRECT3_BLOCK ((BLOCK_SIZE / 4) * (BLOCK_SIZE / 4) * (BLOCK_SIZE / 4))

typedef struct inode { 
    uint32_t mode;
    uint32_t nlinks;
    uint32_t size;
    // uint32_t ctime;
    // uint32_t atime;
    // uint32_t mtime;
    uint32_t block_ptr[INODE_DIRECT_BLOCK];
    uint32_t indirect1_ptr;
    uint32_t indirect2_ptr;
    uint32_t indirect3_ptr;
} inode_t;

/* modes of inode */
#define S_EXEC 0x1  /* executable */
#define S_WRITE 0x2  /* writeable */
#define S_READ 0x4  /* readable */
#define S_DIR 0x8  /* directory */

typedef struct fdesc {
    uint32_t valid;
    uint32_t inode_num;
    uint32_t offset;
    uint16_t mode;
    uint16_t occupid_pid;
} fdesc_t;

#define MAX_FD 32
typedef uint32_t fd_t;

/* modes of do_open */
#define O_RDONLY 1  /* read only open */
#define O_WRONLY 2  /* write only open */
#define O_RDWR   3  /* read/write open */

/* whence of do_lseek */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

extern superblock_t* now_superblock;
extern int now_ino;

/* fs function declarations */

/**
 * @brief initialize the file system
 */
void init_fs(void);

/**
 * @brief make a new file system
 * @return the finish status of mkfs
 * @retval 1 success
 * @retval 0 fail
 */
int do_mkfs(void);

/**
 * @brief display the file system information
 * @return the finish status of statfs
 * @retval 1 success
 * @retval 0 fail
 */
int do_statfs(void);

/**
 * @brief display the current working directory
 * @param buffer the buffer to store the result(must be atleast MAX_PATH_LEN bytes long)
 * @return the finish status of pwd
 * @retval 1 success
 * @retval 0 fail
 */
int do_pwd(char* buffer);

/**
 * @brief cd to a directory
 * @param path the path of the new directory
 * @return the finish status of cd
 * @retval  1 success
 * @retval  0 no such directory
 * @retval -1 invalid path
 */
int do_cd(char *path);

/**
 * @brief create a new directory
 * @param path the path of the new directory
 * @return the finish status of mkdir
 * @retval  1 success
 * @retval  0 no such directory
 * @retval -1 invalid path
 * @retval -2 already exist
 */
int do_mkdir(char *path);

/**
 * @brief remove a directory
 * @param path the path of the directory to be removed
 * @return the finish status of rmdir
 * @retval  1 success
 * @retval  0 no such directory
 * @retval -1 invalid path
 * @retval -2 cannot remove (is not empty or root)
 */
int do_rmdir(char *path);

#define LS_NORMAL 0x00
#define LS_LONG 0x01
#define LS_ALL 0x02
/**
 * @brief list the contents of a directory
 * @param path the path of the directory to be listed
 * @param option the option of ls, LS_NORMAL, LS_LONG for -l, LS_ALL for -a
 * @return the finish status of ls
 * @retval  1 success
 * @retval  0 no such directory
 * @retval -1 invalid path
 */
int do_ls(char *path, int option);

/**
 * @brief find a file or directory
 * @param path the path of the file or directory to be found
 * @return the finish status of find
 * @retval  2 success (directory found)
 * @retval  1 success (file found)
 * @retval  0 no such file or directory
 * @retval -1 invalid path
 */
int do_find(char *path);

/**
 * @brief open a file
 * @param path the path of the file to be opened
 * @param mode the mode of the file, O_RDONLY, O_WRONLY, O_RDWR
 * @return the fd of the file
 * @retval  -1 fail
 * @retval  >=0 success
 */
fd_t do_open(char *path, int mode);

/**
 * @brief read from a file
 * @param fd the fd of the file to be read
 * @param buff the buffer to store the result
 * @param length the length of the data to be read
 * @return success read length
 */
int do_read(int fd, char *buff, int length);

/**
 * @brief write to a file
 * @param fd the fd of the file to be written
 * @param buff the buffer to be written
 * @param length the length of the data to be written
 * @return success write length
 */
int do_write(int fd, char *buff, int length);

/**
 * @brief close a file
 * @param fd the fd of the file to be closed
 * @return the finish status of close
 * @retval  1 success
 * @retval  0 fail
 */
int do_close(int fd);

/**
 * @brief close all file descriptor of a process
 * @param pid the pid of the process
 */
void fd_check_close(int pid);

/**
 * @brief change the position of the file read/write pointer
 * @param fd the fd of the file to be seeked
 * @param offset the offset of the pointer
 * @param whence the position of the pointer, SEEK_SET, SEEK_CUR, SEEK_END
 * @retval 0 success
 * @retval !=0 fail
 */
int do_lseek(int fd, int offset, int whence);

/**
 * @brief link a file
 * @param src_path the path of the source file
 * @param dst_path the path of the destination file
 * @return the finish status of ln
 * @retval  1 success
 * @retval  0 no such file or directory(src)
 * @retval -1 invalid path(src)
 * @retval -2 no such file or directory(dst)
 * @retval -3 invalid path(dst)
 * @retval -4 is a directory(src)
 * @retval -5 already exist(dst)
 * @retval -6 can not link
 */
int do_ln(char *src_path, char *dst_path);


/**
 * @brief remove a file
 * @param path the path of the file to be removed
 * @return the finish status of rm
 * @retval  1 success
 * @retval  0 no such file or directory
 * @retval -1 invalid path
 * @retval -2 is a directory
 */
int do_rmnod(char *path);

/**
 * @brief remove a directory or a file
 * @param path the path of the directory to be removed
 * @return the finish status of rm
 * @retval  1 success
 * @retval  0 no such file or directory
 * @retval -1 invalid path
 * @retval -2 cannot remove (is not empty or root)
 */
int do_rm(char *path);


#endif /* GDFS_H */