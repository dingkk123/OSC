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

void syscall_handle(struct pt_regs *regs);

#endif
