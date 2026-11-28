/* Native safety-facility probe: what this platform/compiler actually offers.
 * Prints availability; never asserts, so it runs everywhere. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
#ifdef _FORTIFY_SOURCE
    printf("fortify=%d\n", _FORTIFY_SOURCE);
#else
    printf("fortify=none\n");
#endif
#ifdef __OPTIMIZE__
    printf("optimized=1\n");
#else
    printf("optimized=0\n");
#endif
#if defined(__STDC_LIB_EXT1__)
    printf("annex_k=%d\n", (int)__STDC_LIB_EXT1__);
#else
    printf("annex_k=none\n");
#endif
#ifdef __has_builtin
# if __has_builtin(__builtin_object_size)
    printf("object_size=yes\n");
# else
    printf("object_size=no\n");
# endif
# if __has_builtin(__builtin_dynamic_object_size)
    printf("dynamic_object_size=yes\n");
# else
    printf("dynamic_object_size=no\n");
# endif
# if __has_builtin(__builtin___memcpy_chk)
    printf("fortified_memcpy=yes\n");
# else
    printf("fortified_memcpy=no\n");
# endif
# if __has_builtin(__builtin_add_overflow)
    printf("checked_arithmetic=yes\n");
# else
    printf("checked_arithmetic=no\n");
# endif
#else
    printf("has_builtin=none\n");
#endif
    char buffer[32];
    printf("object_size_of_32=%zu\n", __builtin_object_size(buffer, 1));
#if defined(__APPLE__)
    printf("platform=apple\n");
#elif defined(__linux__)
    printf("platform=linux\n");
#elif defined(_WIN32)
    printf("platform=windows\n");
#else
    printf("platform=other\n");
#endif
#ifdef __GLIBC__
    printf("glibc=yes\n");
#else
    printf("glibc=no\n");
#endif
#if defined(_WIN32)
    printf("ucrt_secure=available\n");
#else
    printf("ucrt_secure=unavailable\n");
#endif
    printf("sizeof_size_t=%zu\n", sizeof(size_t));
    return 0;
}
