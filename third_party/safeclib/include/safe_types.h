#ifndef __SAFE_TYPES_H__
#define __SAFE_TYPES_H__
#ifdef __cplusplus
extern "C" {
#endif
#ifdef __KERNEL__
#include <linux/stddef.h>
#include <linux/types.h>
#include <linux/errno.h>
typedef int errno_t;
#else
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/types.h>
#ifndef errno_t
typedef int errno_t;
#endif
#endif
#ifndef _RSIZE_T
typedef size_t rsize_t;
#endif
#ifndef RSIZE_MAX_STR
#define RSIZE_MAX_STR (4UL << 10)
#endif
#ifndef RSIZE_MAX_MEM
#define RSIZE_MAX_MEM (RSIZE_MAX_STR*4)
#endif
#ifndef RSIZE_MAX
#define RSIZE_MAX (~(rsize_t)0)
#endif
#ifdef __GNUC__
#define __attribute_format__(type, index, check) __attribute__((format(type, index, check)))
#define __attribute_format_wprintf(index, check)
#define __attribute_format_wscanf(index, check)
#else
#define __attribute_format__(type, index, check)
#define __attribute_format_wprintf(index, check)
#define __attribute_format_wscanf(index, check)
#endif
#ifdef __cplusplus
}
#endif
#endif
