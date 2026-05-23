#ifndef STARTUP_ALLOC_H
#define STARTUP_ALLOC_H

#include <stddef.h>
#include "allocate.h"

void startup_allocator_init(phys_addr_t base, size_t size);
void startup_reserve(phys_addr_t base, size_t size);
void *startup_alloc(size_t size, size_t align);

#endif
