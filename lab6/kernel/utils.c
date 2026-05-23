#include "allocate.h"
#include "utils.h"

int hextoi(const char* s, int n) {
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

int align(int n, int byte) {
    return (n + byte - 1) & ~(byte - 1);
}

int memcmp(const void* s1, const void* s2, int n) {
    const unsigned char* a = s1;
    const unsigned char* b = s2;

    while (n-- > 0) {
        if (*a != *b)
            return *a - *b;
        a++;
        b++;
    }

    return 0;
}

void* alloc_page(void) {
    struct page* pg = alloc_pages(0);

    if (pg == 0)
        return 0;

    pg->alloc_type = 1;
    pg->pool_idx = -1;

    return page_to_ptr(pg);
}

void kmemcpy_local(void* dst, const void* src, unsigned long n) {
    char* d = dst;
    const char* s = src;

    while (n-- > 0)
        *d++ = *s++;
}

void kmemset_local(void* dst, int value, unsigned long n) {//put value into dst for n bytes
    unsigned char* d = dst;

    while (n-- > 0)
        *d++ = value;
}

void str_copy_limit(char *dst, const char *src, int limit) {
    int i = 0;

    while (i < limit - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
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

int str_prefix(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s != *prefix) {
            return 0;
        }
        s++;
        prefix++;
    }
    return 1;
}

char *skip_space(char *s) {
    while (*s == ' ') {
        s++;
    }
    return s;
}
