#ifndef RED_PROBE_LIB_H
#define RED_PROBE_LIB_H
#include <stddef.h>
int red_probe_add(int, int);
size_t red_probe_copy(char *, const char *);
void *red_probe_alloc(size_t);
#endif
