#ifndef INTERRUPT_H
#define INTERRUPT_H

#define SCAUSE_INTERRUPT_FLAG (1UL << 63) //interrupt=1, exception=0
#define SCAUSE_SUPERVISOR_TIMER (SCAUSE_INTERRUPT_FLAG | 5UL) //riscvpdf 4.18 p.745
#define SCAUSE_SUPERVISOR_EXTERNAL (SCAUSE_INTERRUPT_FLAG | 9UL)

struct pt_regs {
    unsigned long ra;
    unsigned long sp;
    unsigned long gp;
    unsigned long tp;
    unsigned long t0;
    unsigned long t1;
    unsigned long t2;
    unsigned long s0;
    unsigned long s1;
    unsigned long a0;
    unsigned long a1;
    unsigned long a2;
    unsigned long a3;
    unsigned long a4;
    unsigned long a5;
    unsigned long a6;
    unsigned long a7;
    unsigned long s2;
    unsigned long s3;
    unsigned long s4;
    unsigned long s5;
    unsigned long s6;
    unsigned long s7;
    unsigned long s8;
    unsigned long s9;
    unsigned long s10;
    unsigned long s11;
    unsigned long t3;
    unsigned long t4;
    unsigned long t5;
    unsigned long t6;
    unsigned long sepc;
    unsigned long sstatus;
    unsigned long scause;
    unsigned long stval;
};

void interrupt_init(void);
void do_trap(struct pt_regs* regs);
void interrupt_set_user_program_base(unsigned long base);

#define SSTATUS_SIE (1UL << 1)

//to save the current interrupt state and disable interrupts, return the previous interrupt state
static inline unsigned long irq_save(void) {
    unsigned long flags;

    //csr read and clear sstatus SIE bit to disable interrupts
    //flags = sstatus
    //sstatus = sstatus & ~SSTATUS_SIE (disable SIE bit to disable interrupts)
    asm volatile("csrrc %0, sstatus, %1" 
                 : "=r"(flags)
                 : "r"(SSTATUS_SIE)
                 : "memory");

    return flags;
}

//csrs:csr write and set，把原本的flags裡面有SSTATUS_SIE的部分寫回去，讓interrupt恢復到之前的狀態
static inline void irq_restore(unsigned long flags) {
    if (flags & SSTATUS_SIE) {
        asm volatile("csrs sstatus, %0"
                     :
                     : "r"(SSTATUS_SIE)
                     : "memory");
    }
}

#endif
