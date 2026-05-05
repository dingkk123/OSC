#include "process.h"
#include "allocate.h"
#include "initrd.h"
#include "interrupt.h"
#include "thread.h"
#include "uart.h"
#include "utils.h"

#define STACK_SIZE 0x1000
#define USER_STACK_SIZE 0x4000
#define SSTATUS_SPIE (1UL << 5)

extern unsigned long initrd_base;
extern void ret_from_exception(void);

static void ret_to_user(void) {
    struct task_struct *current = get_current();
    uart_puts("[process] ret_to_user\r\n");

    asm volatile(
        "mv sp, %0\n"
        "j ret_from_exception\n"
        :
        : "r"(current->trap_frame)
        : "memory"
    );
}

static int find_initrd_file(const char *path, char **data_out, int *size_out) {
    char *p;

    if (initrd_base == 0)
        return -1;

    p = (char *)initrd_base;

    while (memcmp(p + sizeof(struct cpio_t), "TRAILER!!!", 10) != 0) {
        struct cpio_t *hdr = (struct cpio_t *)p;
        int namesize = hextoi(hdr->namesize, 8);
        int filesize = hextoi(hdr->filesize, 8);
        int headsize = align(sizeof(struct cpio_t) + namesize, 4);
        int datasize = align(filesize, 4);

        char *name = p + sizeof(struct cpio_t);
        char *data = p + headsize;

        if (memcmp(name, path, namesize) == 0) {
            *data_out = data;
            *size_out = filesize;
            return 0;
        }

        p += headsize + datasize;
    }

    return -1;
}

static void wake_waiting_parent(struct task_struct *task) {
    if (task->parent && task->parent->state == THREAD_WAITING && task->parent->waiting_pid == task->pid){
        task->parent->waiting_pid = -1;
        task->parent->state = THREAD_RUNNABLE;
    }
}


int load_user_program(struct task_struct *task, const char *path) {
    char *data;
    int filesize;
    unsigned long code_size;
    void *code;
    void *ustack;

    if (find_initrd_file(path, &data, &filesize) < 0)
        return -1;

    uart_puts("[load] find file\r\n");

    code_size = align(filesize, 0x1000);
    
    uart_puts("[load] filesize=");
    uart_put_dec(filesize);
    uart_puts("\r\n");

    uart_puts("[load] code_size=");
    uart_put_dec(code_size);
    uart_puts("\r\n");

    uart_puts("[load] before alloc code\r\n");
    code = allocate(code_size);
    uart_puts("[load] after alloc code\r\n");

    uart_puts("[load] before alloc stack\r\n");
    ustack = allocate(USER_STACK_SIZE);
    uart_puts("[load] after alloc stack\r\n");

    if (code == 0 || ustack == 0) {
        if (code)
            free(code);
        if (ustack)
            free(ustack);
        uart_puts("[load] alloc failed\r\n");
        return -1;
    }

    kmemset_local(code, 0, code_size);
    uart_puts("[load] before copy\r\n");
    kmemcpy_local(code, data, filesize);
    uart_puts("[load] after copy\r\n");

    kmemset_local(ustack, 0, USER_STACK_SIZE);

    task->user_code = (unsigned long)code;
    task->user_code_size = code_size;
    task->user_stack = (unsigned long)ustack;
    task->user_stack_size = USER_STACK_SIZE;

    task->trap_frame->sp = task->user_stack + USER_STACK_SIZE;
    task->trap_frame->sepc = task->user_code;
    task->trap_frame->sstatus = SSTATUS_SPIE;

    uart_puts("[load] setup trap_frame sp=\r\n");
    uart_hex(task->trap_frame->sp);
    uart_puts("\r\n");
    uart_puts("[load] setup trap_frame sepc=\r\n");
    uart_hex(task->trap_frame->sepc);
    uart_puts("\r\n");

    return 0;
}

struct task_struct *process_create(const char *path, struct task_struct *parent) {
    uart_puts("[process] create begin\r\n");
    struct task_struct *task = thread_create(ret_to_user);
    uart_puts("[process] after thread_create\r\n");
    if (task == 0)
        return 0;

    task->parent = parent;
    task->exit_status = 0;
    task->waiting_pid = -1;

    task->kernel_stack = task->stack;
    task->trap_frame = (struct pt_regs *)(task->kernel_stack + THREAD_STACK_SIZE - sizeof(struct pt_regs));
    kmemset_local(task->trap_frame, 0, sizeof(struct pt_regs));


