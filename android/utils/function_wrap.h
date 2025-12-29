#ifndef ANDROID_UTILS_WRAP_H
#define ANDROID_UTILS_WRAP_H

#include <glib.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

typedef struct {
    uint64_t host_pc;
    uint64_t callee;
    char *name;
    bool is_special;
} WrapItem;

#define BLOCK_SIZE 512

typedef struct {
    WrapItem *data_block;
    int current_index;
    GList *blocks;
} WrapPool;

void func_wrap_init(void);

void func_wrap_add(uint64_t key, uint64_t host_pc, uint64_t callee,
                   bool is_special);
WrapItem *wrap_query(uint64_t key);

void wrap_name_add(const char *name, uint64_t func_addr);
uint64_t wrap_name_query(const char *name);

#endif
