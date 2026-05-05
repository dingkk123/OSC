#include "allocate.h"
#include "list.h"
#include "uart.h"
#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE (1UL << 12)
#define NUM_POOLS 8 //8 different chunk size 

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

static struct chunk_pool pools[NUM_POOLS]; // fiexed array type of vector<chunk_pool> pools; pools.resize(NUM_POOLS);

static int find_pool_idx(size_t size) { //find the suitable chunk size
    int i;
    for (i = 0; i < NUM_POOLS; i++) {
        if (size <= pool_sizes[i]) {
            return i;
        }
    }
    return -1;
}

static unsigned int size_to_order(size_t size) { //find the suitable chunk size
    size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    unsigned int order = 0;
    size_t n = 1;

    while (n < pages) {
        n <<= 1;
        order++;
    }

    return order;
}

/* address helpers
phys_addr_t idx_to_phys(size_t idx) {
    return buddy_base + idx * PAGE_SIZE;
}

void* page_to_ptr(struct page* pg) {
    return (void*)idx_to_phys((size_t)(pg - mem_map));
}

size_t ptr_to_page_idx(void* ptr) {
    unsigned long addr = (unsigned long)ptr;

    if (addr < buddy_base) {
        return (size_t)-1;
    }

    return (addr - buddy_base) / PAGE_SIZE;
}

struct page* ptr_to_page(void* ptr) {
    size_t idx = ptr_to_page_idx(ptr);
    if (idx == (size_t)-1 || idx >= total_pages) {
        return 0;
    }
    return &mem_map[idx];
}

*/

//log hepler
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

static void log_release_chunk_page(unsigned long addr, size_t chunk_size) {
    uart_puts("[Chunk to page] All chunks returned, release page ");
    uart_hex(addr);
    uart_puts(" of chunk size ");
    uart_put_dec(chunk_size);
    uart_puts("\r\n");
}


// --------------------------------------------------
// chunk init
// --------------------------------------------------

void kmem_init(void) {
    int i;
    for (i = 0; i < NUM_POOLS; i++) {
        pools[i].chunk_size = pool_sizes[i];
        INIT_LIST_HEAD(&pools[i].free_list);
    }
}

// --------------------------------------------------
// alloc & free
// --------------------------------------------------

static void refill_pool(int pool_idx) {
    struct page* pg = alloc_pages(0);
    size_t offset = 0;
    size_t chunk_size;
    void* page_base;

    if (pg == 0) {
        return;
    }

    chunk_size = pools[pool_idx].chunk_size;
    page_base = page_to_ptr(pg); //to get the real address of page base

    pg->alloc_type = 2;
    pg->pool_idx = pool_idx;
    pg->order = 0;
    pg->refcount = 0;
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
            struct page* pg = ptr_to_page((void*)ck);
            pg->refcount++;
            log_alloc_chunk((unsigned long)ck, pools[pool_idx].chunk_size);
            return (void*)ck;
        }
    }

    unsigned int order = size_to_order(size);
    struct page* pg = alloc_pages(order);

    if (pg == 0) {
        return 0;
    }
    
    pg->alloc_type = 1;
    pg->pool_idx = -1;
    
    return page_to_ptr(pg);
}

static void remove_page_with_chunk(struct page* pg){
    int pool_idx = pg->pool_idx;
    size_t chunk_size = pools[pool_idx].chunk_size;
    void* page_base = page_to_ptr(pg);
    size_t offset = 0;

    while(offset + chunk_size <= PAGE_SIZE) {
        struct chunk* ck = (struct chunk*)((char*)page_base + offset);
        if(list_empty(&ck->node) != 1){
            list_del_init(&ck->node);
        }
        offset += chunk_size;
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

        if(pg->refcount > 0){
            pg->refcount--;
        }

        log_free_chunk((unsigned long)ptr, pools[pool_idx].chunk_size);
        
        if(pg->refcount == 0){
            log_release_chunk_page((unsigned long)ptr, pools[pool_idx].chunk_size);
            remove_page_with_chunk(pg);
            pg->alloc_type = 0;
            pg->pool_idx = -1;
            free_pages(pg);
        }         
        return;
    }

    if (pg->alloc_type == 1) {
        pg->alloc_type = 0;
        pg->pool_idx = -1;
        free_pages(pg);
        return;
    }
}
