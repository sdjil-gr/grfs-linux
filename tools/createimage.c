#include <stdio.h>

#define IMAGE_SIZE 512*1024*1024 // 512MB

#define MAX_SECTORS (IMAGE_SIZE/512)

#define IMAGE_PATH "image"

FILE *img;

void main(){
    img = fopen(IMAGE_PATH, "w+");
    fseek(img, IMAGE_SIZE-1, SEEK_SET);
    fputc(0, img);
    fclose(img);
}