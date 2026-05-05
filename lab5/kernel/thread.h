#ifndef THREAD_H
#define THREAD_H

#define THREAD_STACK_SIZE 0x1000

#include "interrupt.h"


enum thread_state {
    THREAD_RUNNING,
    THREAD_RUNNABLE,
    THREAD_ZOMBIE,
    THREAD_WAITING,
};

struct thread_struct {
    unsigned long ra;
    unsigned long sp;
    unsigned long s[12];
};

struct task_struct {
    struct thread_struct thread;   // must be first, switch_to depends on this
    int pid;
    enum thread_state state;
    unsigned long stack;
    void (*entry)(void);
    struct task_struct *next;

    unsigned long user_code;
    unsigned long user_code_size;
    unsigned long user_stack_size;
    unsigned long user_stack;
    unsigned long kernel_stack;
    struct pt_regs *trap_frame;

    struct task_struct *parent;
    int exit_status;
    int waiting_pid;
};

struct task_struct *get_current(void);
struct task_struct *thread_create(void (*fn)(void));
struct task_struct *thread_find_by_pid(int pid);void thread_add(struct task_struct *task);
void thread_exit(void);
void schedule(void);
void idle(void);
void kill_zombies(void);
void thread_reparent_children(struct task_struct *parent);
void thread_remove(struct task_struct *task);
void thread_free_task(struct task_struct *task);

#endif
