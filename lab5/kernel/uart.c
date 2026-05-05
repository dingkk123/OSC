#include "interrupt.h"

static unsigned long UART_BASE = 0;

#define UART_RBR  (volatile unsigned char*)(UART_BASE + 0x0)
#define UART_THR  (volatile unsigned char*)(UART_BASE + 0x0)
#define UART_IER  (volatile unsigned char*)(UART_BASE + 0x4) //Interrupt Enable Register uartpdf16.3.4.5
#define UART_IIR  (volatile unsigned char*)(UART_BASE + 0x8) //Interrupt Identification Register uartpdf16.3.4.6
#define UART_MCR  (volatile unsigned char*)(UART_BASE + 0x10) //Modem Control Register uartpdf16.3.4.9
#define UART_LSR  (volatile unsigned char*)(UART_BASE + 0x14)

//Received Data Available Interrupt Enable bit in UART_IER, when set, UART would generate interrupt when receive data and store in RBR, 
//and the interrupt would be handled in uart_handle_irq to read the data and push to rx_buf
#define UART_IER_RDI  (1 << 0)

//Transmitter Holding Register Empty Interrupt Enable bit(at bit 1) when set, 
//when thr is empty would interrupt and accept new data to transmit
//and the interrupt would be handled in uart_handle_irq to push data from tx_buf to THR to transmit
#define UART_IER_THRI (1 << 1) 

#define UART_MCR_OUT2 (1 << 3) //OUT2 bit in MCR must be set to 1 to enable UART interrupt to PLIC

#define BUF_SIZE 1024

static volatile char rx_buf[BUF_SIZE];
static volatile int rx_head = 0;
static volatile int rx_tail = 0;

static volatile char tx_buf[BUF_SIZE];
static volatile int tx_head = 0;
static volatile int tx_tail = 0;

static int uart_irq_enabled = 0;

//line status register bit 0 and bit 5
#define LSR_DR    (1 << 0)  //when set, 代表有data在rbr(receive buffer)要被讀取
#define LSR_TDRQ  (1 << 5) //when set, means THR 是空的，可以寫新的data進thr來傳輸

static int buf_next(int x) {
    return (x + 1) % BUF_SIZE;
}

static int rx_empty(void) {
    return rx_head == rx_tail;
}

static int rx_full(void) {
    return buf_next(rx_head) == rx_tail;
}

static int tx_empty(void) {
    return tx_head == tx_tail;
}

static int tx_full(void) {
    return buf_next(tx_head) == tx_tail;
}

static void rx_push(char c) {
    if (!rx_full()) {
        rx_buf[rx_head] = c;
        rx_head = buf_next(rx_head);
    }
}

//to pop one char from tx_buf
static char tx_pop(void) {
    char c = tx_buf[tx_tail];
    tx_tail = buf_next(tx_tail);
    return c;
}

static void tx_push(char c) {
    if (!tx_full()) {
        tx_buf[tx_head] = c;
        tx_head = buf_next(tx_head);
    }
}

int uart_rx_available(void) {
    return !rx_empty();
}

static void uart_putc_blocking(char c) {
    if (c == '\n')
        uart_putc_blocking('\r');

    while ((*UART_LSR & LSR_TDRQ) == 0)
        ;
    *UART_THR = c;
}



//
static void uart_kick_tx(void) {
    //to check if there is data to transmit and the THR is empty, if so, push data from tx_buf to THR to transmit
    while (!tx_empty() && (*UART_LSR & LSR_TDRQ)) {
        *UART_THR = tx_pop();
    }

    //interrrupt setting
    if (tx_empty()) { //tx is empty, no data to transmit, disable THRI interrupt
        *UART_IER &= ~UART_IER_THRI;
    } else {
        *UART_IER |= UART_IER_THRI;
    }
}

void uart_init_base(unsigned long base) {
    UART_BASE = base;
}

char uart_getc(void) {
    unsigned long flags;
    char c;

    if (!uart_irq_enabled) {
        if ((*UART_LSR & LSR_DR) == 0)
            return 0;

        c = (char)*UART_RBR;
        return c == '\r' ? '\n' : c;
    }

    flags = irq_save();

    if (rx_empty()) {
        irq_restore(flags);
        return 0;
    }

    c = rx_buf[rx_tail];
    rx_tail = buf_next(rx_tail);

    irq_restore(flags);
    return c == '\r' ? '\n' : c;
}




void uart_putc(char c) {
    unsigned long flags;

    if (!uart_irq_enabled) {
        uart_putc_blocking(c);
        return;
    }

    if (c == '\n') {
        uart_putc('\r');
    }

    flags = irq_save();
    tx_push(c);
    uart_kick_tx();//每次putc都call uart_kick_tx來檢查要不要把data從tx_buf推到THR裡面去傳輸
    irq_restore(flags);
}


void uart_puts(const char* s) {
    while (*s)
        uart_putc(*s++);
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

void uart_put_dec(unsigned long x) {
    char buf[32];
    int i = 0;

    if (x == 0) {
        uart_putc('0');
        return;
    }

    while (x > 0) {
        buf[i++] = '0' + (x % 10);
        x /= 10;
    }

    while (i > 0) {
        uart_putc(buf[--i]);
    }
}

//to enable UART interrupt
void uart_enable_interrupt(void) {
    uart_irq_enabled = 1;
    *UART_MCR |= UART_MCR_OUT2; //to enable UART interrupt to PLIC
    *UART_IER |= UART_IER_RDI; //to enable Received Data Available Interrupt, when receive data would interrupt and handled in uart_handle_irq to read the data and push to rx_buf
    uart_kick_tx();
}


void uart_handle_irq(void) {
    unsigned char iir = *UART_IIR;
    (void)iir;

    while (*UART_LSR & LSR_DR) {//因為LSR_DR是在bit 0 ，所以and完其實就是在檢查bit 0是不是1，1代表有data在rbr(receive buffer)要被讀取
        char c = (char)*UART_RBR;
        rx_push(c);
    }

    uart_kick_tx();
}

