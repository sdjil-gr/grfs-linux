#ifndef IO_H
#define IO_H

#define IMAGE_SIZE 512*1024*1024 // 512MB

#define MAX_SECTORS (IMAGE_SIZE/512)

#define IMAGE_PATH "image"

void init_io();
void release_io();
void bios_sd_read(unsigned long buf_addr, unsigned num_of_sectors, unsigned start_sector_id);
void bios_sd_write(unsigned long buf_addr, unsigned num_of_sectors, unsigned start_sector_id);

#endif /* IO_H */