#include "process.h"
#include "allocate.h"
#include "initrd.h"
#include "interrupt.h"
#include "thread.h"
#include "uart.h"
#include "utils.h"
#include "vm.h"

#define STACK_SIZE 0x1000
#define USER_STACK_SIZE 0x4000
#define SIGNAL_STACK_SIZE 0x1000
#define USER_CODE_BASE 0x0UL
#define USER_STACK_TOP 0x4000000000UL
#define USER_STACK_BASE (USER_STACK_TOP - USER_STACK_SIZE)
#define USER_SIGNAL_STACK_BASE (USER_STACK_BASE - SIGNAL_STACK_SIZE)
#define USER_MMAP_BASE 0x100000UL
#define USER_MMAP_TOP USER_SIGNAL_STACK_BASE
#define SSTATUS_SPIE (1UL << 5)
#define SSTATUS_SPP (1UL << 8)
#define SIGTERM 15

#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4

#define MAP_ANONYMOUS 0x20
#define MAP_POPULATE  0x8000
#define MAP_SUPPORTED (MAP_ANONYMOUS | MAP_POPULATE)
#define MMAP_FAILED ((unsigned long)-1)

#define SIGRETURN_LI_A7 0x00b00893U
#define SIGRETURN_ECALL 0x00000073U
#define SIGRETURN_LOOP 0x0000006fU

extern unsigned long initrd_base;
extern void ret_from_exception(void);

static void wake_waiting_parent(struct task_struct *task);

static void release_user_address_space(unsigned long *pgd,
                                       unsigned long code_pa,
                                       unsigned long code_va,
                                       unsigned long code_size,
                                       unsigned long stack_pa,
                                       unsigned long stack_va,
                                       unsigned long stack_size,
                                       unsigned long signal_stack_pa) {
    if (pgd && code_size)
        vm_free_user_pages(pgd, code_va, code_size);
    else if (code_pa)
        free((void *)phys_to_virt(code_pa));
    if (pgd && stack_va && stack_size)
        vm_free_user_pages(pgd, stack_va, stack_size);
    else if (stack_pa)
        free((void *)phys_to_virt(stack_pa));
    if (signal_stack_pa)
        free((void *)phys_to_virt(signal_stack_pa));
    if (pgd)
        vm_destroy_pgd(pgd);
}

