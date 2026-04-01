#include "fdt.h"
#include "initrd.h"
#include "uart.h"
#include "allocate.h"

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

    uart_puts("MEM_BASE = ");
    uart_hex(mem_base);
    uart_puts("\n");

    uart_puts("MEM_SIZE = ");
    uart_hex(mem_size);
    uart_puts("\n");
    buddy_init();

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
    reserve_fdt_reserved_memory(fdt);

    uart_puts("after all reserve\r\n");
    dump();

    kmem_init();
    test_alloc_1();
    //test_kmalloc();
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
