/* BASELINE: case_04.c crashes; SAFE: defined refusal/valid result. */
/* Ordinary C APIs plus either safe header:
 *   default     : "stdlib/safe/safe_c.h"          (SafeCRT layer)
 *   -DPLAIN_LIBC: "cplus_libc_safe.h"             (plain hardened-libc layer)
 * The BASELINE keeps a token pointing into the old arena, calls realloc(),
 * then dereferences that stale alias (heap-use-after-free when the block
 * moves).  Here the token's offset is recorded before the resize; after the
 * resize a generation check detects that the old alias is stale and a defined
 * refusal is printed, while the byte is obtained from the new base + offset.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef PLAIN_LIBC
#include "cplus_libc_safe.h"
#define CPLUS_GEN_CHECK(p, g) cplus_alloc_check_generation((p), (g))
typedef cplus_alloc_meta cplus_meta_t;
static int cplus_meta_get(const void *p, cplus_meta_t *m) {
    return cplus_alloc_get_meta(p, m);
}
#else
#include "stdlib/safe/safe_c.h"
#define CPLUS_GEN_CHECK(p, g) cplus_safe_ptr_check_generation((p), (g))
typedef cplus_safe_alloc_meta cplus_meta_t;
static int cplus_meta_get(const void *p, cplus_meta_t *m) {
    return cplus_safe_alloc_get_meta(p, m);
}
#endif

struct Token { const char *start; size_t len; };

int main(void) {
    size_t cap = 4, len = 0;
    char *arena = (char *)malloc(cap);
    if (!arena) { puts("result: refusal - initial allocation failed"); return 1; }
    strcpy(arena, "hdr");
    len = 3;
    struct Token tok = { .start = arena, .len = len };

    size_t extra = 0;
    printf("extra: ");
    if (scanf("%zu", &extra) != 1) { puts("result: refusal - bad input"); return 1; }

    /* Remember the alias' offset and generation while both are known-valid. */
    size_t off = (size_t)(tok.start - arena);
    if (off > cap) { puts("result: refusal - token outside its arena"); free(arena); return 1; }
    cplus_meta_t meta;
    unsigned long long gen_before = 0;
    if (cplus_meta_get(arena, &meta) == 0) gen_before = meta.generation;

    /* Reject a wrapping size request before handing it to realloc(). */
    if (extra > SIZE_MAX - cap) {
        puts("result: refusal - grow size overflows");
        free(arena);
        return 0;
    }
    char *grown = (char *)realloc(arena, cap + extra);
    if (!grown) {
        puts("result: refusal - realloc failed; original arena still valid");
        free(arena);
        return 0;
    }
    arena = grown; /* only the NEW base is used from here on */

    /* The old alias is never dereferenced.  Generation mismatch proves it no
     * longer describes the live block (realloc bumps the generation). */
    if (gen_before == 0 || !CPLUS_GEN_CHECK(tok.start, gen_before)) {
        puts("result: refusal - stale token alias detected by generation check");
    } else {
        puts("result: token alias still current (in-place realloc)");
    }

    /* Re-derive the token from the new base and validate before reading. */
    size_t new_cap = cap + extra;
    if (off >= new_cap || tok.len > new_cap - off) {
        puts("result: refusal - re-derived token range invalid");
        free(arena);
        return 0;
    }
    printf("byte (re-derived): %c\n", arena[off]);
    puts("result: ok - byte read from new base; stale alias not dereferenced");
    free(arena);
    return 0;
}
