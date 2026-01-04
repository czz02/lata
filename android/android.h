#ifndef ANDROID_H
#define ANDROID_H

#include <glib.h>
#include <stdint.h>
#include <stdbool.h>

#include "param.h"
#include "android_if.h"
#include "utils/function_wrap.h"
#include "utils/log.h"

extern __thread int jni_call_depth;

extern pthread_mutex_t b2q_mutex;
extern GHashTable *berberis_to_qemu;

extern BerberisCallbacks *berberis_cb;

typedef struct AndroidConfig {
    const char *g_exec_path;
} AndroidConfig;

extern AndroidConfig android_config;

#endif
