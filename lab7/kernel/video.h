#ifndef VIDEO_H
#define VIDEO_H

#include <stddef.h>
#include "allocate.h"

//as same as exercise
#define FB_BASE 0x7f700000UL
#define FB_WIDTH 1920UL
#define FB_HEIGHT 1080UL
#define FB_BPP 4UL
#define FB_SIZE (FB_WIDTH * FB_HEIGHT * FB_BPP)

struct framebuffer_info {
    unsigned int width;
    unsigned int height;
    unsigned int bpp;
};

void video_reserve_framebuffer(void);
void video_display(unsigned int *bmp_image, unsigned int width, unsigned int height);
unsigned long video_framebuffer_size(void);
long video_framebuffer_write(unsigned long offset, const void *buf, unsigned long len);
int video_framebuffer_get_info(struct framebuffer_info *info);

#endif

