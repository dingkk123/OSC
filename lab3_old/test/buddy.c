#include "allocate.h"
#include "uart.h"
#include "list.h"
#include <stddef.h>
#include <stdint.h>

#define NUM_PAGES 0x280000
#define MAX_ORDER 10
#define PAGE_SIZE (1UL << 12)

typedef unsigned long phys_addr_t;

static struct page mem_map[NUM_PAGES];
static struct list_head free_area[MAX_ORDER + 1];
static phys_addr_t buddy_base = 0;
// --------------------------------------------------
// address/index helpers
// --------------------------------------------------

// ===== CHANGED: 新增 helper，之後 base/index 換算都統一走這裡 =====
static size_t page_to_idx(struct page* pg) {
    return (size_t)(pg - mem_map);
}

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
    if (idx == (size_t)-1 || idx >= NUM_PAGES) {
        return 0;
    }
    return &mem_map[idx];
}

static void log_block_range(size_t idx, unsigned int order) {
    uart_puts("Range of pages: [");
    uart_put_dec(idx);
    uart_puts(", ");
    uart_put_dec(idx + (1U << order) - 1);
    uart_puts("]");
}

static void log_add_block(size_t idx, unsigned int order) {
    uart_puts("[+] Add page ");
    uart_put_dec(idx);
    uart_puts(" to order ");
    uart_put_dec(order);
    uart_puts(". ");
    log_block_range(idx, order);
    uart_puts("\r\n");
}

static void log_remove_block(size_t idx, unsigned int order) {
    uart_puts("[-] Remove page ");
    uart_put_dec(idx);
    uart_puts(" from order ");
    uart_put_dec(order);
    uart_puts(". ");
    log_block_range(idx, order);
    uart_puts("\r\n");
}

static void log_split_block(size_t left_idx, size_t right_idx, unsigned int order) {
    uart_puts("[Split] order ");
    uart_put_dec(order + 1);
    uart_puts(" -> two order ");
    uart_put_dec(order);
    uart_puts(" blocks: left=");
    uart_put_dec(left_idx);
    uart_puts(", right=");
    uart_put_dec(right_idx);
    uart_puts("\r\n");
}

static void log_alloc_page(size_t idx, unsigned int order) {
    uart_puts("[Page] Allocate ");
    uart_hex(idx_to_phys(idx));
    uart_puts(" at order ");
    uart_put_dec(order);
    uart_puts(", page ");
    uart_put_dec(idx);
    uart_puts("\r\n");
}

static void log_free_page(size_t idx, unsigned int order) {
    uart_puts("[Page] Free ");
    uart_hex(idx_to_phys(idx));
    uart_puts(" at order ");
    uart_put_dec(order);
    uart_puts(", page ");
    uart_put_dec(idx);
    uart_puts("\r\n");
}

static void log_buddy_found(size_t idx, size_t buddy_idx, unsigned int order) {
    uart_puts("[*] Buddy found! buddy idx: ");
    uart_put_dec(buddy_idx);
    uart_puts(" for page ");
    uart_put_dec(idx);
    uart_puts(" with order ");
    uart_put_dec(order);
    uart_puts("\r\n");
}

static void log_merge_block(size_t idx, unsigned int new_order) {
    uart_puts("[Merge] merged to page ");
    uart_put_dec(idx);
    uart_puts(" order ");
    uart_put_dec(new_order);
    uart_puts("\r\n");
}

static void log_reserve(phys_addr_t base, size_t size, size_t start_idx, size_t end_idx) {
    uart_puts("[Reserve] Reserve address [");
    uart_hex(base);
    uart_puts(", ");
    uart_hex(base + size);
    uart_puts("). Range of pages: [");
    uart_put_dec(start_idx);
    uart_puts(", ");
    uart_put_dec(end_idx);
    uart_puts(")\r\n");
}

struct page* node_to_page(struct list_head* list_node) {
    char* addr = (char*)list_node;
    size_t off = offsetof(struct page, node);
    return (struct page*)(addr - off);
}

// --------------------------------------------------
// buddy helpers
// --------------------------------------------------

struct page* get_buddy(struct page* page, unsigned int order) {
    // 這裡先保留你原本邏輯，不大改
    size_t idx = page_to_idx(page);
    size_t block_size = 1U << order;
    size_t buddy_idx;

    if ((idx / block_size) % 2 == 0) {
        buddy_idx = idx + block_size;
    } else {
        buddy_idx = idx - block_size;
    }

    if (buddy_idx >= NUM_PAGES)
        return 0;

    return &mem_map[buddy_idx];
}

int count_free_list(int order) {
    int cnt = 0;
    struct list_head* head = &free_area[order];
    struct list_head* cur = head->next;
    while (cur != head) {
        cnt++;
        cur = cur->next;
    }
    return cnt;
}

// --------------------------------------------------
// alloc / free
// --------------------------------------------------