    uart_puts("[process] before load\r\n");
    if (load_user_program(task, path) < 0) {
        uart_puts("[process] load failed\r\n");
        task->state = THREAD_ZOMBIE;
        return 0;
    }
    uart_puts("[process] after load\r\n");
    uart_puts("[process] created\r\n");

    thread_add(task);
    uart_puts("[process] after thread_add\r\n");
    return task;
}

long sys_getpid(void) {
    return get_current()->pid;
}

long sys_uart_read(char *buf, long count) {
    long i;

    if (buf == 0 || count < 0)
        return -1;

    for (i = 0; i < count; i++) {
        char c;

        do {
            c = uart_getc();
            if (c == 0)
                schedule();
        } while (c == 0);

        buf[i] = c;
    }

    return i;
}

long sys_uart_write(const char *buf, long count) {
    long i;

    if (buf == 0 || count < 0)
        return -1;

    for (i = 0; i < count; i++)
        uart_putc(buf[i]);

    return i;
}

int sys_exec(const char *path, struct pt_regs *regs) {
    struct task_struct *current = get_current();
    char *data;
    int filesize;

    if (path == 0)
        return -1;

    if (find_initrd_file(path, &data, &filesize) < 0)
        return -1;

    if (current->user_code)
        free((void *)current->user_code);
    if (current->user_stack)
        free((void *)current->user_stack);

    current->trap_frame = regs;
    kmemset_local(regs, 0, sizeof(struct pt_regs));

    if (load_user_program(current, path) < 0)
        return -1;

    return 0;
}


long sys_fork(struct pt_regs *regs) {
    struct task_struct *parent = get_current();
    struct task_struct *child;
    unsigned long pc_off;
    unsigned long sp_off;

    child = thread_create(ret_to_user);
    if (child == 0)
        return -1;

    child->parent = parent;
    child->exit_status = 0;
    child->waiting_pid = -1;

    child->kernel_stack = child->stack;
    child->trap_frame = (struct pt_regs *)(child->kernel_stack + THREAD_STACK_SIZE - sizeof(struct pt_regs));
    kmemcpy_local(child->trap_frame, regs, sizeof(struct pt_regs));

    child->user_code_size = parent->user_code_size;
    child->user_stack_size = parent->user_stack_size;

    child->user_code = (unsigned long)allocate(child->user_code_size);
    child->user_stack = (unsigned long)allocate(child->user_stack_size);

    if (child->user_code == 0 || child->user_stack == 0) {
        child->state = THREAD_ZOMBIE;
        return -1;
    }

    kmemcpy_local((void *)child->user_code, (void *)parent->user_code, child->user_code_size);
    kmemcpy_local((void *)child->user_stack, (void *)parent->user_stack, child->user_stack_size);

    pc_off = regs->sepc - parent->user_code;
    sp_off = regs->sp - parent->user_stack;

    child->trap_frame->sepc = child->user_code + pc_off;
    child->trap_frame->sp = child->user_stack + sp_off;

    child->trap_frame->a0 = 0;

    thread_add(child);
    return child->pid;
}

long sys_waitpid(long pid) {
    struct task_struct *current = get_current();

    while (1) {
        struct task_struct *child = thread_find_by_pid(pid);

        if (child == 0) {
            current->waiting_pid = -1;
            return -1;
        }

        if (child->parent != current) {
            current->waiting_pid = -1;
            return -1;
        }

        if (child->state == THREAD_ZOMBIE) {
            thread_remove(child);
            thread_free_task(child);
            current->waiting_pid = -1;
            return pid;
        }

        current->state = THREAD_WAITING;
        current->waiting_pid = pid;
        schedule();
    }
}


void sys_exit(int status) {
    struct task_struct *current = get_current();

    thread_reparent_children(current);

    current->exit_status = status;
    current->state = THREAD_ZOMBIE;

    wake_waiting_parent(current);

    schedule();

    while (1)
        ;
}



int sys_stop(long pid) {
    struct task_struct *task = thread_find_by_pid(pid);

    if (task == 0)
        return -1;

    if (task == get_current())
        return -1;

    if (pid == 0 || pid == 1)
        return -1;

    thread_reparent_children(task);

    task->exit_status = -1;
    task->state = THREAD_ZOMBIE;

    wake_waiting_parent(task);

    return 0;
}


