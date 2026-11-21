#ifndef CPLUS_LIBC_MUSL_SHIM_H
#define CPLUS_LIBC_MUSL_SHIM_H
/* Minimal shim to compile selected musl string sources inside cplus hardened libc.
 * We keep musl source files unmodified where possible and map symbols to cplus_*. */
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#define weak
#define hidden
#endif
