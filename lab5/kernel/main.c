#include "fdt.h"
#include "initrd.h"
#include "uart.h"
#include "allocate.h"
#include "startup_alloc.h"
#include "timer.h"
#include "utils.h"
#include "interrupt.h"
#include "thread.h"
#include "process.h"


extern char _start[];
extern char _end[];

extern void ret_from_exception(void);

#define STACK_SIZE 0x1000
#define SSTATUS_SPIE (1UL << 5)



/*
debug start

static volatile unsigned long timer_irq_count = 0;
static volatile unsigned long external_irq_count = 0;
static volatile unsigned long uart_irq_count = 0;
static volatile unsigned long last_irq = 0;

debug end
*/

unsigned long initrd_base = 0;


#define MAX_TIMEOUT_MSGS 16
#define MAX_TIMEOUT_MSG_LEN 64

struct timeout_msg {
    volatile int used;
    volatile int ready;
    char msg[MAX_TIMEOUT_MSG_LEN];
};


static struct timeout_msg timeout_msgs[MAX_TIMEOUT_MSGS];

static volatile int boot_time_ready = 0;
static volatile unsigned long boot_time_to_print = 0;
static unsigned long boot_start_sec = 0;

//to get the next expire time among active timers, if no active timer, return 0
static void boot_time_callback(void *arg) {
    (void)arg;

    boot_time_to_print = timer_now_sec() - boot_start_sec;
    boot_time_ready = 1;

    (void)timer_add(boot_time_callback, 0, 2);//每兩秒更新一次boot time

}

static void flush_boot_time(void) {
    unsigned long flags;
    //unsigned long t;

    flags = irq_save();

    if (!boot_time_ready) { //如果boot_time_ready還沒被boot_time_callback設為1，代表boot_time_to_print裡的boot time還不是最新的，可以不用印
        irq_restore(flags);
        return;
    }

    //t = boot_time_to_print;
    boot_time_ready = 0;

    irq_restore(flags);

    //uart_puts("\r\nboot time: ");
    //uart_put_dec(t);
    //uart_puts("\r\n");
}



static void timeout_callback(void *arg) {
    struct timeout_msg *tm = (struct timeout_msg *)arg;
    tm->ready = 1;
}


static void flush_timeout_messages(void) {
    char msg[MAX_TIMEOUT_MSG_LEN];

    for (int i = 0; i < MAX_TIMEOUT_MSGS; i++) {
        unsigned long flags = irq_save();

        if (!timeout_msgs[i].used || !timeout_msgs[i].ready) {
            irq_restore(flags);
            continue;
        }

        str_copy_limit(msg, timeout_msgs[i].msg, MAX_TIMEOUT_MSG_LEN);

        timeout_msgs[i].ready = 0;
        timeout_msgs[i].used = 0;
        timeout_msgs[i].msg[0] = '\0';

        irq_restore(flags);

        uart_puts("\r\n");
        uart_puts(msg);
        uart_puts("\r\n");
    }
}



/*
int exec(const char* filename) {
    if (initrd_base == 0)
        return -1;

    char* p = (char*)initrd_base;

    while (memcmp(p + sizeof(struct cpio_t), "TRAILER!!!", 10) != 0) {
        struct cpio_t* hdr = (struct cpio_t*)p;

        int namesize = hextoi(hdr->namesize, 8);
        int filesize = hextoi(hdr->filesize, 8);
        int headsize = align(sizeof(struct cpio_t) + namesize, 4);
        int datasize = align(filesize, 4);

        char* name = p + sizeof(struct cpio_t);
        char* data = p + headsize;

        if (memcmp(name, filename, namesize) == 0) {
            unsigned long code_size = align(filesize, 0x1000);
            void *code = allocate(code_size);
           
            void* stack = alloc_page(); // allocate one page for user stack

            if (code == 0 || stack == 0)
                return -1;

            kmemset_local(code, 0, code_size); // clear the code page, 0x1000=>4096=one page size
            kmemcpy_local(code, data, filesize);
            interrupt_set_user_program_base((unsigned long)code);


            unsigned long user_sp = (unsigned long)stack + STACK_SIZE;
            unsigned long kernel_sp;

            asm volatile("mv %0, sp" : "=r"(kernel_sp)); //move the current kernel stack pointer to kernel_sp

            struct pt_regs* regs = (struct pt_regs*)(kernel_sp - sizeof(struct pt_regs));

            kmemset_local(regs, 0, sizeof(struct pt_regs));// clear the pt_regs struct
            
            //setup values which will be used in ret_from_exception to set the user program's context
            regs->sp = user_sp;
            regs->sepc = (unsigned long)code;
            regs->sstatus = SSTATUS_SPIE; //enable interrupt in user mode after sret

            asm volatile(
                "mv sp, %0\n"
                "j ret_from_exception\n"
                :
                : "r"(regs)
                : "memory"
            );
            while (1) {}

        }

        p += headsize + datasize;
    }

    return -1;
}
*/

