#include "log.h"

static PcQueue g_pc_queue;

static inline void pc_queue_init(void)
{
    g_pc_queue.head = 0;
    g_pc_queue.count = 0;
}

void pc_queue_push(uint64_t pc)
{
    g_pc_queue.data[g_pc_queue.head] = pc;

    g_pc_queue.head = (g_pc_queue.head + 1) % MX_QUEUE;
    if (g_pc_queue.count < MX_QUEUE) {
        g_pc_queue.count++;
    }
}

const char *filename = "/data/local/tmp/tb_log.txt";

static FILE *log_file = NULL;
static TBEdge log_buffer[LOG_BUFFER_SIZE];
static int buffer_index = 0;
static pthread_mutex_t tb_log_mutex = PTHREAD_MUTEX_INITIALIZER;

static inline void tb_log_init(void)
{
    log_file = fopen(filename, "w");
    if (!log_file) {
        fprintf(stderr, "Failed to open TB log file: %s\n", filename);
    }
}

static void flush_buffer(void)
{
    if (log_file && buffer_index > 0) {
        for (int i = 0; i < buffer_index; i++) {
            fprintf(log_file,
                    "%016" PRIx64 ",%016" PRIx64 ",%016" PRIx64 ",%016" PRIx64
                    "\n",
                    log_buffer[i].from_addr, log_buffer[i].to_addr,
                    log_buffer[i].from_pc, log_buffer[i].to_pc);
        }
        buffer_index = 0;
    }
}

static void tb_log_close(void)
{
    pthread_mutex_lock(&tb_log_mutex);
    flush_buffer();
    if (log_file) {
        fclose(log_file);
        log_file = NULL;
    }
    pthread_mutex_unlock(&tb_log_mutex);
    pthread_mutex_destroy(&tb_log_mutex);
}

void tb_log_push(uint64_t from, uint64_t to, uint64_t from_pc, uint64_t to_pc)
{
    if (!log_file)
        return;

    pthread_mutex_lock(&tb_log_mutex);

    log_buffer[buffer_index].from_addr = from;
    log_buffer[buffer_index].to_addr = to;
    log_buffer[buffer_index].from_pc = from_pc;
    log_buffer[buffer_index].to_pc = to_pc;
    buffer_index++;

    if (buffer_index >= LOG_BUFFER_SIZE) {
        flush_buffer();
    }

    pthread_mutex_unlock(&tb_log_mutex);
}

void android_log_init(void)
{
    pc_queue_init();
    tb_log_init();

    if (atexit(tb_log_close) != 0) {
        fprintf(stderr, "tb_log_close register failed\n");
    }
}
