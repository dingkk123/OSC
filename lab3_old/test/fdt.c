#include "fdt.h"
#include "allocate.h"
#include "allocate.h"

static int kstrlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static int kstrcmp(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b)
            return (unsigned char)*a - (unsigned char)*b;
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static int kstrncmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i])
            return (unsigned char)a[i] - (unsigned char)b[i];
        if (a[i] == '\0')
            return 0;
    }
    return 0;
}

static const char *kstrchr(const char *s, char c) {
    while (*s) {
        if (*s == c)
            return s;
        s++;
    }
    return 0;
}

static void *kmemcpy(void *dst, const void *src, int n) {
    char *d = (char *)dst;
    const char *s = (const char *)src;
    for (int i = 0; i < n; i++)
        d[i] = s[i];
    return dst;
}

static int node_name_match(const char *node_name, const char *path_part) {
    int len = kstrlen(path_part);

    if (kstrncmp(node_name, path_part, len) != 0)
        return 0;

    if (node_name[len] == '\0')
        return 1;

    if (node_name[len] == '@')
        return 1;

    return 0;
}

int fdt_path_offset(const void* fdt, const char* path) {
    const struct fdt_header* hdr = (const struct fdt_header*)fdt;

    if (bswap32(hdr->magic) != FDT_MAGIC)
        return -1;

    const char* struct_block =
        (const char*)fdt + bswap32(hdr->off_dt_struct);
    const char* p = struct_block;

    char names[64][128];
    int depth = -1;

    while (1) {
        int offset = (int)(p - struct_block);
        uint32_t token = bswap32(*(const uint32_t*)p);
        p += 4;

        if (token == FDT_BEGIN_NODE) {
            const char* name = p;
            int len = kstrlen(name);

            depth++;
            if (depth >= 64)
                return -1;

            int copy_len = len;
            if (copy_len > 127)
                copy_len = 127;
            kmemcpy(names[depth], name, copy_len);
            names[depth][copy_len] = '\0';

            const char *pp = path;
            int matched = 1;

            if (depth == 0) {
                matched = (kstrcmp(path, "/") == 0);
            } else {
                for (int i = 1; i <= depth; i++) {
                    while (*pp == '/')
                        pp++;

                    if (*pp == '\0') {
                        matched = 0;
                        break;
                    }

                    const char *slash = kstrchr(pp, '/');
                    int part_len = slash ? (int)(slash - pp) : kstrlen(pp);

                    char part[128];
                    if (part_len >= (int)sizeof(part))
                        return -1;

                    kmemcpy(part, pp, part_len);
                    part[part_len] = '\0';

                    if (!node_name_match(names[i], part)) {
                        matched = 0;
                        break;
                    }

                    pp += part_len;
                    if (*pp == '/')
                        pp++;
                }

                if (*pp != '\0')
                    matched = 0;
            }

            if (matched)
                return offset;

            p = (const char*)align_up(p + len + 1, 4);
        }
        else if (token == FDT_END_NODE) {
            depth--;
        }
        else if (token == FDT_PROP) {
            uint32_t len = bswap32(*(const uint32_t*)p);
            p += 4;
            p += 4;
            p = (const char*)align_up(p + len, 4);
        }
        else if (token == FDT_NOP) {
        }
        else if (token == FDT_END) {
            break;
        }
        else {
            return -1;
        }
    }

    return -1;
}

const void* fdt_getprop(const void* fdt,
                        int nodeoffset,
                        const char* name,
                        int* lenp) {
    const struct fdt_header* hdr = (const struct fdt_header*)fdt;

    if (bswap32(hdr->magic) != FDT_MAGIC)
        return 0;

    const char* struct_block =
        (const char*)fdt + bswap32(hdr->off_dt_struct);
    const char* strings_block =
        (const char*)fdt + bswap32(hdr->off_dt_strings);

    const char* p = struct_block + nodeoffset;

    uint32_t token = bswap32(*(const uint32_t*)p);
    if (token != FDT_BEGIN_NODE)
        return 0;
    p += 4;

    int namelen = kstrlen(p);
    p = (const char*)align_up(p + namelen + 1, 4);

    while (1) {
        token = bswap32(*(const uint32_t*)p);
        p += 4;

        if (token == FDT_PROP) {
            uint32_t len = bswap32(*(const uint32_t*)p);
            p += 4;
            uint32_t nameoff = bswap32(*(const uint32_t*)p);
            p += 4;

            const char* prop_name = strings_block + nameoff;
            const void* prop_data = p;

            if (kstrcmp(prop_name, name) == 0) {
                if (lenp)
                    *lenp = (int)len;
                return prop_data;
            }

            p = (const char*)align_up(p + len, 4);
        }
        else if (token == FDT_NOP) {
        }
        else if (token == FDT_BEGIN_NODE) {
            return 0;
        }
        else if (token == FDT_END_NODE) {
            return 0;
        }
        else if (token == FDT_END) {
            return 0;
        }
        else {
            return 0;
        }
    }
}

