#ifndef VIDEO_H
#define VIDEO_H

#include <stddef.h>
#include "allocate.h"

#define FB_BASE 0x7f700000UL
#define FB_WIDTH 1920UL
#define FB_HEIGHT 1080UL
#define FB_BPP 4UL
#define FB_SIZE (FB_WIDTH * FB_HEIGHT * FB_BPP)

void video_reserve_framebuffer(void);
void video_display(unsigned int *bmp_image, unsigned int width, unsigned int height);

#endif

