#include "fdt.h"
#include "initrd.h"
#include "uart.h"
#include "allocate.h"
#include "startup_alloc.h"

extern char _start[];
extern char _end[];


static int str_equal(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == *b;
}

static int str_prefix(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s != *prefix) {
            return 0;
        }
        s++;
        prefix++;
    }
    return 1;
}

static char *skip_space(char *s) {
    while (*s == ' ') {
        s++;
    }
    return s;
}

void test_alloc_1() {
    uart_puts("Testing memory allocation...\n");
    wait_enter();
    uart_puts("allocate ptr1\n");
    char *ptr1 = (char *)allocate(4000);

    wait_enter();
    uart_puts("allocate ptr2\n");
    char *ptr2 = (char *)allocate(8000);

    wait_enter();
    uart_puts("allocate ptr3\n");
    char *ptr3 = (char *)allocate(4000);

    wait_enter();
    uart_puts("allocate ptr4\n");
    char *ptr4 = (char *)allocate(4000);

    wait_enter();
    uart_puts("free ptr1\n");
    free(ptr1);

    wait_enter();
    uart_puts("free ptr2\n");
    free(ptr2);

    wait_enter();
    uart_puts("free ptr3\n");
    free(ptr3);

    wait_enter();
    uart_puts("free ptr4\n");
    free(ptr4);

    /* Test kmalloc */
    uart_puts("Testing dynamic allocator...\n");
    wait_enter();
    uart_puts("allocate k1\n");
    char *kmem_ptr1 = (char *)allocate(16);

    wait_enter();
    uart_puts("allocate k2\n");
    char *kmem_ptr2 = (char *)allocate(32);

    wait_enter();
    uart_puts("allocate k3\n");
    char *kmem_ptr3 = (char *)allocate(64);

    wait_enter();
    uart_puts("allocate k4\n");
    char *kmem_ptr4 = (char *)allocate(128);

    wait_enter();
    uart_puts("free k1\n");
    free(kmem_ptr1);

    wait_enter();
    uart_puts("free k2\n");
    free(kmem_ptr2);

    wait_enter();
    uart_puts("free k3\n");
    free(kmem_ptr3);

    wait_enter();
    uart_puts("free k4\n");
    free(kmem_ptr4);

    wait_enter();
    uart_puts("allocate k5\n");
    char *kmem_ptr5 = (char *)allocate(16);

    wait_enter();
    uart_puts("allocate k6\n");
    char *kmem_ptr6 = (char *)allocate(32);


    wait_enter();
    uart_puts("free k5\n");
    free(kmem_ptr5);

    wait_enter();
    uart_puts("free k6\n");
    free(kmem_ptr6);

    // Test allocate new page if the cache is not enough
    void *kmem_ptr[102];
    for (int i=0; i<100; i++) {
        kmem_ptr[i] = (char *)allocate(128);
    }
    for (int i=0; i<100; i++) {
        free(kmem_ptr[i]);
    }

    // Test exceeding the maximum size
    char *kmem_ptr7 = (char *)allocate(MAX_ALLOC_SIZE + 1);
    if (kmem_ptr7 == NULL) {
        uart_puts("Allocation failed as expected for size > MAX_ALLOC_SIZE\n");
    }
    else {
        uart_puts("Unexpected allocation success for size > MAX_ALLOC_SIZE\n");
        free(kmem_ptr7);
    }
}

