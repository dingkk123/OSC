#ifndef SYSCALL_H
#define SYSCALL_H

#include "interrupt.h"

#define SYS_GETPID     0
#define SYS_UART_READ  1
#define SYS_UART_WRITE 2
#define SYS_EXEC       3
#define SYS_FORK       4
#define SYS_WAITPID    5
#define SYS_EXIT       6
#define SYS_STOP       7
#define SYS_DISPLAY    8
#define SYS_USLEEP     9
#define SYS_SIGNAL     10
#define SYS_SIGRETURN  11
#define SYS_KILL       12
#define SYS_MMAP       13

void syscall_handle(struct pt_regs *regs);

#endif

