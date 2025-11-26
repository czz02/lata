/*
 *  qemu user main
 *
 *  Copyright (c) 2003-2008 Fabrice Bellard
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/help-texts.h"
#include "qemu/typedefs.h"
#include "qemu/units.h"
#include "qemu/accel.h"
#include "qemu-version.h"
#include <pthread.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#include <sys/shm.h>
#include <linux/binfmts.h>

#include "qapi/error.h"
#include "qemu.h"
#include "target_cpu.h"
#include "user-internals.h"
#include "qemu/path.h"
#include "qemu/queue.h"
#include "qemu/config-file.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qemu/help_option.h"
#include "qemu/module.h"
#include "qemu/plugin.h"
#include "exec/exec-all.h"
#include "exec/gdbstub.h"
#include "gdbstub/user.h"
#include "tcg/tcg.h"
#include "qemu/timer.h"
#include "qemu/envlist.h"
#include "qemu/guest-random.h"
#include "elf.h"
#include "trace/control.h"
#include "target_elf.h"
#include "cpu_loop-common.h"
#include "crypto/init.h"
#include "fd-trans.h"
#include "signal-common.h"
#include "loader.h"
#include "user-mmap.h"
#include "accel/tcg/perf.h"

#ifdef CONFIG_LATA
#include "target/arm/lata/include/lata.h"
#endif

#ifndef AT_FLAGS_PRESERVE_ARGV0
#define AT_FLAGS_PRESERVE_ARGV0_BIT 0
#define AT_FLAGS_PRESERVE_ARGV0 (1 << AT_FLAGS_PRESERVE_ARGV0_BIT)
#endif

char *exec_path;
char real_exec_path[PATH_MAX];

#ifdef CONFIG_LATA
int indirect_jmp_opt_profile = 0;
#ifdef CONFIG_LATA_INDIRECT_JMP
int option_fam_jmp_cache = 0;
#else
int option_fam_jmp_cache = 0;
#endif
#endif

static bool opt_one_insn_per_tb;
static const char *cpu_model;
static const char *cpu_type;
unsigned long mmap_min_addr;
uintptr_t guest_base;
bool have_guest_base;

/*
 * When running 32-on-64 we should make sure we can fit all of the possible
 * guest address space into a contiguous chunk of virtual host memory.
 *
 * This way we will never overlap with our own libraries or binaries or stack
 * or anything else that QEMU maps.
 *
 * Many cpus reserve the high bit (or more than one for some 64-bit cpus)
 * of the address for the kernel.  Some cpus rely on this and user space
 * uses the high bit(s) for pointer tagging and the like.  For them, we
 * must preserve the expected address space.
 */
#ifndef MAX_RESERVED_VA
#if HOST_LONG_BITS > TARGET_VIRT_ADDR_SPACE_BITS
#if TARGET_VIRT_ADDR_SPACE_BITS == 32 && \
    (TARGET_LONG_BITS == 32 || defined(TARGET_ABI32))
#define MAX_RESERVED_VA(CPU) 0xfffffffful
#else
#define MAX_RESERVED_VA(CPU) ((1ul << TARGET_VIRT_ADDR_SPACE_BITS) - 1)
#endif
#else
#define MAX_RESERVED_VA(CPU) 0
#endif
#endif

unsigned long reserved_va;

// static const char *interp_prefix = CONFIG_QEMU_INTERP_PREFIX;
const char *qemu_uname_release;

#if !defined(TARGET_DEFAULT_STACK_SIZE)
/* XXX: on x86 MAP_GROWSDOWN only works if ESP <= address + 32, so
   we allocate a bigger stack. Need a better solution, for example
   by remapping the process stack directly at the right place */
#define TARGET_DEFAULT_STACK_SIZE 8 * 1024 * 1024UL
#endif

unsigned long guest_stack_size = TARGET_DEFAULT_STACK_SIZE;

/***********************************************************/
/* Helper routines for implementing atomic operations.  */

/* Make sure everything is in a consistent state for calling fork().  */
void fork_start(void)
{
    start_exclusive();
    mmap_fork_start();
    cpu_list_lock();
    qemu_plugin_user_prefork_lock();
}

