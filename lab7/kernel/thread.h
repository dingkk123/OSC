#ifndef THREAD_H
#define THREAD_H

#define THREAD_STACK_SIZE 0x1000
#define MAX_SIGNALS 32
#define MAX_MMAP_REGIONS 32
#define TASK_MAX_OPEN_FILES 16

#include "interrupt.h"

struct vnode;
struct file;


enum thread_state {
    THREAD_RUNNING, 
    THREAD_RUNNABLE, 
    THREAD_ZOMBIE,
    THREAD_WAITING,
};

struct thread_struct {
    unsigned long ra;
    unsigned long sp;
    unsigned long s[12]; //s0-s11, callee saved registers, need to be preserved when switch between threads
};

struct mmap_region {
    int used;
    unsigned long start;
    unsigned long size;
    unsigned long prot;
    unsigned long flags;
    unsigned long pa;
};

struct task_struct {
    struct thread_struct thread;   // must be first, switch_to depends on this
    int pid;
    enum thread_state state;
    unsigned long stack;
    void (*entry)(void);
    struct task_struct *next;

    unsigned long user_code;
    unsigned long user_code_pa;
    unsigned long user_code_size;
    unsigned long user_code_data;
    unsigned long user_code_file_size;
    unsigned long user_stack_size;
    unsigned long user_stack;
    unsigned long user_stack_pa;
    unsigned long kernel_stack;
    struct pt_regs *trap_frame;
    unsigned long *pgd;

    struct task_struct *parent;
    int exit_status;
    int waiting_pid;

    unsigned long signal_handler[MAX_SIGNALS];
    int pending_signal;
    int handling_signal;
    struct pt_regs saved_signal_frame;
    unsigned long signal_stack;
    unsigned long signal_stack_pa;
    struct mmap_region mmap_regions[MAX_MMAP_REGIONS];
    struct vnode *root_dir;
    struct vnode *cwd;
    struct file *fd_table[TASK_MAX_OPEN_FILES];
};

struct task_struct *get_current(void);
struct task_struct *thread_create(void (*fn)(void));
struct task_struct *thread_find_by_pid(int pid);

void thread_exit(void);
void schedule(void);
void idle(void);
void kill_zombies(void);
void thread_reparent_children(struct task_struct *parent);
void thread_remove(struct task_struct *task);
void thread_free_task(struct task_struct *task);

#endif

