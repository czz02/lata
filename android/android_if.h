#ifndef ANDROID_IF_H
#define ANDROID_IF_H

#include <stdint.h>

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

#endif
