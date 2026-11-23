#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/memory.hp"

static void dump(const char* tag, unsigned char* p) {
    printf("%s:", tag);
    for (int i=0;i<16;i++) printf(" %02X", *(p+i));
    printf("\n");
}
int main(void){
    void *dst = stdlib_alloc(64);
    void *src = stdlib_alloc(64);
    unsigned long long gen = stdlib_mem_token(dst);
    memset(dst, 0xA5, 64); memset(src, 0x5A, 64);
    dump("before", (unsigned char*)dst);
    printf("rc0=%d\n", stdlib_mem_set(dst, 65u, 0, 8u, gen, (error*)0));
    dump("v0", (unsigned char*)dst);
    printf("rc1=%d\n", stdlib_mem_set(dst, 60u, 0, 8u, gen, (error*)0));
    dump("v1", (unsigned char*)dst);
    printf("rc3=%d\n", stdlib_mem_copy(dst, 0u, src, 65u, 8u, gen, (error*)0));
    dump("v3", (unsigned char*)dst);
    printf("rc5=%d\n", stdlib_mem_move(dst, SIZE_MAX, src, 0u, 8u, gen, (error*)0));
    dump("v5", (unsigned char*)dst);
    printf("rc7=%d\n", stdlib_mem_copy(dst, 0u, src, 0u, 8u, gen+1u, (error*)0));
    dump("v7", (unsigned char*)dst);
    stdlib_release(dst); stdlib_release(src);
    return 0;
}
