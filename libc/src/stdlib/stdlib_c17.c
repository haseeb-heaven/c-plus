/* PLATFORM-BACKED ISO C17 <stdlib.h> process/allocation entry points.
 *
 * This file is a thin, logged, namespaced adapter over the HOST C runtime
 * (CRT). It does NOT bundle, replace, or reimplement a memory allocator and it
 * does NOT create a new allocation arena. Exact host backend per function:
 *
 *   cplus_aligned_alloc  -> host aligned_alloc()      (macOS / glibc / POSIX)
 *                        -> host _aligned_malloc()    (Windows / MSVC)
 *   cplus_quick_exit     -> host quick_exit()
 *   cplus_at_quick_exit  -> host at_quick_exit()
 *   cplus__Exit          -> host _Exit()
 *
 * ALLOCATOR PROVENANCE / MIXED-FREE HAZARD:
 *   cplus_aligned_alloc deliberately calls the host aligned_alloc (or
 *   _aligned_malloc on Windows) DIRECTLY. It is intentionally NOT routed
 *   through cplus_malloc from alloc.c: mixing allocators means the matching
 *   deallocator is the one belonging to the SAME runtime that produced the
 *   pointer. On macOS/glibc/POSIX, memory returned here is owned by the host
 *   CRT and MUST be released with the host free() -- never with cplus_free()
 *   or free_checked(). On Windows, _aligned_malloc() memory MUST be released
 *   with host _aligned_free(); host free() is INVALID for that pointer. These
 *   caveats are the caller's responsibility because this wrapper only forwards
 *   the host result unchanged.
 *
 * HARDENING EXTENSION (defined behavior in place of UB): the C11 constraints
 * on aligned_alloc are checked before delegating. "alignment" must be a
 * non-zero power of two that is also a multiple of sizeof(void *), and "size"
 * must be an integral multiple of "alignment". Any constraint violation sets
 * errno = EINVAL and returns NULL; size is never silently rounded up.
 *
 * Logging is opt-in (cplus_libc_log_set_enabled) and allocation-free; it never
 * affects the delegated result.
 */

/* Feature macros must precede any system header: on glibc the C11 process and
 * allocation functions are only declared when __USE_ISOC11 is enabled, even
 * when this translation unit is compiled as strict C99. */
#ifndef _ISOC11_SOURCE
#define _ISOC11_SOURCE 1
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif

#include "../../log/log.h"
#include <errno.h>
#include <stdlib.h>

#if defined(_WIN32)
#include <malloc.h>
#endif

void *cplus_aligned_alloc(size_t alignment, size_t size) {
    cplus_libc_log("aligned_alloc", "enter");
    /* Short-circuit order matters: never take "size % alignment" when
     * alignment is zero. */
    if (alignment == 0u || (alignment & (alignment - 1u)) != 0u ||
        (alignment % sizeof(void *)) != 0u || (size % alignment) != 0u) {
        cplus_libc_log("aligned_alloc", "invalid alignment/size");
        errno = EINVAL;
        return NULL;
    }
#if defined(_WIN32)
    /* MSVC has no C11 aligned_alloc. _aligned_malloc takes (size, alignment)
     * (note the reversed argument order) and its blocks MUST be freed with
     * _aligned_free(), not free(). */
    return _aligned_malloc(size, alignment);
#else
    return aligned_alloc(alignment, size);
#endif
}

void cplus_quick_exit(int status) {
    cplus_libc_log("quick_exit", "enter");
    quick_exit(status);
}

int cplus_at_quick_exit(void (*func)(void)) {
    cplus_libc_log("at_quick_exit", "enter");
    return at_quick_exit(func);
}

void cplus__Exit(int status) {
    cplus_libc_log("_Exit", "enter");
    _Exit(status);
}
