#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/memory.hp"

static int all_a5(const void *p) {
    const unsigned char *q = (const unsigned char *)p;
    for (int i=0;i<64;i++) if (q[i] != 0xA5u) return 0;
    return 1;
}
int main(void){
    void *dst = stdlib_alloc(64);
    void *src = stdlib_alloc(64);
    unsigned long long gen = stdlib_mem_token(dst);
    unsigned long long stale_tok;
    void *stale = stdlib_alloc(64);
    stale_tok = stdlib_mem_token(stale);
    stdlib_release(stale);
    memset(dst, 0xA5, 64); memset(src, 0x5A, 64);
    printf("v2 rc=%d all_a5=%d\n", stdlib_mem_set(dst, 0u, 0, 8u, stale_tok, (error*)0), all_a5(dst));
    printf("v4 rc=%d all_a5=%d\n", stdlib_mem_copy(dst, 0u, (const void*)0, 0u, 8u, gen, (error*)0), all_a5(dst));
    printf("v6 rc=%d all_a5=%d\n", stdlib_mem_set((void*)0, 0u, 0, 8u, gen, (error*)0), all_a5(dst));
    stdlib_release(dst); stdlib_release(src);
    return 0;
}
