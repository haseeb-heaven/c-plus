/*
 * Native safety capability probe for #22.
 * Reports platform/compiler features relevant to existing libc safety.
 * This is evidence, not a claim of protection.
 */
#include <stddef.h>
#include <stdio.h>

int main(void) {
#if defined(__clang__)
    printf("compiler=clang %d.%d.%d\n", __clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(__GNUC__)
    printf("compiler=gcc %d.%d.%d\n", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#else
    printf("compiler=unknown\n");
#endif

#ifdef __STDC_LIB_EXT1__
    printf("annex_k=%d\n", (int)__STDC_LIB_EXT1__);
#else
    printf("annex_k=none\n");
#endif

#ifdef _FORTIFY_SOURCE
    printf("fortify_source=%d\n", (int)_FORTIFY_SOURCE);
#else
    printf("fortify_source=none\n");
#endif

#if defined(__has_feature)
# if __has_feature(address_sanitizer)
    printf("asan=compile\n");
# else
    printf("asan=no\n");
# endif
# if __has_feature(undefined_behavior_sanitizer)
    printf("ubsan=compile\n");
# else
    printf("ubsan=no\n");
# endif
# if __has_feature(thread_sanitizer)
    printf("tsan=compile\n");
# else
    printf("tsan=no\n");
# endif
# if __has_feature(bounds_safety)
    printf("bounds_safety=compile\n");
# else
    printf("bounds_safety=no\n");
# endif
#else
    printf("asan=unknown\n");
    printf("ubsan=unknown\n");
    printf("tsan=unknown\n");
    printf("bounds_safety=unknown\n");
#endif

#if defined(__has_builtin)
# if __has_builtin(__builtin_object_size)
    printf("builtin_object_size=yes\n");
# else
    printf("builtin_object_size=no\n");
# endif
# if __has_builtin(__builtin_dynamic_object_size)
    printf("builtin_dynamic_object_size=yes\n");
# else
    printf("builtin_dynamic_object_size=no\n");
# endif
#else
    printf("builtin_object_size=unknown\n");
    printf("builtin_dynamic_object_size=unknown\n");
#endif

#if defined(__APPLE__)
    printf("platform=apple\n");
#elif defined(_WIN32)
    printf("platform=windows\n");
#elif defined(__linux__)
    printf("platform=linux\n");
#else
    printf("platform=other\n");
#endif

#ifdef __GLIBC__
    printf("libc=glibc\n");
#elif defined(__APPLE__)
    printf("libc=apple-libc\n");
#elif defined(__MUSL__)
    printf("libc=musl\n");
#elif defined(_MSC_VER)
    printf("libc=ucrt-msvc\n");
#else
    printf("libc=unknown\n");
#endif

    {
        char buf[16];
        printf("object_size_const=%zu\n", __builtin_object_size(buf, 1));
    }
    return 0;
}
