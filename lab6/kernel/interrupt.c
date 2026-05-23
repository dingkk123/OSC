#include "interrupt.h"
#include "timer.h"
#include "uart.h"
#include "syscall.h"
#include "process.h"
#include "vm.h"


#define UART_IRQ 42UL //dts use Ctrl+F find "interrupt"

#define PLIC_BASE phys_to_virt(0xe0000000UL)////dts use Ctrl+F find "PLIC"
#define PLIC_PRIORITY(irq)   (PLIC_BASE + (irq) * 4) //0xe0000000 + 42*4, UART_IRQ priority register address
#define PLIC_ENABLE(hart)    (PLIC_BASE + 0x2080 + (hart) * 0x100)
#define PLIC_THRESHOLD(hart) (PLIC_BASE + 0x201000 + (hart) * 0x2000)
#define PLIC_CLAIM(hart)     (PLIC_BASE + 0x201004 + (hart) * 0x2000)

#define SIE_SEIE (1UL << 9)//Supervisor External Interrupt Enable bit in SIE register, see riscv-privileged-1.10 section 3.1.2
#define SSTATUS_SUM (1UL << 18)

static unsigned long boot_cpu_hartid = 0;

static unsigned long user_program_base = 0;

void interrupt_set_user_program_base(unsigned long base) {
    user_program_base = base;
}

//write the value to the memory-mapped I/O register at the specified address
static void mmio_write(unsigned long addr, unsigned int value) {
    *(volatile unsigned int *)addr = value; //因為plic是周邊裝置 所以是32bit的memory-mapped I/O register，所以要寫入32bit的值到這個地址
}

static unsigned int mmio_read(unsigned long addr) {
    return *(volatile unsigned int *)addr;
}

//to initialize the PLIC to open this hart's UART_IRQ and set the threshold, and the sie register to enable supervisor external interrupt
//haven't enable the global interrupt(sstatus.SIE) yet, that would be done in main.c after timer is initialized
static void plic_init(void) {
    unsigned long enable_addr;

    mmio_write(PLIC_PRIORITY(UART_IRQ), 1);//write 1 into the UART_IRQ priority register to enable this interrupt source, 0 means disabled

    //enable the UART_IRQ for the current hart
    //plic is store by bitset, one 32-bit register control 32 IRQ, so has to /32 to get the register
    //and *4 to get the byte address
    enable_addr = PLIC_ENABLE(boot_cpu_hartid) + (UART_IRQ / 32) * 4;

    unsigned int enable = mmio_read(enable_addr);
    enable |= (1U << (UART_IRQ % 32)); //42%32=10, so enable=0x400
    mmio_write(enable_addr, enable); //write back to th eplic enable register

    mmio_write(PLIC_THRESHOLD(boot_cpu_hartid), 0);//threshold 0 means accepting all interrupts with priority > 0

    asm volatile("csrs sie, %0" :: "r"(SIE_SEIE)); //sie = sie | SIE_SEIE, enable supervisor external interrupt
}

//to get the claimed interrupt request number from PLIC_CLAIM register
static int plic_claim(void) {
    return mmio_read(PLIC_CLAIM(boot_cpu_hartid));
}   

//write 42 back to the PLIC_CLAIM register, plic would know the interrupt is finished
static void plic_complete(int irq) {
    mmio_write(PLIC_CLAIM(boot_cpu_hartid), irq);
}

void interrupt_init(void) {
    plic_init();
}
static void uart_hex32(unsigned long h) {
    uart_puts("0x");
    for (int c = 28; c >= 0; c -= 4) {
        unsigned long n = (h >> c) & 0xf;
        uart_putc(n > 9 ? n - 10 + 'a' : n + '0');
    }
}

void do_trap(struct pt_regs* regs) {
    unsigned long sepc = regs->sepc;

    asm volatile("csrs sstatus, %0" :: "r"(SSTATUS_SUM) : "memory");

    if (regs->scause == SCAUSE_SUPERVISOR_TIMER) { 
        timer_handle_interrupt();
        process_handle_signal(regs);
        return;
    }

    if (regs->scause == SCAUSE_SUPERVISOR_EXTERNAL) { //external interrupt, could be from UART or other devices
        int irq = plic_claim(); //得到irqnunber

        if (irq == UART_IRQ) {//42就是uart
            uart_handle_irq();
        }
        //處理完interrupt 要告訴plic這個interrupt處理完了
        if (irq) {
            plic_complete(irq);
        }

        return;
    }

    if (regs->scause == SCAUSE_USER_ECALL) {
        regs->sepc += 4;
        syscall_handle(regs);
        process_handle_signal(regs);
        return;
    }

    if (regs->scause == SCAUSE_INST_PAGE_FAULT ||
        regs->scause == SCAUSE_LOAD_PAGE_FAULT ||
        regs->scause == SCAUSE_STORE_PAGE_FAULT) {
        if (process_handle_page_fault(regs))
            return;
    }



    if (user_program_base != 0 && sepc >= user_program_base) {
        sepc = sepc - user_program_base;
    }
    
    uart_puts("=== S-Mode trap ===\r\n");

    uart_puts("scause: ");
    uart_put_dec(regs->scause); //8 is ecall from U-mode
    uart_puts("\r\n");

    uart_puts("sepc: ");
    uart_hex32(sepc);
    uart_puts("\r\n");

    uart_puts("raw sepc: ");
    uart_hex(regs->sepc);
    uart_puts(" ra=");
    uart_hex(regs->ra);
    uart_puts(" gp=");
    uart_hex(regs->gp);
    uart_puts(" sp=");
    uart_hex(regs->sp);
    uart_puts("\r\n");

    uart_puts("a0=");
    uart_hex(regs->a0);
    uart_puts(" a1=");
    uart_hex(regs->a1);
    uart_puts(" a2=");
    uart_hex(regs->a2);
    uart_puts(" a7=");
    uart_hex(regs->a7);
    uart_puts("\r\n");

    uart_puts("stval: ");
    uart_put_dec(regs->stval); //0, ecall from U-mode does not have fault address
    uart_puts("\r\n");
    
    while (1)
        ;
    //regs->sepc += 4;
}

