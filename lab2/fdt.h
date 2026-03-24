#ifndef FDT_H
#define FDT_H

#include <stdint.h>
#include <stddef.h>

#define FDT_BEGIN_NODE 0x00000001
#define FDT_END_NODE   0x00000002
#define FDT_PROP       0x00000003
#define FDT_NOP        0x00000004
#define FDT_END        0x00000009
#define FDT_MAGIC      0xd00dfeed

struct fdt_header {
    uint32_t magic;
    uint32_t totalsize;// whole fdt size
    uint32_t off_dt_struct; //structure block: used to put the Node token
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap; //describe where is the physical mem that can not be use
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
};

static inline uint32_t bswap32(uint32_t x) {
    return ((x & 0x000000FFU) << 24) |
           ((x & 0x0000FF00U) << 8)  |
           ((x & 0x00FF0000U) >> 8)  |
           ((x & 0xFF000000U) >> 24);
}

static inline uint64_t bswap64(uint64_t x) {
    return ((uint64_t)bswap32((uint32_t)(x >> 32))) |
           ((uint64_t)bswap32((uint32_t)(x & 0xFFFFFFFFULL)) << 32);
}

static inline const void* align_up(const void* ptr, size_t align) {
    return (const void*)(((uintptr_t)ptr + align - 1) & ~(align - 1));
}

int fdt_path_offset(const void* fdt, const char* path);
const void* fdt_getprop(const void* fdt, int nodeoffset, const char* name, int* lenp);
unsigned long get_uart_base(const void *fdt);
unsigned long get_initrd_start(const void *fdt);
unsigned long get_initrd_end(const void *fdt);

#endif