static void ret_to_user(void) {
    struct task_struct *current = get_current();

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

static unsigned long align_up_page_ulong(unsigned long value) {
    return (value + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

static int range_overlaps(unsigned long start,
                          unsigned long end,
                          unsigned long other_start,
                          unsigned long other_end) {
    return start < other_end && other_start < end;
}

static int mmap_range_invalid(unsigned long start, unsigned long size) {
    unsigned long end = start + size;

    if (size == 0)
        return 1;
    if (end <= start)
        return 1;
    if (end > USER_MMAP_TOP)
        return 1;

    return 0;
}

static int mmap_range_overlaps(struct task_struct *task,
                               unsigned long start,
                               unsigned long size) {
    unsigned long end = start + size;

    if (task->user_code_size &&
        range_overlaps(start, end, task->user_code, task->user_code + task->user_code_size))
        return 1;

    if (task->user_stack_size &&
        range_overlaps(start, end, task->user_stack, task->user_stack + task->user_stack_size))
        return 1;

    if (task->signal_stack &&
        range_overlaps(start, end, task->signal_stack, task->signal_stack + SIGNAL_STACK_SIZE))
        return 1;

    for (int i = 0; i < MAX_MMAP_REGIONS; i++) {
        struct mmap_region *region = &task->mmap_regions[i];

        if (!region->used)
            continue;

        if (range_overlaps(start, end, region->start, region->start + region->size))
            return 1;
    }

    return 0;
}

static int mmap_find_slot(struct task_struct *task) {
    for (int i = 0; i < MAX_MMAP_REGIONS; i++) {
        if (!task->mmap_regions[i].used)
            return i;
    }

    return -1;
}

static unsigned long mmap_find_free_base(struct task_struct *task,
                                         unsigned long hint,
                                         unsigned long size) {
    unsigned long start;

    if (hint)
        start = align_up_page_ulong(hint);
    else
        start = USER_MMAP_BASE;

    if (start < USER_MMAP_BASE)
        start = USER_MMAP_BASE;

    for (unsigned long va = start; va + size <= USER_MMAP_TOP; va += PAGE_SIZE) {
        if (!mmap_range_overlaps(task, va, size))
            return va;
    }

    for (unsigned long va = USER_MMAP_BASE; va < start && va + size <= USER_MMAP_TOP; va += PAGE_SIZE) {
        if (!mmap_range_overlaps(task, va, size))
            return va;
    }

    return 0;
}

static unsigned long mmap_prot_to_pte_flags(int prot) {
    unsigned long flags = PTE_V | PTE_U | PTE_A | PTE_D;

    if (prot & PROT_READ)
        flags |= PTE_R;
    if (prot & PROT_WRITE)
        flags |= PTE_R | PTE_W;
    if (prot & PROT_EXEC)
        flags |= PTE_X;

    return flags;
}

static int mmap_fault_allowed(struct mmap_region *region, unsigned long scause) {
    if (region == 0 || region->prot == PROT_NONE)
        return 0;

    if (scause == SCAUSE_INST_PAGE_FAULT)
        return (region->prot & PROT_EXEC) != 0;
    if (scause == SCAUSE_LOAD_PAGE_FAULT)
        return (region->prot & PROT_READ) != 0;
    if (scause == SCAUSE_STORE_PAGE_FAULT)
        return (region->prot & PROT_WRITE) != 0;

    return 0;
}

static struct mmap_region *find_mmap_region(struct task_struct *task, unsigned long addr) {
    for (int i = 0; i < MAX_MMAP_REGIONS; i++) {
        struct mmap_region *region = &task->mmap_regions[i];

        if (!region->used)
            continue;
        if (addr >= region->start && addr < region->start + region->size)
            return region;
    }

    return 0;
}

static void release_mmap_regions(struct task_struct *task) {
    for (int i = 0; i < MAX_MMAP_REGIONS; i++) {
        struct mmap_region *region = &task->mmap_regions[i];

        if (region->used && task->pgd)
            vm_free_user_pages(task->pgd, region->start, region->size);

        region->used = 0;
        region->start = 0;
        region->size = 0;
        region->prot = 0;
        region->flags = 0;
        region->pa = 0;
    }
}

static int duplicate_mmap_regions(struct task_struct *parent,
                                  struct task_struct *child) {
    for (int i = 0; i < MAX_MMAP_REGIONS; i++) {
        struct mmap_region *src = &parent->mmap_regions[i];
        struct mmap_region *dst = &child->mmap_regions[i];

        if (!src->used)
            continue;

        dst->used = 1;
        dst->start = src->start;
        dst->size = src->size;
        dst->prot = src->prot;
        dst->flags = src->flags;
        dst->pa = 0;

        if (vm_share_region_cow(parent->pgd, child->pgd, src->start, src->size) < 0)
            return -1;
    }

    return 0;
}


int load_user_program(struct task_struct *task, const char *path) {
    char *data;
    int filesize;
    unsigned long code_size;
    unsigned long *old_pgd;
    unsigned long old_code_pa;
    unsigned long old_code_va;
    unsigned long old_code_size;
    unsigned long old_stack_pa;
    unsigned long old_stack_va;
    unsigned long old_stack_size;
    unsigned long old_signal_stack_pa;
    unsigned long *new_pgd;
    void *ustack;
    unsigned long stack_pa;

    if (task == 0 || path == 0 || task->trap_frame == 0)
        return -1;

    if (find_initrd_file(path, &data, &filesize) < 0)
        return -1;

    code_size = align(filesize, 0x1000);

    new_pgd = vm_create_pgd();
    if (new_pgd == 0)
        return -1;

    ustack = allocate(PAGE_SIZE);

    if (ustack == 0) {
        vm_destroy_pgd(new_pgd);
        return -1;
    }

    for (unsigned long off = 0; off < code_size; off += PAGE_SIZE) {
        void *page = allocate(PAGE_SIZE);
        unsigned long copy_size = PAGE_SIZE;

        if (page == 0) {
            free(ustack);
            vm_free_user_pages(new_pgd, USER_CODE_BASE, code_size);
            vm_destroy_pgd(new_pgd);
            return -1;
        }

        if (off + copy_size > (unsigned long)filesize)
            copy_size = off < (unsigned long)filesize ? (unsigned long)filesize - off : 0;

        kmemset_local(page, 0, PAGE_SIZE);
        if (copy_size)
            kmemcpy_local(page, data + off, copy_size);

        map_pages(new_pgd, USER_CODE_BASE + off, PAGE_SIZE, virt_to_phys_ptr(page), PROT_USER_RWX);
    }

    kmemset_local(ustack, 0, PAGE_SIZE);

    stack_pa = virt_to_phys_ptr(ustack);

    map_pages(new_pgd, USER_STACK_TOP - PAGE_SIZE, PAGE_SIZE, stack_pa, PROT_USER_RW);

    release_mmap_regions(task);

    old_pgd = task->pgd;
    old_code_pa = task->user_code_pa;
    old_code_va = task->user_code;
    old_code_size = task->user_code_size;
    old_stack_pa = task->user_stack_pa;
    old_stack_va = task->user_stack;
    old_stack_size = task->user_stack_size;
    old_signal_stack_pa = task->signal_stack_pa;

    task->pgd = new_pgd;
    task->user_code = USER_CODE_BASE;
    task->user_code_pa = 0;
    task->user_code_size = code_size;
    task->user_stack = USER_STACK_BASE;
    task->user_stack_pa = stack_pa;
    task->user_stack_size = USER_STACK_SIZE;
    task->signal_stack = 0;
    task->signal_stack_pa = 0;

    kmemset_local(task->trap_frame, 0, sizeof(struct pt_regs));
    task->trap_frame->sp = USER_STACK_TOP;
    task->trap_frame->tp = (unsigned long)task;
    task->trap_frame->sepc = task->user_code;
    task->trap_frame->sstatus = SSTATUS_SPIE;

    if (task == get_current())
        vm_switch_pgd(task->pgd);

    release_user_address_space(old_pgd,
                               old_code_pa,
                               old_code_va,
                               old_code_size,
                               old_stack_pa,
                               old_stack_va,
                               old_stack_size,
                               old_signal_stack_pa);

    return 0;
}

static void clear_signal_state(struct task_struct *task, int clear_handlers) {
    if (task->signal_stack_pa) {
        free((void *)phys_to_virt(task->signal_stack_pa));
        task->signal_stack = 0;
        task->signal_stack_pa = 0;
    }

    task->pending_signal = 0;
    task->handling_signal = 0;
    kmemset_local(&task->saved_signal_frame, 0, sizeof(struct pt_regs));

    if (clear_handlers)
        kmemset_local(task->signal_handler, 0, sizeof(task->signal_handler));
}

static void terminate_process(struct task_struct *task, int status) {
    thread_reparent_children(task);

    task->exit_status = status;
    task->state = THREAD_ZOMBIE;

    clear_signal_state(task, 0);
    wake_waiting_parent(task);
}

//exec
struct task_struct *process_create(const char *path, struct task_struct *parent) {
    unsigned long flags;
    struct task_struct *task;

    flags = irq_save();
    task = thread_create(ret_to_user);
    if (task)
        task->state = THREAD_WAITING;
    irq_restore(flags);

    if (task == 0)
        return 0;

    task->parent = parent;
    task->exit_status = 0;
    task->waiting_pid = -1;

    task->kernel_stack = task->stack;
    task->trap_frame = (struct pt_regs *)(task->kernel_stack + THREAD_STACK_SIZE - sizeof(struct pt_regs));
    task->thread.sp = (unsigned long)task->trap_frame;
    kmemset_local(task->trap_frame, 0, sizeof(struct pt_regs));

    if (load_user_program(task, path) < 0) {
        uart_puts("[process] load failed\r\n");
        flags = irq_save();
        thread_remove(task);
        irq_restore(flags);
        thread_free_task(task);
        return 0;
    }

    flags = irq_save();
    task->state = THREAD_RUNNABLE;
    irq_restore(flags);

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

        c = uart_getc();
        while (c == 0) {
            schedule();
            c = uart_getc();
        }
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

    if (current == 0 || regs == 0 || path == 0)
        return -1;

    if (find_initrd_file(path, &data, &filesize) < 0)
        return -1;

    clear_signal_state(current, 1);

    current->trap_frame = regs;

    if (load_user_program(current, path) < 0)
        return -1;

    return 0;
}


long sys_fork(struct pt_regs *regs) {
    struct task_struct *parent = get_current();
    struct task_struct *child;
    unsigned long pc_off;
    unsigned long sp_off;
    unsigned long flags;

    if (parent == 0 || regs == 0 || parent->pgd == 0)
        return -1;

    flags = irq_save();
    child = thread_create(ret_to_user);
    if (child)
        child->state = THREAD_WAITING;
    irq_restore(flags);

    if (child == 0)
        return -1;

    child->parent = parent;
    child->exit_status = 0;
    child->waiting_pid = -1;

    //複製父程序的kernel stack和trap frame
    child->kernel_stack = child->stack;
    child->trap_frame = (struct pt_regs *)(child->kernel_stack + THREAD_STACK_SIZE - sizeof(struct pt_regs));
    child->thread.sp = (unsigned long)child->trap_frame; //sp紀錄kernel stack的top 因為trap frame放在stack top
    kmemcpy_local(child->trap_frame, regs, sizeof(struct pt_regs));

    child->pgd = vm_create_pgd();
    if (child->pgd == 0) {
        flags = irq_save();
        thread_remove(child);
        irq_restore(flags);
        thread_free_task(child);
        return -1;
    }

    kmemcpy_local(child->signal_handler, parent->signal_handler, sizeof(child->signal_handler));

    child->user_code = USER_CODE_BASE;
    child->user_code_pa = 0;
    child->user_code_size = parent->user_code_size;
    child->user_stack = USER_STACK_BASE;
    child->user_stack_pa = 0;
    child->user_stack_size = parent->user_stack_size;

    if (vm_share_region_cow(parent->pgd, child->pgd, parent->user_code, parent->user_code_size) < 0) {
        flags = irq_save();
        thread_remove(child);
        irq_restore(flags);
        thread_free_task(child);
        return -1;
    }

    if (vm_share_region_cow(parent->pgd, child->pgd, parent->user_stack, parent->user_stack_size) < 0) {
        flags = irq_save();
        thread_remove(child);
        irq_restore(flags);
        thread_free_task(child);
        return -1;
    }

    if (duplicate_mmap_regions(parent, child) < 0) {
        flags = irq_save();
        thread_remove(child);
        irq_restore(flags);
        thread_free_task(child);
        return -1;
    }

    pc_off = regs->sepc - parent->user_code;
    sp_off = regs->sp - parent->user_stack;

    child->trap_frame->sepc = child->user_code + pc_off;
    child->trap_frame->sp = child->user_stack + sp_off;

    if (regs->s0 >= parent->user_stack && regs->s0 < parent->user_stack + parent->user_stack_size) {
        child->trap_frame->s0 = child->user_stack + (regs->s0 - parent->user_stack);
    }

    child->trap_frame->tp = (unsigned long)child;
    child->trap_frame->a0 = 0;

    flags = irq_save();
    child->state = THREAD_RUNNABLE;
    irq_restore(flags);

    return child->pid;
}


long sys_waitpid(long pid) {
    struct task_struct *current = get_current(); //cur is shell thread

    if (current == 0)
        return -1;

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

    if (current == 0)
        return;

    terminate_process(current, status);

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

    terminate_process(task, -1);

    return 0;
}

unsigned long sys_mmap(void *addr, unsigned long length, int prot, int flags) {
    struct task_struct *current = get_current();
    unsigned long hint = (unsigned long)addr;
    unsigned long size;
    unsigned long start;
    unsigned long pa = 0;
    int slot;

    if (current == 0 || current->pgd == 0)
        return MMAP_FAILED;

    if ((flags & ~MAP_SUPPORTED) != 0)
        return MMAP_FAILED;

    if ((flags & MAP_ANONYMOUS) == 0)
        return MMAP_FAILED;

    if (prot < 0 || (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) != 0)
        return MMAP_FAILED;

    if (length > USER_MMAP_TOP - USER_MMAP_BASE)
        return MMAP_FAILED;

    size = align_up_page_ulong(length);
    if (size == 0)
        return MMAP_FAILED;

    slot = mmap_find_slot(current);
    if (slot < 0)
        return MMAP_FAILED;

    if (hint != 0 &&
        (hint & (PAGE_SIZE - 1)) == 0 &&
        !mmap_range_invalid(hint, size) &&
        !mmap_range_overlaps(current, hint, size)) {
        start = hint;
    } else {
        start = mmap_find_free_base(current, hint, size);
        if (start == 0)
            return MMAP_FAILED;
    }

    if (prot != PROT_NONE && (flags & MAP_POPULATE)) {
        for (unsigned long va = start; va < start + size; va += PAGE_SIZE) {
            void *mem = allocate(PAGE_SIZE);

            if (mem == 0) {
                vm_free_user_pages(current->pgd, start, size);
                return MMAP_FAILED;
            }

            kmemset_local(mem, 0, PAGE_SIZE);
            if (pa == 0)
                pa = virt_to_phys_ptr(mem);
            map_pages(current->pgd, va, PAGE_SIZE, virt_to_phys_ptr(mem), mmap_prot_to_pte_flags(prot));
        }
    }

    current->mmap_regions[slot].used = 1;
    current->mmap_regions[slot].start = start;
    current->mmap_regions[slot].size = size;
    current->mmap_regions[slot].prot = prot;
    current->mmap_regions[slot].flags = flags;
    current->mmap_regions[slot].pa = pa;

    return start;
}

long sys_signal(int signum, void (*handler)(void)) {
    struct task_struct *current = get_current();
    unsigned long old_handler;

    if (current == 0)
        return -1;

    if (signum <= 0 || signum >= MAX_SIGNALS){
        return -1;
    }
        

    old_handler = current->signal_handler[signum];
    current->signal_handler[signum] = (unsigned long)handler;

    return old_handler;
}

void sys_sigreturn(struct pt_regs *regs) {
    struct task_struct *current = get_current();
    unsigned long signal_stack_pa;

    uart_puts("[signal] sigreturn\r\n");

    if (current == 0 || regs == 0)
        return;

    if (!current->handling_signal)
        return;

    signal_stack_pa = current->signal_stack_pa;
    current->signal_stack = 0;
    current->signal_stack_pa = 0;
    current->handling_signal = 0;
    current->pending_signal = 0;

    kmemcpy_local(regs, &current->saved_signal_frame, sizeof(struct pt_regs));
    kmemset_local(&current->saved_signal_frame, 0, sizeof(struct pt_regs));

    if (signal_stack_pa)
        free((void *)phys_to_virt(signal_stack_pa));
}

int sys_kill(long pid, int signum) {
    struct task_struct *task;

    if (signum <= 0 || signum >= MAX_SIGNALS)
        return -1;

    task = thread_find_by_pid(pid);
    if (task == 0)
        return -1;

    if (pid == 0 || pid == 1)
        return -1;

    if (task->signal_handler[signum]) {
        task->pending_signal = signum;
        if (task->state == THREAD_WAITING){
            task->state = THREAD_RUNNABLE;
        }
            
        return 0;
    }

    terminate_process(task, -1);

    if (task == get_current()) {
        schedule();
        while (1)
            ;
    }

    return 0;
}

void process_handle_signal(struct pt_regs *regs) {
    struct task_struct *current = get_current();
    unsigned long handler;
    unsigned int *trampoline;
    int signum;

    if (current == 0 || regs == 0)
        return;

    if (regs->sstatus & SSTATUS_SPP)
        return;

    if (current->pgd == 0 || current->user_code_size == 0)
        return;

    if (current->handling_signal || current->pending_signal == 0)
        return;

    signum = current->pending_signal;
    if (signum <= 0 || signum >= MAX_SIGNALS) {
        current->pending_signal = 0;
        return;
    }

    handler = current->signal_handler[signum];
    if (handler == 0) {
        terminate_process(current, -1);
        schedule();
        while (1)
            ;
    }

    current->signal_stack_pa = virt_to_phys_addr((unsigned long)allocate(SIGNAL_STACK_SIZE));
    if (current->signal_stack_pa == 0) {
        terminate_process(current, -1);
        schedule();
        while (1)
            ;
    }

    current->signal_stack = USER_SIGNAL_STACK_BASE;
    map_pages(current->pgd,
              current->signal_stack,
              SIGNAL_STACK_SIZE,
              current->signal_stack_pa,
              PROT_USER_RWX);

    trampoline = (unsigned int *)phys_to_virt(current->signal_stack_pa);
    trampoline[0] = SIGRETURN_LI_A7;
    trampoline[1] = SIGRETURN_ECALL;
    trampoline[2] = SIGRETURN_LOOP;
    asm volatile(".word 0x0000100F" ::: "memory");

    kmemcpy_local(&current->saved_signal_frame, regs, sizeof(struct pt_regs));

    current->handling_signal = 1;
    current->pending_signal = 0;

    regs->sepc = handler;
    regs->ra = current->signal_stack;
    regs->sp = current->signal_stack + SIGNAL_STACK_SIZE;
    regs->a0 = signum;
    regs->tp = (unsigned long)current;
}

int process_handle_page_fault(struct pt_regs *regs) {
    struct task_struct *current = get_current();
    struct mmap_region *region;
    unsigned long fault_addr;
    unsigned long page;
    unsigned long map_flags;
    void *mem;
    int valid_fault = 0;

    if (current == 0 || regs == 0)
        return 0;

    if (regs->sstatus & SSTATUS_SPP)
        return 0;

    if (current->pgd == 0)
        return 0;

    fault_addr = regs->stval;
    page = fault_addr & ~(PAGE_SIZE - 1);

    if (regs->scause == SCAUSE_STORE_PAGE_FAULT) {
        int cow = vm_handle_cow_fault(current->pgd, page);

        if (cow > 0) {
            uart_puts("[Permission fault]: ");
            uart_hex(page);
            uart_puts("\r\n");
            return 1;
        }

        if (cow < 0) {
            uart_puts("[Segmentation fault]: Kill Process\r\n");
            terminate_process(current, -1);
            schedule();

            while (1)
                ;
        }
    }

    region = find_mmap_region(current, fault_addr);

    if (region) {
        valid_fault = mmap_fault_allowed(region, regs->scause);
        map_flags = mmap_prot_to_pte_flags(region->prot);
    } else if (current->user_stack_size &&
               fault_addr >= current->user_stack &&
               fault_addr < current->user_stack + current->user_stack_size &&
               (regs->scause == SCAUSE_LOAD_PAGE_FAULT ||
                regs->scause == SCAUSE_STORE_PAGE_FAULT)) {
        valid_fault = 1;
        map_flags = PROT_USER_RW;
    } else {
        map_flags = 0;
    }

    if (!valid_fault || vm_get_mapping(current->pgd, page, 0, 0)) {
        uart_puts("[Segmentation fault]: Kill Process\r\n");
        terminate_process(current, -1);
        schedule();

        while (1)
            ;
    }

    mem = allocate(PAGE_SIZE);
    if (mem == 0) {
        uart_puts("[Segmentation fault]: Kill Process\r\n");
        terminate_process(current, -1);
        schedule();

        while (1)
            ;
    }

    kmemset_local(mem, 0, PAGE_SIZE);
    map_pages(current->pgd, page, PAGE_SIZE, virt_to_phys_ptr(mem), map_flags);

    uart_puts("[Translation fault]: ");
    uart_hex(page);
    uart_puts("\r\n");

    return 1;
}

