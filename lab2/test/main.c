#include "fdt.h"
#include "initrd.h"

unsigned long UART_BASE = 0;

#define UART_RBR  ((volatile unsigned char *)(UART_BASE + 0x0))
#define UART_THR  ((volatile unsigned char *)(UART_BASE + 0x0))
#define UART_LSR  ((volatile unsigned char *)(UART_BASE + 0x14))
#define LSR_DR    (1 << 0)
#define LSR_TDRQ  (1 << 5)

static void uart_putc(char c) {
    while (((*UART_LSR) & LSR_TDRQ) == 0) {}
    *UART_THR = c;
}

static void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}

static char uart_getc(void) {
    while (((*UART_LSR) & LSR_DR) == 0) {}
    return *UART_RBR;
}

static void uart_hex(unsigned long h) {
    uart_puts("0x");
    for (int c = 60; c >= 0; c -= 4) {
        unsigned long n = (h >> c) & 0xf;
        n += n > 9 ? 0x57 : '0';
        uart_putc((char)n);
    }
}

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

void start_kernel(void *fdt) {
    

    unsigned long new_uart = get_uart_base(fdt);
    unsigned long initrd_start = get_initrd_start(fdt);

    if (new_uart == 0) {
        while (1) {}
    }

    UART_BASE = new_uart;

    uart_puts("Second kernel booted!\n");
    uart_puts("UART from FDT = ");
    uart_hex(new_uart);
    uart_puts("\n");
    
    uart_puts("INITRD_START = ");
    uart_hex(initrd_start);
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
            else if(str_equal(buf, "help")){
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
        }
        else {
            if (idx < 99) {
                uart_putc(c);
                buf[idx++] = c;
            }
        }
    }
}
