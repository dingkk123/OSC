#include <stdint.h>

unsigned long UART_BASE = 0;

#define UART_RBR  (unsigned char*)(UART_BASE + 0x0)
#define UART_THR  (unsigned char*)(UART_BASE + 0x0)
#define UART_LSR  (unsigned char*)(UART_BASE + 0x14)
/*
#define UART_RBR  (unsigned char*)(UART_BASE + 0x0)
#define UART_THR  (unsigned char*)(UART_BASE + 0x0)
#define UART_LSR  (unsigned char*)(UART_BASE + 0x5)
*/
#define LSR_DR    (1 << 0)
#define LSR_TDRQ  (1 << 5)

char uart_getc() {
    // TODO: Implement this function
    while ( (*UART_LSR & LSR_DR) == 0 ) {
      continue;
    }
    return *UART_RBR;
}

void uart_putc(char c) {
    // TODO: Implement this function
    while ( (*UART_LSR & LSR_TDRQ) == 0 ) {
      continue;
    }
    *UART_THR = c;
}

void uart_puts(const char* s) {
    // TODO: Implement this function
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}

void uart_hex(unsigned long h) {
    uart_puts("0x");
    unsigned long n;
    for (int c = 60; c >= 0; c -= 4) {
        n = (h >> c) & 0xf;
        n += n > 9 ? 0x57 : '0';
        uart_putc(n);
    }
}


