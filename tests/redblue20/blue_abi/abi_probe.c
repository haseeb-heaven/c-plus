/* BLUE20 ordinary-C ABI probe: LP64 pointer size + public struct layouts.
 * Compiled by /usr/bin/clang -std=c11 (plain C, no C+ headers). */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "cplus_libc.h"
#include "cplus_threads.h"
#include "cplus_uchar.h"
#include "safe_alloc.h"
#include "safe_stats.h"
#include "safe_platform.h"
#include "safe_error.h"

static int pass = 0;
static int fail = 0;

#define CHK(cond, name)                                                        \
    do {                                                                       \
        if (cond) {                                                            \
            ++pass;                                                            \
            printf("OK   %s\n", name);                                         \
        } else {                                                               \
            ++fail;                                                            \
            printf("FAIL %s\n", name);                                         \
        }                                                                      \
    } while (0)

int main(void)
{
    /* Ordinary C ABI: LP64, 8-byte data and function pointers. */
    CHK(sizeof(void *) == 8, "sizeof(void*)==8");
    CHK(sizeof(char *) == 8, "sizeof(char*)==8");
    CHK(sizeof(int *) == 8, "sizeof(int*)==8");
    CHK(sizeof(int (*)(int)) == 8, "sizeof(fnptr)==8");
    CHK(sizeof(void (*)(void)) == 8, "sizeof(voidfnptr)==8");
    CHK(sizeof(size_t) == 8, "sizeof(size_t)==8");
    CHK(sizeof(uintptr_t) == 8, "sizeof(uintptr_t)==8");
    CHK(_Alignof(void *) == 8, "alignof(void*)==8");

    /* Public C-compatible struct/union/enum layouts (golden ABI table). */
    CHK(sizeof(cplus_alloc_meta) == 40, "sizeof(cplus_alloc_meta)==40");
    CHK(_Alignof(cplus_alloc_meta) == 8, "alignof(cplus_alloc_meta)==8");
    CHK(offsetof(cplus_alloc_meta, byte_size) == 0, "alloc.byte_size@0");
    CHK(offsetof(cplus_alloc_meta, elem_size) == 8, "alloc.elem_size@8");
    CHK(offsetof(cplus_alloc_meta, elem_count) == 16, "alloc.elem_count@16");
    CHK(offsetof(cplus_alloc_meta, generation) == 24, "alloc.generation@24");
    CHK(offsetof(cplus_alloc_meta, is_live) == 32, "alloc.is_live@32");

    CHK(sizeof(cplus_safe_alloc_meta) == 40, "sizeof(safe_alloc_meta)==40");
    CHK(_Alignof(cplus_safe_alloc_meta) == 8, "alignof(safe_alloc_meta)==8");
    CHK(sizeof(cplus_thrd_t) == 32, "sizeof(cplus_thrd_t)==32");
    CHK(sizeof(cplus_mtx_t) == 128, "sizeof(cplus_mtx_t)==128");
    CHK(sizeof(cplus_cnd_t) == 128, "sizeof(cplus_cnd_t)==128");
    CHK(sizeof(cplus_once_flag) == 64, "sizeof(cplus_once_flag)==64");
    CHK(sizeof(cplus_tss_t) == 8, "sizeof(cplus_tss_t)==8");

    CHK(sizeof(char16_t) == 2, "sizeof(char16_t)==2");
    CHK(sizeof(char32_t) == 4, "sizeof(char32_t)==4");

    CHK(sizeof(cplus_safe_stat) == 48, "sizeof(cplus_safe_stat)==48");
    CHK(offsetof(cplus_safe_stat, calls) == 24, "stat.calls@24");
    CHK(offsetof(cplus_safe_stat, ok) == 32, "stat.ok@32");
    CHK(offsetof(cplus_safe_stat, fail) == 40, "stat.fail@40");
    CHK(sizeof(cplus_safe_platform_info) == 28, "sizeof(platform_info)==28");
    CHK(_Alignof(cplus_safe_platform_info) == 4, "alignof(platform_info)==4");
    CHK(sizeof(errno_t) == 4, "sizeof(errno_t)==4");
    CHK(sizeof(rsize_t) == 8, "sizeof(rsize_t)==8");

    printf("abi_probe: %d passed, %d failed\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