void reserve_fdt_reserved_memory(const void *fdt) {
    int rm_node = fdt_path_offset(fdt, "/reserved-memory");
    if (rm_node < 0)
        return;

    const struct fdt_header *hdr = (const struct fdt_header *)fdt;
    const char *struct_block = (const char *)fdt + bswap32(hdr->off_dt_struct);

    const char *p = struct_block + rm_node;

    uint32_t token = bswap32(*(const uint32_t *)p);
    if (token != FDT_BEGIN_NODE)
        return;

    p += 4;
    int namelen = kstrlen(p);
    p = (const char *)align_up(p + namelen + 1, 4);

    int depth = 0;

    while (1) {
        const char *token_ptr = p;
        token = bswap32(*(const uint32_t *)p);
        p += 4;

        if (token == FDT_BEGIN_NODE) {
            depth++;

            if (depth == 1) {
                int child_off = (int)(token_ptr - struct_block);

                int len = 0;
                const uint32_t *reg =
                    (const uint32_t *)fdt_getprop(fdt, child_off, "reg", &len);

                if (reg != 0 && len >= 16) {
                    unsigned long base =
                        ((unsigned long)bswap32(reg[0]) << 32) |
                        (unsigned long)bswap32(reg[1]);
                    unsigned long size =
                        ((unsigned long)bswap32(reg[2]) << 32) |
                        (unsigned long)bswap32(reg[3]);

                    memory_reserve(base, size);
                }
            }

            const char *name = p;
            int len = kstrlen(name);
            p = (const char *)align_up(p + len + 1, 4);
        }
        else if (token == FDT_PROP) {
            uint32_t len = bswap32(*(const uint32_t *)p);
            p += 4;
            p += 4;
            p = (const char *)align_up(p + len, 4);
        }
        else if (token == FDT_END_NODE) {
            if (depth == 0)
                break;
            depth--;
        }
        else if (token == FDT_NOP) {
        }
        else if (token == FDT_END) {
            break;
        }
        else {
            break;
        }
    }
}

unsigned long get_uart_base(const void *fdt) {
    int node = fdt_path_offset(fdt, "/soc/serial");
    if (node < 0)
        node = fdt_path_offset(fdt, "/soc/uart");
    if (node < 0)
        return 0;

    int len = 0;
    const uint32_t *reg = (const uint32_t *)fdt_getprop(fdt, node, "reg", &len);
    if (!reg || len < 16)
        return 0;

    unsigned long addr = ((unsigned long)bswap32(reg[0]) << 32) |
                         (unsigned long)bswap32(reg[1]);
    return addr;
}

static unsigned long fdt_get_addr_value(const void *prop, int len) {
    if (!prop)
        return 0;

    if (len >= 8) {
        const uint32_t *p = (const uint32_t *)prop;
        return ((unsigned long)bswap32(p[0]) << 32) |
               (unsigned long)bswap32(p[1]);
    }

    if (len >= 4) {
        const uint32_t *p = (const uint32_t *)prop;
        return (unsigned long)bswap32(p[0]);
    }

    return 0;
}

unsigned long get_initrd_start(const void *fdt) {
    int node = fdt_path_offset(fdt, "/chosen");
    if (node < 0)
        return 0;

    int len = 0;
    const void *prop = fdt_getprop(fdt, node, "linux,initrd-start", &len);
    return fdt_get_addr_value(prop, len);
}

unsigned long get_initrd_end(const void *fdt) {
    int node = fdt_path_offset(fdt, "/chosen");
    if (node < 0)
        return 0;

    int len = 0;
    const uint32_t *prop =
        (const uint32_t *)fdt_getprop(fdt, node, "linux,initrd-end", &len);
    if (prop == 0 || len < 8)
        return 0;

    return ((unsigned long)bswap32(prop[0]) << 32) |
           (unsigned long)bswap32(prop[1]);
}
unsigned long get_memory_base(const void *fdt) {
    int node = fdt_path_offset(fdt, "/memory");
    if (node < 0)
        return 0;

    int len = 0;
    const uint32_t *reg = (const uint32_t *)fdt_getprop(fdt, node, "reg", &len);
    if (reg == 0 || len < 16)
        return 0;

    unsigned long base = ((unsigned long)bswap32(reg[0]) << 32) |
                         (unsigned long)bswap32(reg[1]);
    return base;
}

unsigned long get_memory_size(const void *fdt) {
    int node = fdt_path_offset(fdt, "/memory");
    if (node < 0)
        return 0;

    int len = 0;
    const uint32_t *reg = (const uint32_t *)fdt_getprop(fdt, node, "reg", &len);
    if (reg == 0 || len < 16)
        return 0;

    unsigned long size = ((unsigned long)bswap32(reg[2]) << 32) |
                         (unsigned long)bswap32(reg[3]);
    return size;
}

void reserve_dtb(const void *fdt) {
    const struct fdt_header *hdr = (const struct fdt_header *)fdt;
    unsigned long dtb_start = (unsigned long)fdt;
    unsigned long dtb_size = bswap32(hdr->totalsize);

    memory_reserve(dtb_start, dtb_size);
}

void reserve_initrd(const void *fdt) {
    unsigned long start = get_initrd_start(fdt);
    unsigned long end = get_initrd_end(fdt);

    if (start != 0 && end > start) {
        memory_reserve(start, end - start);
    }
}

extern char _start[];
extern char _end[];

void reserve_kernel_image(void) {
    unsigned long start = (unsigned long)_start;
    unsigned long end = (unsigned long)_end;

    if (end > start) {
        memory_reserve(start, end - start);
    }
}