void test_kmalloc(void) {
    void* a;
    void* b;
    void* c;
    void* d;

    uart_puts("=== kmalloc test start ===\r\n");

    a = allocate(16);
    b = allocate(32);
    c = allocate(64);
    d = allocate(128);

    uart_puts("allocated 16/32/64/128\r\n");

    free(a);
    free(b);
    free(c);
    free(d);

    uart_puts("freed 16/32/64/128\r\n");
    uart_puts("=== kmalloc test end ===\r\n");
}
/*
void test_reserve_simple(void) {
    unsigned long base = 0x10000000UL;
    unsigned long size = 16 * 1024 * 1024UL;

    uart_puts("=== reserve simple test start ===\r\n");

    buddy_init();
    buddy_add_region(base, size);
    dump();
    wait_enter();

    uart_puts("[S1] reserve one aligned page\r\n");
    memory_reserve(base + 0x2000, 0x1000);
    dump();
    wait_enter();

    uart_puts("[S2] reserve unaligned range\r\n");
    memory_reserve(base + 0x3500, 0x900);
    dump();
    wait_enter();

    uart_puts("[S3] reserve cross multiple pages\r\n");
    memory_reserve(base + 0x8000, 0x5000);
    dump();
    wait_enter();

    uart_puts("=== reserve simple test end ===\r\n");
}

void test_reserve_full(void) {
    unsigned long base = 0x10000000UL;
    unsigned long size = 16 * 1024 * 1024UL;
    struct page *p0;
    struct page *p1;
    struct page *p2;

    uart_puts("=== reserve full test start ===\r\n");

    buddy_init();
    buddy_add_region(base, size);

    uart_puts("[F0] initial state\r\n");
    dump();
    wait_enter();

    uart_puts("[F1] reserve one aligned page [base+0x2000, +0x1000)\r\n");
    memory_reserve(base + 0x2000, 0x1000);
    dump();
    wait_enter();

    uart_puts("[F2] reserve unaligned small range [base+0x3500, +0x900)\r\n");
    memory_reserve(base + 0x3500, 0x900);
    dump();
    wait_enter();

    uart_puts("[F3] reserve cross pages [base+0x8000, +0x5000)\r\n");
    memory_reserve(base + 0x8000, 0x5000);
    dump();
    wait_enter();

    uart_puts("[F4] reserve inside larger block [base+0x20000, +0x9000)\r\n");
    memory_reserve(base + 0x20000, 0x9000);
    dump();
    wait_enter();

    uart_puts("[F5] reserve large aligned range [base+0x40000, +0x20000)\r\n");
    memory_reserve(base + 0x40000, 0x20000);
    dump();
    wait_enter();

    uart_puts("[F6] reserve near end of region [base+size-0x1000, +0x4000)\r\n");
    memory_reserve(base + size - 0x1000, 0x4000);
    dump();
    wait_enter();

    uart_puts("[F7] allocate order-0 after reserves\r\n");
    p0 = alloc_pages(0);
    if (p0) {
        uart_puts("order-0 allocated at ");
        uart_hex((unsigned long)page_to_ptr(p0));
        uart_puts("\r\n");
    } else {
        uart_puts("order-0 allocation failed\r\n");
    }
    wait_enter();

    uart_puts("[F8] allocate order-1 after reserves\r\n");
    p1 = alloc_pages(1);
    if (p1) {
        uart_puts("order-1 allocated at ");
        uart_hex((unsigned long)page_to_ptr(p1));
        uart_puts("\r\n");
    } else {
        uart_puts("order-1 allocation failed\r\n");
    }
    wait_enter();

    uart_puts("[F9] allocate order-2 after reserves\r\n");
    p2 = alloc_pages(2);
    if (p2) {
        uart_puts("order-2 allocated at ");
        uart_hex((unsigned long)page_to_ptr(p2));
        uart_puts("\r\n");
    } else {
        uart_puts("order-2 allocation failed\r\n");
    }
    wait_enter();

    uart_puts("[F10] free allocated pages\r\n");
    if (p0) free_pages(p0);
    if (p1) free_pages(p1);
    if (p2) free_pages(p2);
    dump();
    wait_enter();

    uart_puts("=== reserve full test end ===\r\n");
}
*/

void test_kmalloc_chunk_page_reclaim(void) {
    void *a, *b;

    uart_puts("=== kmalloc chunk reclaim test start ===\r\n");

    a = allocate(32);
    b = allocate(32);

    uart_puts("allocated a and b\r\n");

    free(a);
    uart_puts("freed a\r\n");

    free(b);
    uart_puts("freed b\r\n");

    uart_puts("=== kmalloc chunk reclaim test end ===\r\n");
}


