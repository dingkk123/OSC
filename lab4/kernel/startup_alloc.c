#include "startup_alloc.h"

#include <stddef.h>
#include <stdint.h>

#define STARTUP_MAX_RESERVED 32

struct startup_range {
    phys_addr_t start;
    phys_addr_t end;
};

static phys_addr_t startup_region_start = 0;
static phys_addr_t startup_region_end = 0;
static phys_addr_t startup_cur = 0;
static struct startup_range startup_reserved[STARTUP_MAX_RESERVED];
static size_t startup_reserved_count = 0;

static phys_addr_t align_up_addr(phys_addr_t value, size_t align) {
    phys_addr_t mask;

    if (align <= 1) {
        return value;
    }

    mask = (phys_addr_t)align - 1;
    return (value + mask) & ~mask;
}

static int ranges_overlap(phys_addr_t a_start,
                          phys_addr_t a_end,
                          phys_addr_t b_start,
                          phys_addr_t b_end) {
    return !(a_end <= b_start || b_end <= a_start);
}

// --------------------------------------------------
// init
// --------------------------------------------------
void startup_allocator_init(phys_addr_t base, size_t size) {
    startup_region_start = base;
    startup_region_end = base + size;
    startup_cur = base;
    startup_reserved_count = 0;
}

// --------------------------------------------------
// prereserve(get the range)
// --------------------------------------------------
void startup_reserve(phys_addr_t base, size_t size) {
    phys_addr_t start = base;
    phys_addr_t end = base + size;

    if (size == 0) {
        return;
    }

    if (startup_region_end <= startup_region_start) {
        return;
    }

    if (end <= startup_region_start || start >= startup_region_end) {
        return;
    }

    if (start < startup_region_start) {
        start = startup_region_start;
    }
    if (end > startup_region_end) {
        end = startup_region_end;
    }

    if (start >= end) {
        return;
    }

    if (startup_reserved_count >= STARTUP_MAX_RESERVED) {
        return;
    }

    startup_reserved[startup_reserved_count].start = start;
    startup_reserved[startup_reserved_count].end = end;
    startup_reserved_count++;
}

// --------------------------------------------------
// slloc the continuous mem range for page array
// --------------------------------------------------
void *startup_alloc(size_t size, size_t align) {
    phys_addr_t alloc_start;
    phys_addr_t alloc_end;
    phys_addr_t next_cur;
    size_t i;

    if (size == 0) {
        return 0;
    }

    if (startup_region_end <= startup_region_start) {
        return 0;
    }

    while (1) {
        alloc_start = align_up_addr(startup_cur, align);
        alloc_end = alloc_start + size;

        if (alloc_start < startup_cur || alloc_end < alloc_start) {
            return 0;
        }

        if (alloc_end > startup_region_end) {
            return 0;
        }

        next_cur = 0;
        for (i = 0; i < startup_reserved_count; i++) {
            if (ranges_overlap(alloc_start, alloc_end, startup_reserved[i].start, startup_reserved[i].end)) {
                if (startup_reserved[i].end > next_cur) {
                    next_cur = startup_reserved[i].end;
                }
            }
        }

        if (next_cur != 0) {
            startup_cur = next_cur;
            continue;
        }

        startup_cur = alloc_end;
        return (void *)alloc_start;
    }
}
