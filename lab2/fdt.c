#include "fdt.h"

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

/*
dts
/ {
    soc {
        serial{
            compatible = "ns16550a";
            reg = <0x0 0x25000000 0x0 0x100>;
        };
    };
};

string block:
"compatible\0reg\0"
reg is at 11 in the string

structure block:

FDT_BEGIN_NODE ""                     // root
FDT_BEGIN_NODE "soc"
FDT_BEGIN_NODE "serial"

FDT_PROP
  len = 10
  nameoff = 0                         // 指到 strings block 的 "compatible"
  value = "ns16550a\0"
  padding

FDT_PROP
  len = 16
  nameoff = 11                        // 指到 strings block 的 "reg"
  value = <0x0 0x25000000 0x0 0x100>
  padding

FDT_END_NODE
FDT_END_NODE
FDT_END_NODE
FDT_END

*/

int fdt_path_offset(const void* fdt, const char* path) {
    const struct fdt_header* hdr = (const struct fdt_header*)fdt;

    if (bswap32(hdr->magic) != FDT_MAGIC)
        return -1;

    const char* struct_block = (const char*)fdt + bswap32(hdr->off_dt_struct);
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

            const char *pp = path; //path pointer
            int matched = 1;

            if (depth == 0) {
                matched = (kstrcmp(path, "/") == 0); // root define
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

            p = (const char*)align_up(p + len + 1, 4);//+1 to skip \0
        }
        else if (token == FDT_END_NODE) {
            depth--;
        }
/*
in the structure block
FFDT_PROP
  len = 16
  nameoff = 11 (指到 strings block 的 "reg")
  value = <0x0 0x25000000 0x0 0x100>
  padding
*/
        else if (token == FDT_PROP) {
            uint32_t len = bswap32(*(const uint32_t*)p);
            p += 4;// p = nameoff
            p += 4;// p = value
            p = (const char*)align_up(p + len, 4); //to skip value, so use len(len of value) to add
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

/*
dts
/ {
    soc {
        serial{
            compatible = "ns16550a";
            reg = <0x0 0x25000000 0x0 0x100>;
        };
    };
};

string block:
"compatible\0reg\0"
reg is at 11 in the string

structure block:

FDT_BEGIN_NODE ""                     // root
FDT_BEGIN_NODE "soc"
FDT_BEGIN_NODE "serial"

FDT_PROP
  len = 10
  nameoff = 0                         // 指到 strings block 的 "compatible"
  value = "ns16550a\0"
  padding

FDT_PROP
  len = 16
  nameoff = 11                        // 指到 strings block 的 "reg"
  value = <0x0 0x25000000 0x0 0x100>
  padding

FDT_END_NODE
FDT_END_NODE
FDT_END_NODE
FDT_END

*/
const void* fdt_getprop(const void* fdt,
                        int nodeoffset,
                        const char* name,
                        int* lenp) {
    const struct fdt_header* hdr = (const struct fdt_header*)fdt;
    //ex : get the serial@'s 'reg' property

    if (bswap32(hdr->magic) != FDT_MAGIC)
        return 0;

    const char* struct_block = (const char*)fdt + bswap32(hdr->off_dt_struct);
    const char* strings_block = (const char*)fdt + bswap32(hdr->off_dt_strings);

    const char* p = struct_block + nodeoffset;//go to structure block
    //ex.now  p = FDT_BEGIN_NODE
    uint32_t token = bswap32(*(const uint32_t*)p);//token = FDT_XX_NODE
//because it has just find the path offset we want to find so it needs to be the begin node
    if (token != FDT_BEGIN_NODE) 
        return 0;
    p += 4;// equal the node name "serial@"

    int namelen = kstrlen(p);
    p = (const char*)align_up(p + namelen + 1, 4); // to skip the "serial@"

    while (1) {
        token = bswap32(*(const uint32_t*)p); // p = FDT_XXX_NODE
        p += 4; 
/*
in the structure block
FFDT_PROP
  len = 16
  nameoff = 11 (指到 strings block 的 "reg")
  value = <0x0 0x25000000 0x0 0x100>
  padding
*/

        if (token == FDT_PROP) {
            uint32_t len = bswap32(*(const uint32_t*)p);
            p += 4;
            uint32_t nameoff = bswap32(*(const uint32_t*)p);
            p += 4;

            const char* prop_name = strings_block + nameoff;// get the prop name in string block
            const void* prop_data = p;//get the prop value

            //if prop_name == name, lenp = prop_len
            if (kstrcmp(prop_name, name) == 0) {
                if (lenp != NULL){
                    *lenp = (int)len;
                }
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

unsigned long get_uart_base(const void *fdt) {
    int node = fdt_path_offset(fdt, "/soc/serial");
    if (node < 0)
        node = fdt_path_offset(fdt, "/soc/uart");
    if (node < 0)
        return 0;

    int len = 0;
    const uint32_t *reg = (const uint32_t *)fdt_getprop(fdt, node, "reg", &len);
    if (reg == NULL || len < 16)
        return 0;

    unsigned long addr = ((unsigned long)bswap32(reg[0]) << 32) |
                         (unsigned long)bswap32(reg[1]);
    return addr;
}

static unsigned long fdt_get_addr_value(const void *prop, int len) {
    if (prop == NULL)
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