void fork_end(int child)
{
    qemu_plugin_user_postfork(child);
    mmap_fork_end(child);
    if (child) {
        CPUState *cpu, *next_cpu;
        /* Child processes created by fork() only have a single thread.
           Discard information about the parent threads.  */
        CPU_FOREACH_SAFE (cpu, next_cpu) {
            if (cpu != thread_cpu) {
                QTAILQ_REMOVE_RCU(&cpus, cpu, node);
            }
        }
        qemu_init_cpu_list();
    } else {
        cpu_list_unlock();
    }
    /*
     * qemu_init_cpu_list() reinitialized the child exclusive state, but we
     * also need to keep current_cpu consistent, so call end_exclusive() for
     * both child and parent.
     */
    end_exclusive();
}

__thread CPUState *thread_cpu;

bool qemu_cpu_is_self(CPUState *cpu)
{
    return thread_cpu == cpu;
}

void qemu_cpu_kick(CPUState *cpu)
{
    cpu_exit(cpu);
}

void task_settid(TaskState *ts)
{
    if (ts->ts_tid == 0) {
        ts->ts_tid = (pid_t)syscall(SYS_gettid);
    }
}

void stop_all_tasks(void)
{
    /*
     * We trust that when using NPTL, start_exclusive()
     * handles thread stopping correctly.
     */
    start_exclusive();
}

/* Assumes contents are already zeroed.  */
void init_task_state(TaskState *ts)
{
    long ticks_per_sec;
    struct timespec bt;

    ts->used = 1;
    ts->sigaltstack_used = (struct target_sigaltstack){
        .ss_sp = 0,
        .ss_size = 0,
        .ss_flags = TARGET_SS_DISABLE,
    };

    /* Capture task start time relative to system boot */

    ticks_per_sec = sysconf(_SC_CLK_TCK);

    if ((ticks_per_sec > 0) && !clock_gettime(CLOCK_BOOTTIME, &bt)) {
        /* start_boottime is expressed in clock ticks */
        ts->start_boottime = bt.tv_sec * (uint64_t)ticks_per_sec;
        ts->start_boottime +=
            bt.tv_nsec * (uint64_t)ticks_per_sec / NANOSECONDS_PER_SECOND;
    }
}

#ifdef CONFIG_LATA_INDIRECT_JMP
static void handle_arg_indirect_jmp_opt_profile(const char *arg)
{
    indirect_jmp_opt_profile = 1;
}
#endif

static pthread_mutex_t create_lock = PTHREAD_MUTEX_INITIALIZER;

CPUArchState *cpu_copy(CPUArchState *env)
{
    CPUState *cpu = env_cpu(env);
    CPUState *new_cpu = cpu_create(cpu_type);
    CPUArchState *new_env = new_cpu->env_ptr;

    /* Reset non arch specific state */
    cpu_reset(new_cpu);

    new_cpu->tcg_cflags = cpu->tcg_cflags;
    memcpy(new_env, env, sizeof(CPUArchState));

    return new_env;
}

#include "android.h"

BerberisCallbacks *berberis_cb;
GHashTable *berberis_to_qemu;
void *berberis_guest_host;
void *berberis_guest_callee;
ENV *lsenv_debug;

CPUState *main_cpu;

void *android_create_cpu(void)
{
    pthread_mutex_lock(&create_lock);
    CPUArchState *new_env = cpu_copy(main_cpu->env_ptr);
    CPUState *new_cpu = env_cpu(new_env);
    TaskState *ts = g_new0(TaskState, 1);
    init_task_state(ts);
    pthread_mutex_unlock(&create_lock);

    new_cpu->opaque = ts;

    // thread_cpu = new_cpu;

    return (void *)new_env;
}

static void android_destory_cpu(void *env)
{
}

void android_set_tls(void *env, uint64_t tls)
{
    assert(env);
    CPUArchState *_env = (CPUArchState *)env;
    _env->cp15.tpidr_el[0] = tls;
}

void android_add_state_kv(void *berberis_state, void *env)
{
    CPUArchState *_env = (CPUArchState *)env;
    // printf("pid : %d, berberis_state : %p, qemu_state : %p\n", getpid(),
    // berberis_state, _env);
    pthread_mutex_lock(&b2q_mutex);
    g_hash_table_insert(berberis_to_qemu, berberis_state, env_cpu(_env));
    g_hash_table_insert(berberis_to_qemu, env_cpu(_env), berberis_state);
    pthread_mutex_unlock(&b2q_mutex);
}

