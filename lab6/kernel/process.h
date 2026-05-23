#ifndef PROCESS_H
#define PROCESS_H

#include "thread.h"
#include "interrupt.h"

int load_user_program(struct task_struct *task, const char *path);
struct task_struct *process_create(const char *path, struct task_struct *parent);
long sys_getpid(void);
long sys_uart_read(char *buf, long count);
long sys_uart_write(const char *buf, long count);
int sys_exec(const char *path, struct pt_regs *regs);
long sys_fork(struct pt_regs *regs);
long sys_waitpid(long pid);
void sys_exit(int status);
int sys_stop(long pid);
unsigned long sys_mmap(void *addr, unsigned long length, int prot, int flags);
long sys_signal(int signum, void (*handler)(void));
void sys_sigreturn(struct pt_regs *regs);
int sys_kill(long pid, int signum);
void process_handle_signal(struct pt_regs *regs);
int process_handle_page_fault(struct pt_regs *regs);

#endif

