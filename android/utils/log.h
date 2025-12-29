#ifndef ANDROID_UTILS_LOG_H
#define ANDROID_UTILS_LOG_H

#include <stdint.h>
#include <pthread.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <inttypes.h>

#define MX_QUEUE 64
typedef struct {
    uint64_t data[MX_QUEUE];
    int head;
    int count;
} PcQueue;

#define LOG_BUFFER_SIZE 32

typedef struct {
    uint64_t from_addr;
    uint64_t to_addr;

    uint64_t from_pc;
    uint64_t to_pc;
} TBEdge;

void android_log_init(void);

void pc_queue_push(uint64_t pc);
void tb_log_push(uint64_t from, uint64_t to, uint64_t from_pc, uint64_t to_pc);

#endif
