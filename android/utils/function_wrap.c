#include "function_wrap.h"

static inline WrapPool *wrap_pool_new(void)
{
    WrapPool *pool = g_new0(WrapPool, 1);
    pool->data_block = g_new(WrapItem, BLOCK_SIZE);
    pool->current_index = 0;
    return pool;
}

static inline WrapItem *wrap_pool_alloc(WrapPool *pool)
{
    if (pool->current_index >= BLOCK_SIZE) {
        pool->blocks = g_list_append(pool->blocks, pool->data_block);
        pool->data_block = g_new(WrapItem, BLOCK_SIZE);
        pool->current_index = 0;
    }
    return &pool->data_block[pool->current_index++];
}

static WrapPool *g_wrap_pool;
static GHashTable *g_wrap_table; // guest_plt_pc, guest_pc => (host_pc, callee)
static GHashTable *g_name_table; // func_name => special_func addr
static pthread_mutex_t wrap_mutex = PTHREAD_MUTEX_INITIALIZER;

void func_wrap_init(void)
{
    g_wrap_pool = wrap_pool_new();
    g_wrap_table = g_hash_table_new(g_direct_hash, g_direct_equal);
    g_name_table =
        g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
}

void func_wrap_add(uint64_t key, uint64_t host_pc, uint64_t callee,
                   bool is_special)
{
    WrapItem *it = wrap_pool_alloc(g_wrap_pool);

    it->host_pc = host_pc;
    it->callee = callee;
    it->is_special = is_special;

    pthread_mutex_lock(&wrap_mutex);
    g_hash_table_insert(g_wrap_table, (gpointer)key, it);
    pthread_mutex_unlock(&wrap_mutex);
}

void wrap_name_add(const char *name, uint64_t func_addr)
{
    g_hash_table_insert(g_name_table, g_strdup(name), (gpointer)func_addr);
}

uint64_t wrap_name_query(const char *name)
{
    return (uint64_t)g_hash_table_lookup(g_name_table, name);
}

WrapItem *wrap_query(uint64_t key)
{
    return (WrapItem *)g_hash_table_lookup(g_wrap_table, (gpointer)key);
}
