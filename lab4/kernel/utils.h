#ifndef UTILS_H
#define UTILS_H

int hextoi(const char* s, int n);
int align(int n, int byte);
int memcmp(const void* s1, const void* s2, int n);
void* alloc_page(void);

void kmemcpy_local(void* dst, const void* src, unsigned long n);
void kmemset_local(void* dst, int value, unsigned long n);

void str_copy_limit(char *dst, const char *src, int limit);
int str_equal(const char *a, const char *b);
int str_prefix(const char *s, const char *prefix);
char *skip_space(char *s);

#endif
