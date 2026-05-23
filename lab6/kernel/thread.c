#include "thread.h"
#include "allocate.h"
#include "uart.h"
#include "utils.h"
#include "vm.h"

extern void switch_to(struct task_struct *prev, struct task_struct *next);

static int nr_threads = 0;
static struct task_struct *run_queue = 0;


void thread_free_task(struct task_struct *task) {
    if (task == 0)
        return;

    if (task->pgd && task->user_code_size)
        vm_free_user_pages(task->pgd, task->user_code, task->user_code_size);
    else if (task->user_code_pa)
        free((void *)phys_to_virt(task->user_code_pa));

    if (task->pgd && task->user_stack && task->user_stack_size)
        vm_free_user_pages(task->pgd, task->user_stack, task->user_stack_size);
    else if (task->user_stack_pa)
        free((void *)phys_to_virt(task->user_stack_pa));

    if (task->signal_stack_pa)
        free((void *)phys_to_virt(task->signal_stack_pa));

    for (int i = 0; i < MAX_MMAP_REGIONS; i++) {
        if (task->pgd && task->mmap_regions[i].used)
            vm_free_user_pages(task->pgd,
                               task->mmap_regions[i].start,
                               task->mmap_regions[i].size);
    }

    if (task->pgd)
        vm_destroy_pgd(task->pgd);

    if (task->stack)
        free((void *)task->stack);

    free(task);
}

void thread_remove(struct task_struct *task) {
    struct task_struct *prev;
    struct task_struct *cur;

    if (run_queue == 0 || task == 0)
        return;

    prev = run_queue;
    cur = run_queue->next;

    if (task == run_queue) { //要移除的是run_queue
        while (prev->next != run_queue){
            prev = prev->next;
        }
            
        if (run_queue->next == run_queue) {
            run_queue = 0;
        } 
        else {
            prev->next = run_queue->next;
            run_queue = run_queue->next;
        }
        return;
    }

    while (cur != run_queue) {
        if (cur == task) {
            prev->next = cur->next;
            return;
        }

        prev = cur;
        cur = cur->next;
    }
}


static void enqueue(struct task_struct *task) {
    if (run_queue == 0) {
        run_queue = task;
        task->next = task;
        return;
    }

    struct task_struct *tail = run_queue;
    while (tail->next != run_queue)
        tail = tail->next;

    tail->next = task;
    task->next = run_queue;
}

struct task_struct *get_current(void) {
    register struct task_struct *current asm("tp"); //current 這個 C 變數直接使用 tp 這個 register
    return current;
}

static void thread_bootstrap(void) {
    struct task_struct *current = get_current();

    if (current->entry)
        current->entry();

    thread_exit();
}

struct task_struct *thread_create(void (*fn)(void)) {
    struct task_struct *task = allocate(sizeof(struct task_struct));

    if (task == 0)
        return 0;

    kmemset_local(task, 0, sizeof(struct task_struct));

    task->pid = nr_threads++;
    task->state = THREAD_RUNNABLE;
    task->stack = (unsigned long)allocate(THREAD_STACK_SIZE); //kernel stack
    task->entry = fn;
    task->waiting_pid = -1;

    if (task->stack == 0) {
        free(task);
        return 0;
    }

    task->thread.ra = (unsigned long)thread_bootstrap;
    //因為是kernel thread 只要管kernel stack的sp就好
    task->thread.sp = task->stack + THREAD_STACK_SIZE; //sp set to the top of "kernel stack"

    for (int i = 0; i < 12; i++)
        task->thread.s[i] = 0;

    enqueue(task);

    return task;
}


void schedule(void) {
    struct task_struct *current = get_current();
    struct task_struct *next;

    if (current == 0 || current->next == 0)
        return;

    next = current->next;
    while (next != current && next->state != THREAD_RUNNABLE)
        next = next->next;

    if (next == current || next->state != THREAD_RUNNABLE)
        return;

    if (current->state == THREAD_RUNNING)
        current->state = THREAD_RUNNABLE;

    next->state = THREAD_RUNNING;
    vm_switch_pgd(next->pgd);
    switch_to(current, next);
}

void thread_exit(void) {
    struct task_struct *current = get_current();

    current->state = THREAD_ZOMBIE;
    schedule();

    while (1)
        ;
}

void kill_zombies(void) {
    struct task_struct *current = get_current();
    struct task_struct *prev;
    struct task_struct *cur;

    if (run_queue == 0)
        return;

    prev = run_queue;
    cur = run_queue->next;

    while (cur != run_queue) {
        if (cur != current && cur->state == THREAD_ZOMBIE && cur->parent == 0) {
            prev->next = cur->next;
            thread_free_task(cur);
            cur = prev->next;
        } 
        else {
            prev = cur;
            cur = cur->next;
        }
    }
}


void idle(void) {
    while (1) {
        kill_zombies();
        schedule();
    }
}

struct task_struct *thread_find_by_pid(int pid) {
    struct task_struct *cur;

    if (run_queue == 0)
        return 0;

    cur = run_queue;

    do {
        if (cur->pid == pid)
            return cur;

        cur = cur->next;
    } while (cur != run_queue);

    return 0;
}

void thread_reparent_children(struct task_struct *parent) {
    struct task_struct *cur;

    if (run_queue == 0)
        return;

    cur = run_queue;

    do {
        if (cur->parent == parent)
            cur->parent = 0;

        cur = cur->next;
    } while (cur != run_queue);
}