struct page* alloc_pages(unsigned int order) {
    if (order > MAX_ORDER) {
        return 0;
    }

    unsigned int cur_order = order;
    while (cur_order <= MAX_ORDER && list_empty(&free_area[cur_order])) {
        cur_order++;
    }

    if (cur_order > MAX_ORDER) {
        return 0;
    }

    struct list_head* node = free_area[cur_order].next;
    struct page* pg = node_to_page(node);
    log_remove_block(page_to_idx(pg), cur_order);
    list_del_init(node);

    pg->is_free = 0;
    pg->refcount = 1;

    while (cur_order > order) {
        cur_order--;

        // ===== CHANGED: mem_map.data() -> mem_map =====
        size_t pg_idx = page_to_idx(pg);
        size_t buddy_idx = pg_idx + (1U << cur_order);
        
        struct page* buddy = &mem_map[buddy_idx];
        buddy->order = cur_order;
        buddy->is_free = 1;
        buddy->refcount = 0;
        INIT_LIST_HEAD(&buddy->node);
        list_add(&buddy->node, &free_area[cur_order]);
        log_split_block(pg_idx, buddy_idx, cur_order);
        log_add_block(buddy_idx, cur_order);
        pg->order = cur_order;
    }

    pg->order = order;
    pg->is_free = 0;
    pg->refcount = 1;
    log_alloc_page(page_to_idx(pg), order);
    return pg;
}

void free_pages(struct page* page) {
    if (page == 0)
        return;

    log_free_page(page_to_idx(page), page->order);
    page->refcount = 0;
    page->is_free = 1;
    
    while (page->order < MAX_ORDER) {
        struct page* buddy = get_buddy(page, page->order);
        if (buddy == 0 || buddy->is_free == 0 || buddy->order != page->order) {
            break;
        }
        log_buddy_found(page_to_idx(page), page_to_idx(buddy), page->order);
        log_remove_block(page_to_idx(buddy), buddy->order);
        list_del_init(&buddy->node);
        buddy->is_free = 0;

        if (buddy < page) {
            page = buddy;
        }

        page->order++;
        page->is_free = 1;
        page->refcount = 0;
        log_merge_block(page_to_idx(page), page->order);
    }

    INIT_LIST_HEAD(&page->node);
    list_add_tail(&page->node, &free_area[page->order]);
    log_add_block(page_to_idx(page), page->order);
}

// --------------------------------------------------
// reserve
// --------------------------------------------------

void memory_reserve(phys_addr_t base, size_t size) {
    // ===== CHANGED: 改成用 buddy_base 算 index，不再直接 base / PAGE_SIZE =====
    if (size == 0) return;
    if (base < buddy_base) return;

    size_t start_idx = (base - buddy_base) / PAGE_SIZE;
    size_t end_idx = (base - buddy_base + size + PAGE_SIZE - 1) / PAGE_SIZE;

    if (start_idx >= NUM_PAGES) return;
    if (end_idx > NUM_PAGES) end_idx = NUM_PAGES;
    log_reserve(base, size, start_idx, end_idx);

    for (int order = MAX_ORDER; order >= 0; order--) {
        size_t block_pages_num = 1U << order;
        struct list_head* head = &free_area[order];
        struct list_head* cur = head->next;

        while (cur != head) {
            struct list_head* next = cur->next;
            struct page* pg = node_to_page(cur);

            size_t block_start_idx = page_to_idx(pg);
            size_t block_end_idx = block_start_idx + block_pages_num;

            // case1 : no overlap
            if (block_end_idx <= start_idx || block_start_idx >= end_idx) {
                cur = next;
                continue;
            }
            log_remove_block(block_start_idx, order);
            list_del_init(&pg->node);

            // case2 : fully overlap
            if (block_start_idx >= start_idx && block_end_idx <= end_idx) {
                pg->order = order;
                pg->is_free = 0;
                pg->refcount = 1;
            }
            // case3 : partial overlap
            else {
                if (order > 0) {
                    size_t half = block_pages_num / 2;
                    struct page* left = pg;
                    struct page* right = &mem_map[block_start_idx + half];

                    left->order = order - 1;
                    left->is_free = 1;
                    left->refcount = 0;
                    INIT_LIST_HEAD(&left->node);
                    list_add(&left->node, &free_area[order - 1]);

                    right->order = order - 1;
                    right->is_free = 1;
                    right->refcount = 0;
                    INIT_LIST_HEAD(&right->node);
                    list_add(&right->node, &free_area[order - 1]);
                    
                    log_split_block(block_start_idx, block_start_idx + half, order - 1);
                    log_add_block(block_start_idx, order - 1);
                    log_add_block(block_start_idx + half, order - 1);
                } 
                else {
                    pg->is_free = 0;
                    pg->refcount = 1;
                }
            }

            cur = next;
        }
    }
}

// --------------------------------------------------
// init
// --------------------------------------------------

