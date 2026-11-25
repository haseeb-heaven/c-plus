/* ONE caller source, compiled TWICE against the SAME saved libstdc_legacy.a:
 *
 *   (A) app_plain : no safe_c.h; links ONLY the saved archive (+ host libc)
 *   (B) app_safe  : -include stdlib/safe/safe_c.h; links the SAME saved archive
 *                   plus the prebuilt SafeCRT / hardened support archives
 *
 * The only thing that differs between (A) and (B) is caller-side macro
 * expansion.  For valid inputs stdout must be byte-identical.  The precompiled
 * archive is never rebuilt, recompiled, or re-archived in either case.
 *
 * argv[1], argv[2] are the identical valid inputs supplied by run.sh to both
 * builds (defaults 20 and 22 when omitted).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Public API of the one-time-compiled legacy.c (plain C declarations). */
extern int stdc_legacy_version(void);
extern int stdc_legacy_mul(int, int);
extern int stdc_legacy_sum(const unsigned char *, size_t);
extern size_t stdc_legacy_copy(unsigned char *, const unsigned char *, size_t);
extern int stdc_legacy_equal(const unsigned char *, const unsigned char *, size_t);

int main(int argc, char **argv) {
    int a = (argc > 1) ? atoi(argv[1]) : 20;
    int b = (argc > 2) ? atoi(argv[2]) : 22;
    unsigned char src[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    unsigned char dst[8];
    char label[32];

    printf("caller: version=%d\n", stdc_legacy_version());
    printf("caller: mul=%d\n", stdc_legacy_mul(a, b));
    printf("caller: sum=%d\n", stdc_legacy_sum(src, 8));

    /* Exercise the saved library's own memcpy/memcmp-backed entry points. */
    stdc_legacy_copy(dst, src, 8);
    printf("caller: copy_equal=%d\n", stdc_legacy_equal(dst, src, 8));

    /* Ordinary C names.  In (B) these are compile-time macro-mapped to the
     * capacity-checked wrappers; in (A) they are host libc. */
    strcpy(label, "stdc");
    strcat(label, "-legacy-ok");
    printf("caller: label=%s\n", label);
    printf("caller: END\n");
    return 0;
}
