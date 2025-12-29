#ifndef ANDROID_H
#define ANDROID_H

#include <glib.h>
#include <stdint.h>
#include <stdbool.h>

#include "android/param.h"

extern __thread int jni_call_depth;

typedef struct BerberisCallbacks {
    void (*RunGuestSyscallCallback)(void);
    void (*berberis_RunGeneratedCode)(void *state, uint64_t code);
    void (*handle_signal)(void *state);
    void (*debug_wrap)(void *state, void *pc);
    void (*profile)(void *pc);
    void (*reset_after_fork)(void *berberis_thread);
    int (*RunGuestThreadCallback)(void *parent, void *env);
    void (*ResetAfterQemuFork)(void *env);
    void (*ExitThread)(int status);
    void (*qemu_log)(const char *);
} BerberisCallbacks;

typedef struct AndroidConfig {
    const char *g_exec_path;
} AndroidConfig;

extern AndroidConfig android_config;

extern pthread_mutex_t b2q_mutex;
extern GHashTable *berberis_to_qemu;

extern pthread_mutex_t tb_add_mutex;
extern void *berberis_guest_host;
extern void *berberis_guest_callee;

extern BerberisCallbacks *berberis_cb;

// extern int android_exec_loop(void *state);
// extern int android_cpu_exec_loop(void* state);
extern void android_add_tb(uint64_t guest_pc, uint64_t host_pc, uint64_t arg,
                           bool is_special);

extern void android_jni_run(void *state);


extern void handle_android_syscall(CPUARMState *env);

#define MAP_BITS 12
#define MAP_SIZE (1 << MAP_BITS)

#define MX_QUEUE 64
typedef struct {
    uint64_t data[MX_QUEUE];
    int head; // 指向下一个将被写入的位置
    int count; // 当前队列中有效元素的数量
} CircularQueue;

void push_queue(CircularQueue *q, uint64_t val);
extern CircularQueue q4pc;

#endif
