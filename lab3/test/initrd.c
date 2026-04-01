#include "initrd.h"

extern void uart_putc(char c);
extern void uart_puts(const char *s);

static int str_equal(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b)
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static int hextoi(const char* s, int n) {
    int r = 0;
    while (n-- > 0) {
        r <<= 4;
        if (*s >= '0' && *s <= '9')
            r += *s - '0';
        else if (*s >= 'A' && *s <= 'F')
            r += *s - 'A' + 10;
        else if (*s >= 'a' && *s <= 'f')
            r += *s - 'a' + 10;
        s++;
    }
    return r;
}

static int align_int(int n, int byte) {
    return (n + byte - 1) & ~(byte - 1);
}

static void uart_put_dec(unsigned int x) {
    char buf[16];
    int i = 0;

    if (x == 0) {
        uart_putc('0');
        return;
    }

    while (x > 0) {
        buf[i++] = '0' + (x % 10);
        x /= 10;
    }

    while (i > 0) {
        uart_putc(buf[--i]);
    }
}

//each entry structure : [header][pathname][padding][data][padding]
void initrd_list(const void* rd) {
    const char *p = (const char *)rd;// p = address of initrd start

    while (1) {
        const struct cpio_t *h = (const struct cpio_t *)p;

        char magic[7];
        for (int i = 0; i < 6; i++){
            magic[i] = h->magic[i];
        }
        magic[6] = '\0';

        if (!str_equal(magic, "070701")) {
            uart_puts("Bad magic\r\n");
            return;
        }

        int namesize = hextoi(h->namesize, 8);
        int filesize = hextoi(h->filesize, 8);
        const char *name = p + sizeof(struct cpio_t); // beacuse pathname is behind the header

        if (str_equal(name, "TRAILER!!!"))
            break;

        uart_put_dec(filesize);
        uart_puts(" ");
        uart_puts(name);
        uart_puts("\r\n");

        //split and add is because it has padding behind pathname
        //[header][pathname][padding][data][padding]
        int header_and_name = align_int(sizeof(struct cpio_t) + namesize, 4);
        int data_and_padding = align_int(filesize, 4);
        p = p + header_and_name + data_and_padding;
    }
}

void initrd_cat(const void* rd, const char* filename) {
    const char *p = (const char *)rd;

    while (1) {
        const struct cpio_t *h = (const struct cpio_t *)p;

        char magic[7];
        for (int i = 0; i < 6; i++){
            magic[i] = h->magic[i];
        }
        magic[6] = '\0';

        if (!str_equal(magic, "070701")) {
            uart_puts("Bad magic\r\n");
            return;
        }

        int namesize = hextoi(h->namesize, 8);
        int filesize = hextoi(h->filesize, 8);
        const char *name = p + sizeof(struct cpio_t);

        if (str_equal(name, "TRAILER!!!")) {
            uart_puts(filename);
            uart_puts(": file not found\r\n");
            return;
        }

        if (str_equal(name, filename)) {
            int header_and_name = align_int(sizeof(struct cpio_t) + namesize, 4);
            const char *data = p + header_and_name;

            for (int i = 0; i < filesize; i++) {
                char c = data[i];
                if (c == '\n') {
                    uart_putc('\r');
                    uart_putc('\n');
                } else {
                    uart_putc(c);
                }
            }
            uart_puts("\r\n");
            return;
        }

        int header_and_name = align_int(sizeof(struct cpio_t) + namesize, 4);
        int data_and_padding = align_int(filesize, 4);
        p = p + header_and_name + data_and_padding;
    }
}
