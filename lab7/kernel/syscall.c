#include "syscall.h"
#include "process.h"
#include "timer.h"
#include "video.h"




void syscall_handle(struct pt_regs *regs) {
    switch (regs->a7) {
    case SYS_GETPID:
        regs->a0 = sys_getpid();
        break;
    case SYS_UART_READ:
        regs->a0 = sys_uart_read((char *)regs->a0, regs->a1);
        break;
    case SYS_UART_WRITE:
        regs->a0 = sys_uart_write((const char *)regs->a0, regs->a1);
        break;
    case SYS_EXEC:
        regs->a0 = sys_exec((const char *)regs->a0, regs);
        break;
    case SYS_FORK:
        regs->a0 = sys_fork(regs);
        break;
    case SYS_WAITPID:
        regs->a0 = sys_waitpid(regs->a0);
        break;
    case SYS_EXIT:
        sys_exit(regs->a0);
        break;
    case SYS_STOP:
        regs->a0 = sys_stop(regs->a0);
        break;
    case SYS_DISPLAY:
        video_display((unsigned int *)regs->a0, regs->a1, regs->a2);
        regs->a0 = 0;
        break;
    case SYS_USLEEP:
        regs->a0 = timer_usleep(regs->a0);
        break;
    case SYS_SIGNAL:
        regs->a0 = sys_signal(regs->a0, (void (*)(void))regs->a1);
        break;
    case SYS_SIGRETURN:
        sys_sigreturn(regs);
        break;
    case SYS_KILL:
        regs->a0 = sys_kill(regs->a0, regs->a1);
        break;
    case SYS_MMAP:
        regs->a0 = sys_mmap((void *)regs->a0, regs->a1, regs->a2, regs->a3);
        break;
    case SYS_OPEN:
        regs->a0 = sys_open((const char *)regs->a0, regs->a1);
        break;
    case SYS_CLOSE:
        regs->a0 = sys_close(regs->a0);
        break;
    case SYS_READ:
        regs->a0 = sys_read(regs->a0, (void *)regs->a1, regs->a2);
        break;
    case SYS_WRITE:
        regs->a0 = sys_write(regs->a0, (const void *)regs->a1, regs->a2);
        break;
    case SYS_MKDIR:
        regs->a0 = sys_mkdir((const char *)regs->a0, regs->a1);
        break;
    case SYS_MOUNT:
        regs->a0 = sys_mount((const char *)regs->a0,
                             (const char *)regs->a1,
                             (const char *)regs->a2,
                             regs->a3,
                             (const void *)regs->a4);
        break;
    case SYS_CHDIR:
        regs->a0 = sys_chdir((const char *)regs->a0);
        break;
    case SYS_LSEEK64:
        regs->a0 = sys_lseek64(regs->a0, regs->a1, regs->a2);
        break;
    case SYS_IOCTL:
        regs->a0 = sys_ioctl(regs->a0, regs->a1, (void *)regs->a2);
        break;
    default:
        regs->a0 = -1;
        break;
    }
}

