#include "allocate.h"
#include "list.h"
#include "uart.h"
#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE (1UL << 12)
#define NUM_POOLS 8

struct chunk {
    struct list_head node;
};

struct chunk_pool {
    size_t chunk_size;
    struct list_head free_list;
};

static size_t pool_sizes[NUM_POOLS] = {
    16, 32, 64, 128, 256, 512, 1024, 2048
};

static struct chunk_pool pools[NUM_POOLS];

static int find_pool_idx(size_t size) {
    int i;
    for (i = 0; i < NUM_POOLS; i++) {
        if (size <= pool_sizes[i]) {
            return i;
        }
    }
    return -1;
}

static unsigned int size_to_order(size_t size) {
    size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    unsigned int order = 0;
    size_t n = 1;

    while (n < pages) {
        n <<= 1;
        order++;
    }

    return order;
}

static void log_alloc_chunk(unsigned long addr, size_t chunk_size) {
    uart_puts("[Chunk] Allocate ");
    uart_hex(addr);
    uart_puts(" at chunk size ");
    uart_put_dec(chunk_size);
    uart_puts("\r\n");
}

static void log_free_chunk(unsigned long addr, size_t chunk_size) {
    uart_puts("[Chunk] Free ");
    uart_hex(addr);
    uart_puts(" at chunk size ");
    uart_put_dec(chunk_size);
    uart_puts("\r\n");
}

void kmem_init(void) {
    int i;
    for (i = 0; i < NUM_POOLS; i++) {
        pools[i].chunk_size = pool_sizes[i];
        INIT_LIST_HEAD(&pools[i].free_list);
    }
}

static void refill_pool(int pool_idx) {
    struct page* pg = alloc_pages(0);
    size_t offset = 0;
    size_t chunk_size;
    void* page_base;

    if (pg == 0) {
        return;
    }

    chunk_size = pools[pool_idx].chunk_size;
    page_base = page_to_ptr(pg);

    pg->alloc_type = 2;
    pg->pool_idx = pool_idx;
    pg->order = 0;
    pg->refcount = 1;
    pg->is_free = 0;

    while (offset + chunk_size <= PAGE_SIZE) {
        struct chunk* ck = (struct chunk*)((char*)page_base + offset);
        INIT_LIST_HEAD(&ck->node);
        list_add_tail(&ck->node, &pools[pool_idx].free_list);
        offset += chunk_size;
    }
}

void* allocate(size_t size) {
    if (size == 0) {
        return 0;
    }
    if (size > MAX_ALLOC_SIZE)
        return 0;

    if (size < PAGE_SIZE) {
        int pool_idx = find_pool_idx(size);

        if (pool_idx >= 0) {
            if (list_empty(&pools[pool_idx].free_list)) {
                refill_pool(pool_idx);
            }

            if (list_empty(&pools[pool_idx].free_list)) {
                return 0;
            }

            struct list_head* node = pools[pool_idx].free_list.next;
            list_del_init(node);

            struct chunk* ck = (struct chunk*)node;
            log_alloc_chunk((unsigned long)ck, pools[pool_idx].chunk_size);
            return (void*)ck;
        }
    }

    {
        unsigned int order = size_to_order(size);
        struct page* pg = alloc_pages(order);

        if (pg == 0) {
            return 0;
        }

        pg->alloc_type = 1;
        pg->pool_idx = -1;
        return page_to_ptr(pg);
    }
}

void free(void* ptr) {
    struct page* pg;

    if (ptr == 0) {
        return;
    }

    pg = ptr_to_page(ptr);
    if (pg == 0) {
        return;
    }

    if (pg->alloc_type == 2) {
        int pool_idx = pg->pool_idx;
        struct chunk* ck = (struct chunk*)ptr;

        INIT_LIST_HEAD(&ck->node);
        list_add_tail(&ck->node, &pools[pool_idx].free_list);
        log_free_chunk((unsigned long)ptr, pools[pool_idx].chunk_size);
        return;
    }

    if (pg->alloc_type == 1) {
        pg->alloc_type = 0;
        pg->pool_idx = -1;
        free_pages(pg);
        return;
    }
}