void foo(void) {
    for (int i = 0; i < 5; i++) {
        uart_puts("Thread id: ");
        uart_put_dec(get_current()->pid);
        uart_puts(" ");
        uart_put_dec(i);
        uart_puts("\r\n");

        for (volatile int j = 0; j < 100000000; j++)
            ;

        schedule();
    }

    thread_exit();
}

/*
debug start
*/

static void shell_thread(void) {
    char buf[100];
    int idx = 0;

    uart_puts("opi-rv2> ");

    while (1) {
        flush_boot_time();
        flush_timeout_messages();

        char c = uart_getc();

        if (c == 0) {
            schedule();
            continue;
        }

        if (c == '\r' || c == '\n') {
            buf[idx] = '\0';
            uart_puts("\r\n");

            if (buf[0] == '\0') {
            }
            else if (str_equal(buf, "help")) {
                uart_puts("Available commands:\r\n");
                uart_puts("  help        - show all commands.\r\n");
                uart_puts("  hello       - print Hello world.\r\n");
                uart_puts("  ls          - list the files.\r\n");
                uart_puts("  cat         - demonstrate the txt content.\r\n");
                uart_puts("  exec <file> - run user program.\r\n");
                uart_puts("  settimeout  - show text after x sec.\r\n");
            }
            else if (str_equal(buf, "hello")) {
                uart_puts("Hello World from second kernel.\r\n");
            }
            else if (str_equal(buf, "ls")) {
                if (initrd_base == 0) {
                    uart_puts("initrd not found\r\n");
                } else {
                    initrd_list((const void *)initrd_base);
                }
            }
            else if (str_prefix(buf, "cat")) {
                char *filename = skip_space(buf + 3);

                if (*filename == '\0') {
                    uart_puts("Usage: cat <filename>\r\n");
                } else if (initrd_base == 0) {
                    uart_puts("initrd not found\r\n");
                } else {
                    initrd_cat((const void *)initrd_base, filename);
                }
            }
            else if (str_prefix(buf, "exec")) {
                char *filename = skip_space(buf + 4);

                if (*filename == '\0') {
                    uart_puts("Usage: exec <filename>\r\n");
                } else {
                    uart_puts("shell thread before exec\r\n");
                    struct task_struct *child = process_create(filename, get_current());
                    uart_puts("shell thread after exec\r\n");
                    if (child == 0) {
                        uart_puts("Failed to exec user program!\r\n");
                    } else {
                        sys_waitpid(child->pid);
                        uart_puts("shell thread after waitpid\r\n");
                    }
                }
            }
            else if (str_prefix(buf, "settimeout")) {
                char *p = skip_space(buf + 10); //point to the first char of seconds
                int sec = 0;
                int slot = -1;

                if (*p == '\0') {
                    uart_puts("Usage: settimeout <seconds> <message>\r\n");
                } else {
                    while (*p >= '0' && *p <= '9') {
                        sec = sec * 10 + (*p - '0');
                        p++;
                    }

                    p = skip_space(p);//point to the first char of message

                    if (*p == '\0') {
                        uart_puts("Usage: settimeout <seconds> <message>\r\n");
                    } else {
                        for (int i = 0; i < MAX_TIMEOUT_MSGS; i++) {
                            if (!timeout_msgs[i].used) {
                                slot = i;
                                break;
                            }
                        }
                        //失敗代表沒有inactive的timeout_msg slot可以用來存這個新的timeout
                        if (slot < 0) {
                            uart_puts("No timeout slot available\r\n");
                        } else {
                            unsigned long flags;

                            flags = irq_save();
                            timeout_msgs[slot].used = 1; //把這個slot標記為used，代表正在使用中
                            timeout_msgs[slot].ready = 0;//把這個slot標記為not ready，代表還沒到時間不能印
                            str_copy_limit(timeout_msgs[slot].msg, p, MAX_TIMEOUT_MSG_LEN);
                            irq_restore(flags);

                            //if<0代表timer_add失敗，可能是沒有inactive的timer
                            if (timer_add(timeout_callback, &timeout_msgs[slot], sec) < 0) {
                                flags = irq_save();
                                timeout_msgs[slot].used = 0;
                                timeout_msgs[slot].ready = 0;
                                timeout_msgs[slot].msg[0] = '\0';
                                irq_restore(flags);

                                uart_puts("No timer slot available\r\n");
                            }

                        }

                    }
                }
            }
            else {
                uart_puts("Unknown command\r\n");
            }

            idx = 0;
            uart_puts("\r\nopi-rv2> ");
        } else {
            if (idx < 99) {
                uart_putc(c);
                buf[idx++] = c;
            }
        }
    }
}






