#ifndef ANDROID_UTILS_LOW_ALLOC_H
#define ANDROID_UTILS_LOW_ALLOC_H

#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

#define ALIGN_UP(addr, align) (((addr) + (align) - 1) & ~((align) - 1))
#define MAPS_BUF_SIZE 16384 

void* find_lowest_addr(size_t n);


#endif
