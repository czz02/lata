#include "low_alloc.h"

static const size_t g_page_size = 4096;
static const uintptr_t g_mmap_min_addr = 0x10000;

static uintptr_t quick_atoh(char **ptr) {
    uintptr_t val = 0;
    char *c = *ptr;
    while (*c) {
        uint8_t byte = (uint8_t)*c;
        if (byte >= '0' && byte <= '9') val = (val << 4) | (byte - '0');
        else if (byte >= 'a' && byte <= 'f') val = (val << 4) | (byte - 'a' + 10);
        else if (byte >= 'A' && byte <= 'F') val = (val << 4) | (byte - 'A' + 10);
        else break;
        c++;
    }
    *ptr = c;
    return val;
}

void *find_lowest_addr(size_t size)
{
    int fd;
    char buf[MAPS_BUF_SIZE];
    ssize_t nread;
    char *p;
    unsigned long start;
    unsigned long aligned_size;
    unsigned long reg_start, reg_end;
    
    if (!size)
        return NULL;
        
    fd = open("/proc/self/maps", O_RDONLY);
    if (fd < 0)
        return NULL;
        
    start = g_mmap_min_addr;
    aligned_size = ALIGN_UP(size, g_page_size);
    
    while ((nread = read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[nread] = '\0';
        p = buf;
        
        while (*p) {
            reg_start = quick_atoh(&p);
            if (*p != '-')
                goto next_line;
            p++;
            reg_end = quick_atoh(&p);
            
            if (reg_start > start) {
                if (reg_start - start >= aligned_size) {
                    close(fd);
                    return (void *)start;
                }
            }
            
            if (reg_end > start)
                start = ALIGN_UP(reg_end, g_page_size);
                
next_line:
            while (*p && *p != '\n')
                p++;
            if (*p == '\n')
                p++;
        }
    }
    
    close(fd);
    return (void *)start;
}
