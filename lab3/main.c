#include "fdt.h"

extern unsigned long UART_BASE;
extern char uart_getc(void);
extern void uart_putc(char c);
extern void uart_puts(const char* s);
extern void uart_hex(unsigned long h);


#define SBI_EXT_SET_TIMER 0x0
#define SBI_EXT_SHUTDOWN  0x8
#define SBI_EXT_BASE      0x10
/*
#ifdef QEMU
#define KERNEL_LOAD_ADDR 0x82000000UL
#else
#define KERNEL_LOAD_ADDR 0x20000000UL
#endif
*/
#define KERNEL_LOAD_ADDR 0x20000000UL
#define BOOT_MAGIC 0x544F4F42UL

enum sbi_ext_base_fid {
    SBI_EXT_BASE_GET_SPEC_VERSION,
    SBI_EXT_BASE_GET_IMP_ID,
    SBI_EXT_BASE_GET_IMP_VERSION,
    SBI_EXT_BASE_PROBE_EXT,
    SBI_EXT_BASE_GET_MVENDORID,
    SBI_EXT_BASE_GET_MARCHID,
    SBI_EXT_BASE_GET_MIMPID,
};

struct sbiret {
    long error;
    long value;
};

struct sbiret sbi_ecall(int ext,
                        int fid,
                        unsigned long arg0,
                        unsigned long arg1,
                        unsigned long arg2,
                        unsigned long arg3,
                        unsigned long arg4,
                        unsigned long arg5) {
    struct sbiret ret;
    register unsigned long a0 asm("a0") = (unsigned long)arg0;
    register unsigned long a1 asm("a1") = (unsigned long)arg1;
    register unsigned long a2 asm("a2") = (unsigned long)arg2;
    register unsigned long a3 asm("a3") = (unsigned long)arg3;
    register unsigned long a4 asm("a4") = (unsigned long)arg4;
    register unsigned long a5 asm("a5") = (unsigned long)arg5;
    register unsigned long a6 asm("a6") = (unsigned long)fid;
    register unsigned long a7 asm("a7") = (unsigned long)ext;
    asm volatile("ecall"
                 : "+r"(a0), "+r"(a1)
                 : "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6), "r"(a7)
                 : "memory");
    ret.error = a0;
    ret.value = a1;
    return ret;
}

/**
 * sbi_get_spec_version() - Get the SBI specification version.
 *
 * Return: The current SBI specification version.
 * The minor number of the SBI specification is encoded in the low 24 bits,
 * with the major number encoded in the next 7 bits. Bit 31 must be 0.
 */
long sbi_get_spec_version(void) {
    // TODO: Implement this function
    struct sbiret ret;
    ret = sbi_ecall(SBI_EXT_BASE,
                    SBI_EXT_BASE_GET_SPEC_VERSION,
                    0, 0, 0, 0, 0, 0);
    return ret.value;
}

long sbi_get_impl_id(void) {
    // TODO: Implement this function
    struct sbiret ret;
    ret = sbi_ecall(SBI_EXT_BASE,
                    SBI_EXT_BASE_GET_IMP_ID,
                    0, 0, 0, 0, 0, 0);
    return ret.value;
}

long sbi_get_impl_version(void) {
    // TODO: Implement this function
    struct sbiret ret;
    ret = sbi_ecall(SBI_EXT_BASE,
                    SBI_EXT_BASE_GET_IMP_VERSION,
                    0, 0, 0, 0, 0, 0);
    return ret.value;
}


/**
 * sbi_probe_extension() - Check if an SBI extension ID is supported or not.
 * @extid: The extension ID to be probed.
 *
 * Return: 1 or an extension specific nonzero value if yes, 0 otherwise.
 */
long sbi_probe_extension(int extid) {
    // TODO: Implement this function
    struct sbiret ret;

    ret = sbi_ecall(SBI_EXT_BASE,
                    SBI_EXT_BASE_PROBE_EXT,
                    extid, 0, 0, 0, 0, 0);

    return ret.value;
}

unsigned int uart_get_u32(void) {
    unsigned int b0 = (unsigned char)uart_getc();
    unsigned int b1 = (unsigned char)uart_getc();
    unsigned int b2 = (unsigned char)uart_getc();
    unsigned int b3 = (unsigned char)uart_getc();

    return (b3 << 24) | (b2 << 16) | (b1 << 8) | (b0);
}

void load_kernel(void) {
    uart_puts("Wait the header\r\n");
    //[0x42][0x4F][0x4F][0x54][size byte0][size byte1][size byte2][size byte3]
    unsigned int magic = uart_get_u32();
    unsigned int size  = uart_get_u32();

    if (magic != BOOT_MAGIC) {
        uart_puts("Boot got wrong");
        uart_hex(magic);
        uart_puts("\r\n");
        return;
    }

    uart_puts("Kernel size: ");
    uart_hex(size);
    uart_puts("\r\n");

    unsigned char *p = (unsigned char *)KERNEL_LOAD_ADDR;

    uart_puts("Start loading kernel...\r\n");
    for (unsigned int i = 0; i < size; i++) {
        p[i] = (unsigned char)uart_getc();
    }

    uart_puts("Kernel loaded at ");
    uart_hex(KERNEL_LOAD_ADDR);
    uart_puts("\r\n");
}

void boot_kernel(void *fdt) {
    uart_puts("Booting kernel...\r\n");
    void (*kernel_entry)(void *) = (void (*)(void *))KERNEL_LOAD_ADDR;
    kernel_entry(fdt);

    while (1) {}
}

int str_equal(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == *b;
}

void start_kernel(void *fdt) {
    UART_BASE = get_uart_base(fdt);
    if (UART_BASE == 0) {
        while (1) {}
    }

    char buf[100];
    int idx=0;
    uart_puts("opi-rv2> ");
    while (1) {
        char c = uart_getc();
        if (c == '\r' || c == '\n'){
            buf[idx] = '\0';
            uart_puts("\r\n");
            
            if(buf[0] == '\0'){}
            else if(str_equal(buf, "hello")){
                uart_puts("Hello world.");
            }
            else if(str_equal(buf, "help")){
                uart_puts("Available commands:\r\n");
                uart_puts("  help        - show all commands.\r\n");
                uart_puts("  hello       - print Hello world.\r\n");
                uart_puts("  info        - print system info.\r\n");
                uart_puts("  load        - load kernel through UART.\r\n");
                uart_puts("  boot        - boot loaded kernel.\r\n");
            }
            else if(str_equal(buf, "info")){
                uart_puts("System information:\r\n");
                uart_puts("  OpenSBI specification version: ");
                uart_hex(sbi_get_spec_version());
                uart_puts("\r\n");
                
                uart_puts("  implementation ID: ");
                uart_hex(sbi_get_impl_id());
                uart_puts("\r\n");
                
                uart_puts("  implementation version: ");
                uart_hex(sbi_get_impl_version());
            }
            else if(str_equal(buf, "load")){
                load_kernel();
            }
            else if(str_equal(buf, "boot")){
                boot_kernel(fdt);
            }
            else{
                uart_puts("Unknown command: ");
                uart_puts(buf);
                uart_puts("\r\nUse help to get commands.");
            }
            idx = 0;
            uart_puts("\r\n");
            uart_puts("opi-rv2> ");
        } 
        else{
            if(idx < 99){
                  uart_putc(c);
                  buf[idx] = c;
                  idx++;
            }
        }
    }
}

