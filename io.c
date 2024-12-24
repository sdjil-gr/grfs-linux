#include <stdio.h>
#include <assert.h>
#include "io.h"


FILE *img;

void init_io(){
    img = fopen(IMAGE_PATH, "rb+");
}

void release_io(){
    fclose(img);
}

void bios_sd_read(unsigned long buf_addr, unsigned num_of_sectors, unsigned start_sector_id) {
    assert(start_sector_id<MAX_SECTORS);
    fseek(img, start_sector_id*512, SEEK_SET);
    fread((void*)buf_addr, 512, num_of_sectors, img);
}

void bios_sd_write(unsigned long buf_addr, unsigned num_of_sectors, unsigned start_sector_id) {
    assert(start_sector_id<MAX_SECTORS);
    fseek(img, start_sector_id*512, SEEK_SET);
    fwrite((void*)buf_addr, 512, num_of_sectors, img);
}