// static void init_queue(CircularQueue *q) {
//     q->head = 0;
//     q->count = 0;
// }

// void push_queue(CircularQueue *q, uint64_t val) {
//     q->data[q->head] = val; // 写入数据

//     q->head = (q->head + 1) % MX_QUEUE;
//     if (q->count < MX_QUEUE) {
//         q->count++;
//     }

// }

// CircularQueue q4pc;

extern void *android_init(BerberisCallbacks *cbs);

struct AndroidRuntimeCallbacks {
    void *(*initialize)(BerberisCallbacks *cbs);
    void *(*create_cpu)(void);
    void (*destroy_cpu)(void *);
    void *(*translate)(void *cpu_env, uint64_t guest_pc);
    void (*exec)(void *berberis_state);
    void (*set_reg)(void *cpu_env, int reg_index, uint64_t value);
    uint64_t (*get_reg)(void *cpu_env, int reg_index);
    void (*set_tls)(void *cpu_env, uint64_t tls);
    void (*clone_env)(void *clone_env, void *env);
    void (*add_tb)(uint64_t guest_pc, uint64_t host_pc, uint64_t arg);
    void (*add_state_kv)(void *berberis_state, void *env);
    abi_long (*target_mmap)(abi_ulong start, abi_ulong len, int target_prot,
                            int flags, int fd, off_t offset);
    int (*target_munmap)(abi_ulong start, abi_ulong len);
    const void *epilogue;
} android_runtime_callbacks = {
    &android_init,
    &android_create_cpu,
    &android_destory_cpu,
    NULL,
    &android_jni_run,
    NULL,
    NULL,
    &android_set_tls,
    NULL,
    &android_add_tb,
    &android_add_state_kv,
    &target_mmap,
    &target_munmap,
    NULL,
};

// Config by berberis
AndroidConfig android_config;

void *android_init(BerberisCallbacks *cbs)
{
    assert(cbs);

    // init_queue(&q4pc);

    berberis_cb = cbs;

    module_call_init(MODULE_INIT_TRACE);
    qemu_init_cpu_list();
    module_call_init(MODULE_INIT_QOM);

    cpu_model = cpu_get_model(0);
    cpu_type = parse_cpu_option(cpu_model);

    /* init tcg before creating CPUs and to get qemu_host_page_size */
    {
        AccelState *accel = current_accel();
        AccelClass *ac = ACCEL_GET_CLASS(accel);

        accel_init_interfaces(ac);
        object_property_set_bool(OBJECT(accel), "one-insn-per-tb",
                                 opt_one_insn_per_tb, &error_abort);
        ac->init_machine(NULL);
    }

    CPUState *cpu = cpu_create(cpu_type);
    cpu_reset(cpu);
    main_cpu = cpu;

    TaskState *ts = g_new0(TaskState, 1);
    init_task_state(ts);
    main_cpu->opaque = ts;
    thread_cpu = main_cpu;

    if (TARGET_ARCH_HAS_SIGTRAMP_PAGE) {
        abi_long tramp_page =
            target_mmap(0, TARGET_PAGE_SIZE, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANON, -1, 0);
        if (tramp_page == -1) {
            assert(0);
        }

        setup_sigtramp(tramp_page);
        target_mprotect(tramp_page, TARGET_PAGE_SIZE, PROT_READ | PROT_EXEC);
    }


    syscall_init();
    signal_init();

    fd_trans_init();

#ifdef CONFIG_LATA_INDIRECT_JMP
    lata_fast_jmp_cache_init(cpu->env_ptr, 0, 0);
#endif

#ifdef CONFIG_LATA
    lata_tr_data_init(cpu->env_ptr);
    cpu->env_ptr->pstate = 0x40000000;
    cpu->env_ptr->jr_cnt = 0;
    cpu->env_ptr->jr_hit = 0;
    lata_prologue_init(tcg_ctx);
#else
    tcg_prologue_init(tcg_ctx);
#endif


    berberis_to_qemu = g_hash_table_new(g_direct_hash, g_direct_equal);
    berberis_guest_host = g_hash_table_new(g_direct_hash, g_direct_equal);
    berberis_guest_callee = g_hash_table_new(g_direct_hash, g_direct_equal);

    return main_cpu->env_ptr;
}
