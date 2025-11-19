#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef CONFIG_ANDROID

#include <android/log.h>

#define LOG_TAG "LATA"

#define android_assert assert

// #define android_assert(condition)                                                 \
//     do {                                                                          \
//         if (!(condition)) {                                                       \
//             __android_log_print(                                                  \
//                 ANDROID_LOG_FATAL, LOG_TAG,                                       \
//                 "Assertion failed: '%s', in file %s, line %d, function: %s",      \
//                 #condition, __FILE__, __LINE__, __func__);                        \
//             abort();                                                              \
//         }                                                                         \
//     } while (0)

#endif

#ifdef CONFIG_LATA
#define lsassert(cond)                                                  \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr,                                             \
                    "\033[31m assertion failed in <%s> %s:%d \033[m\n", \
                    __FUNCTION__, __FILE__, __LINE__);                  \
            abort();                                                    \
        }                                                               \
    } while (0)

#define lsassertm(cond, ...)                                                  \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "\033[31m assertion failed in <%s> %s:%d \033[m", \
                    __FUNCTION__, __FILE__, __LINE__);                        \
            fprintf(stderr, __VA_ARGS__);                                     \
            abort();                                                          \
        }                                                                     \
    } while (0)

#else
#define lsassert(cond)          ((void)0)
#define lsassertm(cond, ...)    ((void)0)
#endif