// ===== CHANGED: 新增 buddy_init()，只做初始化，不直接加 region =====
void buddy_init(void) {
    size_t i;

    for (i = 0; i <= MAX_ORDER; i++) {
        INIT_LIST_HEAD(&free_area[i]);
    }

    for (i = 0; i < NUM_PAGES; i++) {
        mem_map[i].order = 0;
        mem_map[i].refcount = 0;
        mem_map[i].is_free = 0;
        mem_map[i].alloc_type = 0;
        mem_map[i].pool_idx = -1;
        INIT_LIST_HEAD(&mem_map[i].node);
    }
}

// ===== CHANGED: 新增 buddy_add_region(base, size) =====
// 用來把某段 usable physical memory 加到 buddy 裡
void buddy_add_region(phys_addr_t base, size_t size) {
    size_t start_idx;
    size_t end_idx;
    size_t i;

    // 第一次加 region 時，記住 base
    // 這版先假設你只管理一段連續記憶體
    buddy_base = base;

    start_idx = 0;
    end_idx = size / PAGE_SIZE;

    if (end_idx > NUM_PAGES)
        end_idx = NUM_PAGES;

    // 先從大 block 塞進 free_area[MAX_ORDER]
    for (i = start_idx; i + (1U << MAX_ORDER) <= end_idx; i += (1U << MAX_ORDER)) {
        mem_map[i].order = MAX_ORDER;
        mem_map[i].refcount = 0;
        mem_map[i].is_free = 1;
        INIT_LIST_HEAD(&mem_map[i].node);
        list_add_tail(&mem_map[i].node, &free_area[MAX_ORDER]);
        log_add_block(i, MAX_ORDER);
    }

    // 剩下不滿 MAX_ORDER 的尾巴，簡單先用 order 0 補
    while (i < end_idx) {
        mem_map[i].order = 0;
        mem_map[i].refcount = 0;
        mem_map[i].is_free = 1;
        INIT_LIST_HEAD(&mem_map[i].node);
        list_add_tail(&mem_map[i].node, &free_area[0]);
        log_add_block(i, 0);
        i++;
    }
}

// --------------------------------------------------
// dump / test
// --------------------------------------------------

void dump(void) {
    int i;
    uart_puts("buddy free_area dump:\r\n");
    for (i = MAX_ORDER; i >= 0; i--) {
        uart_puts("free_area[");
        uart_put_dec(i);
        uart_puts("] = ");
        uart_put_dec(count_free_list(i));
        uart_puts("\r\n");
    }
}

void dump_map(void) {
    int i;
    uart_puts("map=================\r\n");
    for (i = 0; i < 32; i++) {
        uart_puts("page[");
        uart_put_dec(i);
        uart_puts("] order=");
        uart_put_dec(mem_map[i].order);
        uart_puts(" is_free=");
        uart_put_dec(mem_map[i].is_free);
        uart_puts(" refcount=");
        uart_put_dec(mem_map[i].refcount);
        uart_puts("\r\n");
    }
    uart_puts("====================\r\n");
}

void wait_enter(void) {
    uart_puts("press enter...\r\n");
    while (1) {
        char c = uart_getc();
        if (c == '\r' || c == '\n') {
            break;
        }
    }
}

// ===== CHANGED: 新增簡單 kernel test =====
void test_buddy(void) {
    struct page* p1;
    struct page* p2;
    struct page* p3;

    uart_puts("=== buddy test start ===\r\n");
    wait_enter();

    uart_puts("alloc p1\r\n");
    p1 = alloc_pages(1);
    dump();
    wait_enter();

    uart_puts("alloc p1 p2\r\n");
    p2 = alloc_pages(1);
    dump();
    wait_enter();

    uart_puts("alloc p1 p2 p3\r\n");
    p3 = alloc_pages(1);
    dump();
    wait_enter();

    if (p1) {
        uart_puts("p1 idx = ");
        uart_put_dec(page_to_idx(p1));
        uart_puts(", phys = ");
        uart_hex(idx_to_phys(page_to_idx(p1)));
        uart_puts("\r\n");
    }

    if (p2) {
        uart_puts("p2 idx = ");
        uart_put_dec(page_to_idx(p2));
        uart_puts(", phys = ");
        uart_hex(idx_to_phys(page_to_idx(p2)));
        uart_puts("\r\n");
    }

    if (p3) {
        uart_puts("p3 idx = ");
        uart_put_dec(page_to_idx(p3));
        uart_puts(", phys = ");
        uart_hex(idx_to_phys(page_to_idx(p3)));
        uart_puts("\r\n");
    }
    uart_puts("free p1\r\n");
    free_pages(p1);
    dump();
    wait_enter();

    uart_puts("free p1 p2\r\n");
    free_pages(p2);
    dump();
    wait_enter();
    
    uart_puts("free p1 p2 p3\r\n");
    free_pages(p3);
    dump();
    wait_enter();

    uart_puts("=== buddy test end ===\r\n");
}
