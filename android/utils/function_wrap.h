#ifndef ANDROID_UTILS_WRAP_H
#define ANDROID_UTILS_WRAP_H

#include <glib.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

typedef struct {
    uint64_t host_pc;
    uint64_t callee;
    bool is_spcial;
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
WrapItem *func_wrap_query(uint64_t key);

#endif
