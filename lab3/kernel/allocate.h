#ifndef ALLOCATE_H
#define ALLOCATE_H

#include <stddef.h>
#include "list.h"
#define MAX_ALLOC_SIZE 2147483647UL

typedef unsigned long phys_addr_t;

struct page {
    int order;
    int refcount;
    int is_free;
    int alloc_type;   // 0=free/unused, 1=page allocation, 2=chunk page
    int pool_idx;     // chunk page 屬於哪個 pool，沒有就 -1
    struct list_head node;
};

void buddy_init(struct page *page_array, size_t num_pages, phys_addr_t base);
void buddy_add_region(phys_addr_t base, size_t size);
void memory_reserve(phys_addr_t base, size_t size);

struct page* alloc_pages(unsigned int order);
void free_pages(struct page* page);

/* kmalloc */
void kmem_init(void);
void* allocate(size_t size);
void free(void* ptr);

/* helpers shared with kmalloc.c */
void* page_to_ptr(struct page* pg);
size_t ptr_to_page_idx(void* ptr);
struct page* ptr_to_page(void* ptr);
phys_addr_t idx_to_phys(size_t idx);

void dump(void);
void test_buddy(void);
void wait_enter(void);

#endif