void start_kernel(void *fdt) {
    unsigned long new_uart = get_uart_base(fdt);
    unsigned long initrd_start = get_initrd_start(fdt);

    if (new_uart == 0) {
        while (1) {}
    }

    uart_init_base(new_uart);

    uart_puts("Second kernel booted!\n");
    uart_puts("UART from FDT = ");
    uart_hex(new_uart);
    uart_puts("\n");

    uart_puts("INITRD_START = ");
    uart_hex(initrd_start);
    uart_puts("\n");

    unsigned long mem_base = get_memory_base(fdt);
    unsigned long mem_size = get_memory_size(fdt);
    size_t total_pages = mem_size / 4096;
    size_t frame_array_size = total_pages * sizeof(struct page);

    startup_allocator_init(mem_base, mem_size);

    uart_puts("MEM_BASE = ");
    uart_hex(mem_base);
    uart_puts("\n");

    uart_puts("MEM_SIZE = ");
    uart_hex(mem_size);
    uart_puts("\n");

    
    const struct fdt_header *hdr = (const struct fdt_header *)fdt;
    unsigned long dtb_size = bswap32(hdr->totalsize);
    startup_reserve((phys_addr_t)fdt, dtb_size);
    

    startup_reserve((phys_addr_t)_start, (unsigned long)_end - (unsigned long)_start);

    
    unsigned long initrd_start2 = get_initrd_start(fdt);
    unsigned long initrd_end = get_initrd_end(fdt);
    if (initrd_start2 != 0 && initrd_end > initrd_start2) {
        startup_reserve(initrd_start2, initrd_end - initrd_start2);
    }
    
    fdt_reserved_memory(fdt, 1); // 1 is startup reserve

    struct page *page_array = (struct page *)startup_alloc(frame_array_size, 4096);

    if (page_array == 0) {
        uart_puts("startup alloc frame array failed\r\n");
        while (1) {}
    }

    uart_puts("FRAME_ARRAY = ");
    uart_hex((unsigned long)page_array);
    uart_puts("\r\n");

    uart_puts("FRAME_ARRAY_SIZE = ");
    uart_put_dec(frame_array_size);
    uart_puts("\r\n");

    startup_reserve((phys_addr_t)page_array, frame_array_size);

    buddy_init(page_array, total_pages, mem_base);
    buddy_add_region(mem_base, mem_size);

    //uart_puts("memory reserve\r\n");
    //memory_reserve(0x80000000UL, 0x200000UL);
    uart_puts("reserve dtb\r\n");
    reserve_dtb(fdt);

    uart_puts("reserve kernel\r\n");
    reserve_kernel_image();

    uart_puts("reserve initrd\r\n");
    reserve_initrd(fdt);

    uart_puts("reserve reserved-memory\r\n");
    fdt_reserved_memory(fdt, 0); //0 is memory reserve

    memory_reserve((phys_addr_t)page_array, frame_array_size); // to reserve the page metadata region
    uart_puts("after all reserve\r\n");
    dump();

    kmem_init();
    
    uart_puts("FRAME_ARRAY = ");
    uart_hex((unsigned long)page_array);
    uart_puts("\r\n");

    uart_puts("FRAME_ARRAY_SIZE = ");
    uart_put_dec(frame_array_size);
    uart_puts("\r\n");
    
    uart_puts("MEM_BASE = ");
    uart_hex(mem_base);
    uart_puts("\n");

    uart_puts("MEM_SIZE = ");
    uart_hex(mem_size);
    uart_puts("\n");



/*
lab4 part
*/
    initrd_base = initrd_start;

    //interrupt_init(); // to initialize the interrupt controller (PLIC)
    //uart_enable_interrupt(); // to enable UART interrupt, so that when receive data, it would interrupt and handled in uart_handle_irq

    timer_init(); // to initialize the timer, set all timers to inactive
    timer_enable_interrupt(); // to enable timer interrupt, so that when timer expires, it would interrupt

    boot_start_sec = timer_now_sec();
    //uart_puts("boot time: 0\r\n");
    (void)timer_add(boot_time_callback, 0, 2); 

    struct task_struct *idle_task = thread_create(idle);
    asm volatile("mv tp, %0" : : "r"(idle_task));
    idle_task->state = THREAD_RUNNING;
    thread_add(idle_task);

    struct task_struct *shell_task = thread_create(shell_thread);
    thread_add(shell_task);

    idle();

    
}
