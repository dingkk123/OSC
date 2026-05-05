#include "thread.h"
#include "allocate.h"
#include "uart.h"
#include "utils.h"

extern void switch_to(struct task_struct *prev, struct task_struct *next);

static int nr_threads = 0;
static struct task_struct *run_queue = 0;


void thread_free_task(struct task_struct *task) {
    if (task->user_code)
        free((void *)task->user_code);

    if (task->user_stack)
        free((void *)task->user_stack);

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

    if (task == run_queue) {
        while (prev->next != run_queue)
            prev = prev->next;

        if (run_queue->next == run_queue) {
            run_queue = 0;
        } else {
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
    register struct task_struct *current asm("tp");
    return current;
}

static void thread_bootstrap(void) {
    struct task_struct *current = get_current();

    uart_puts("[thread] bootstrap pid=");
    uart_put_dec(current->pid);
    uart_puts("\r\n");

    if (current->entry)
        current->entry();

    thread_exit();
}

struct task_struct *thread_create(void (*fn)(void)) {
    uart_puts("[thread] create begin\r\n");

    struct task_struct *task = allocate(sizeof(struct task_struct));
    uart_puts("[thread] after task alloc\r\n");

    if (task == 0)
        return 0;

    kmemset_local(task, 0, sizeof(struct task_struct));

    task->pid = nr_threads++;
    task->state = THREAD_RUNNABLE;

    uart_puts("[thread] before stack alloc\r\n");
    task->stack = (unsigned long)allocate(THREAD_STACK_SIZE);
    uart_puts("[thread] after stack alloc\r\n");

    task->entry = fn;

    if (task->stack == 0) {
        free(task);
        return 0;
    }

    task->thread.ra = (unsigned long)thread_bootstrap;
    task->thread.sp = task->stack + THREAD_STACK_SIZE;

    return task;
}

void thread_add(struct task_struct *task) {
    if (task == 0)
        return;

    uart_puts("[thread] add task pid=");
    uart_put_dec(task->pid);
    uart_puts("\r\n");

    unsigned long flags = irq_save();
    enqueue(task);
    irq_restore(flags);
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
        if (cur != current &&
            cur->state == THREAD_ZOMBIE &&
            cur->parent == 0) {
            prev->next = cur->next;
            thread_free_task(cur);
            cur = prev->next;
        } else {
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
