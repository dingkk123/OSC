#include "video.h"
#include "utils.h"
#include "vm.h"

#define CACHE_BLOCK_SIZE 64UL //from exercise

static void cbo_flush_raw(unsigned long addr) {
    asm volatile(
        "mv a0, %0\n\t"
        ".word 0x0025200F"
        :
        : "r"(addr)
        : "memory", "a0");
}

static void flush_dcache(void *addr, unsigned long len) {
    unsigned long start;
    unsigned long end;

    if (len == 0)
        return;

    start = (unsigned long)addr & ~(CACHE_BLOCK_SIZE - 1);
    end = (unsigned long)addr + len;

    asm volatile("fence rw, rw" ::: "memory");

    for (unsigned long line = start; line < end; line += CACHE_BLOCK_SIZE)
        cbo_flush_raw(line);

    asm volatile("fence rw, rw" ::: "memory");
}

void video_reserve_framebuffer(void) {
    memory_reserve(FB_BASE, FB_SIZE);
}

unsigned long video_framebuffer_size(void) {
    return FB_SIZE;
}

long video_framebuffer_write(unsigned long offset, const void *buf, unsigned long len) {
    void *fb;
    unsigned long writable;

    if (buf == 0)
        return -1;
    if (len == 0)
        return 0;
    if (offset >= FB_SIZE)
        return 0;

    writable = FB_SIZE - offset;
    if (writable > len)
        writable = len;

    fb = (void *)(phys_to_virt(FB_BASE) + offset);
    kmemcpy_local(fb, buf, writable);
    flush_dcache(fb, writable);
    return writable;
}

int video_framebuffer_get_info(struct framebuffer_info *info) {
    if (info == 0)
        return -1;

    info->width = FB_WIDTH;
    info->height = FB_HEIGHT;
    info->bpp = FB_BPP;
    return 0;
}

void video_display(unsigned int *bmp_image, unsigned int width, unsigned int height) {
    unsigned int *fb = (unsigned int *)phys_to_virt(FB_BASE);
    unsigned int draw_width;
    unsigned int draw_height;
    unsigned int start_x;
    unsigned int start_y;

    if (bmp_image == 0 || width == 0 || height == 0)
        return;

    draw_width = width;
    draw_height = height;

    if (draw_width > FB_WIDTH)
        draw_width = FB_WIDTH;

    if (draw_height > FB_HEIGHT)
        draw_height = FB_HEIGHT;

    start_x = (FB_WIDTH - draw_width) / 2;
    start_y = (FB_HEIGHT - draw_height) / 2;

    for (unsigned int y = 0; y < draw_height; y++) {
        void *dst = fb + (start_y + y) * FB_WIDTH + start_x;
        void *src = bmp_image + y * width;
        unsigned long len = draw_width * sizeof(unsigned int);

        kmemcpy_local(dst, src, len);
        flush_dcache(dst, len);
    }
}

