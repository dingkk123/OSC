#ifndef UART_H
#define UART_H

void uart_init_base(unsigned long base);
char uart_getc(void);
void uart_putc(char c);
void uart_puts(const char *s);
void uart_hex(unsigned long h);
void uart_put_dec(unsigned long x);

#endif