void start_kernel(void *fdt) {
    unsigned long new_uart = get_uart_base(fdt);
    unsigned long initrd_start = get_initrd_start(fdt);

    if (new_uart == 0) {
        while (1) {}
    }

    uart_init_base(new_uart);

    uart_puts("Second kernel booted!\n");
    uart_puts("UART from FDT = ");
    uart_hex(new_uart);
    uart_puts("\n");

    uart_puts("INITRD_START = ");
    uart_hex(initrd_start);
    uart_puts("\n");

    unsigned long mem_base = get_memory_base(fdt);
    unsigned long mem_size = get_memory_size(fdt);
    size_t total_pages = mem_size / 4096;
    size_t frame_array_size = total_pages * sizeof(struct page);

    startup_allocator_init(mem_base, mem_size);

    uart_puts("MEM_BASE = ");
    uart_hex(mem_base);
    uart_puts("\n");

    uart_puts("MEM_SIZE = ");
    uart_hex(mem_size);
    uart_puts("\n");

    
    const struct fdt_header *hdr = (const struct fdt_header *)fdt;
    unsigned long dtb_size = bswap32(hdr->totalsize);
    startup_reserve((phys_addr_t)fdt, dtb_size);
    

    startup_reserve((phys_addr_t)_start, (unsigned long)_end - (unsigned long)_start);

    
    unsigned long initrd_start2 = get_initrd_start(fdt);
    unsigned long initrd_end = get_initrd_end(fdt);
    if (initrd_start2 != 0 && initrd_end > initrd_start2) {
        startup_reserve(initrd_start2, initrd_end - initrd_start2);
    }
    
    fdt_reserved_memory(fdt, 1); // 1 is startup reserve

    struct page *page_array = (struct page *)startup_alloc(frame_array_size, 4096);

    if (page_array == 0) {
        uart_puts("startup alloc frame array failed\r\n");
        while (1) {}
    }

    uart_puts("FRAME_ARRAY = ");
    uart_hex((unsigned long)page_array);
    uart_puts("\r\n");

    uart_puts("FRAME_ARRAY_SIZE = ");
    uart_put_dec(frame_array_size);
    uart_puts("\r\n");

    startup_reserve((phys_addr_t)page_array, frame_array_size);

    buddy_init(page_array, total_pages, mem_base);
    buddy_add_region(mem_base, mem_size);

    //uart_puts("memory reserve\r\n");
    //memory_reserve(0x80000000UL, 0x200000UL);
    uart_puts("reserve dtb\r\n");
    reserve_dtb(fdt);

    uart_puts("reserve kernel\r\n");
    reserve_kernel_image();

    uart_puts("reserve initrd\r\n");
    reserve_initrd(fdt);

    uart_puts("reserve reserved-memory\r\n");
    fdt_reserved_memory(fdt, 0); //0 is memory reserve

    memory_reserve((phys_addr_t)page_array, frame_array_size); // to reserve the page metadata region
    uart_puts("after all reserve\r\n");
    dump();

    kmem_init();
    test_alloc_1();
    //test_kmalloc();
    //test_kmalloc_chunk_page_reclaim();
    //test_reserve_simple();
    //test_reserve_full();
    uart_puts("FRAME_ARRAY = ");
    uart_hex((unsigned long)page_array);
    uart_puts("\r\n");

    uart_puts("FRAME_ARRAY_SIZE = ");
    uart_put_dec(frame_array_size);
    uart_puts("\r\n");
    
    uart_puts("MEM_BASE = ");
    uart_hex(mem_base);
    uart_puts("\n");

    uart_puts("MEM_SIZE = ");
    uart_hex(mem_size);
    uart_puts("\n");


    char buf[100];
    int idx = 0;

    uart_puts("2nd-kernel> ");

    while (1) {
        char c = uart_getc();

        if (c == '\r' || c == '\n') {
            buf[idx] = '\0';
            uart_puts("\r\n");

            if (buf[0] == '\0') {
            }
            else if (str_equal(buf, "help")) {
                uart_puts("Available commands:\r\n");
                uart_puts("  help        - show all commands.\r\n");
                uart_puts("  hello       - print Hello world.\r\n");
                uart_puts("  ls          - list the files.\r\n");
                uart_puts("  cat         - demonstrate the txt content.\r\n");
            }
            else if (str_equal(buf, "hello")) {
                uart_puts("Hello World from second kernel.\r\n");
            }
            else if (str_equal(buf, "ls")) {
                if (initrd_start == 0) {
                    uart_puts("initrd not found\r\n");
                } else {
                    initrd_list((const void *)initrd_start);
                }
            }
            else if (str_prefix(buf, "cat")) {
                char *filename = skip_space(buf + 3);

                if (*filename == '\0') {
                    uart_puts("Usage: cat <filename>\r\n");
                } else if (initrd_start == 0) {
                    uart_puts("initrd not found\r\n");
                } else {
                    initrd_cat((const void *)initrd_start, filename);
                }
            }
            else {
                uart_puts("Unknown command\r\n");
            }

            idx = 0;
            uart_puts("\r\n2nd-kernel> ");
        } else {
            if (idx < 99) {
                uart_putc(c);
                buf[idx++] = c;
            }
        }
    }
}
