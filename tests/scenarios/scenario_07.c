/* scenario_07: input-controlled string overflow through ordinary strcpy().
 *
 * Standalone C99.  Build/run (see tests/scenarios/run_scenarios.sh):
 *   clang -O2 -std=c99 -Wall -Wextra -pedantic \
 *     -I. -Istdlib/safe -Ilibc -Ilibc/install-include \
 *     -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
 *     tests/scenarios/scenario_07.c \
 *     build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
 *     -lm -pthread -o /tmp/scenario_07 && /tmp/scenario_07
 *
 * The source string is always a valid, NUL-terminated string built from a
 * deterministic table of (dest_kind, dest_capacity, src_len, pattern) tuples.
 * Every write goes through the hardened ordinary-C strcpy() wrapper supplied
 * by stdlib/safe/safe_c.h; this test never performs a raw overflow itself.
 *
 * dest_kind cycles through:
 *   0  stack array with compile-time-known __builtin_object_size
 *   1  tracked malloc() allocation (capacity from the allocation table)
 *   2  struct member with a trailing canary (mode-1 BOS = member size only)
 *   3  global array with compile-time-known __builtin_object_size
 *
 * Capacity 0 is represented by a one-past-the-end pointer into a 1-byte
 * object; its proven capacity is 0, so the wrapper must refuse every copy
 * without writing.  For the heap, malloc(0) is tracked as 1 byte by the
 * allocator (documented behaviour), so a heap "capacity 0" tuple has an
 * effective proven capacity of 1.
 */
#include "stdlib/safe/safe_c.h"
#include "stdlib/safe/safe_runtime.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#define CANARY_MAGIC 0xD5A5C3E7A1B29876ULL
#define FILL_BYTE    ((unsigned char)0x5A)

/* 4 kinds x 10 capacities x 6 length modes x 6 content patterns = 1440
 * distinct deterministic tuples. */
#define NCAPS 10
#define NMODES 6
#define NPATS 6
#define NCASES (4 * NCAPS * NMODES * NPATS)

static const unsigned short CAPS[NCAPS] = {
    0u, 1u, 2u, 3u, 4u, 8u, 16u, 32u, 64u, 128u
};

/* Largest source is 4096 bytes plus its terminating NUL. */
static char srcbuf[4097];
static unsigned long g_findings;

/* ---- raw helpers (deliberately not the hardened API) ------------------- */
static void fill_bytes(char *p, size_t n, unsigned char v) {
    while (n-- != 0u) *p++ = (char)v;
}

static int same_bytes(const char *a, const char *b, size_t n) {
    while (n-- != 0u) {
        if (*a++ != *b++) return 0;
    }
    return 1;
}

/* Build a valid NUL-terminated source of exactly `len` visible bytes.
 * Patterns 1 and 4 place an embedded NUL before the end; strlen() then
 * reports the shorter effective length, which is what strcpy() must copy. */
static void build_source(size_t len, int pat) {
    size_t i;
    for (i = 0; i < len; ++i) {
        char c;
        switch (pat) {
        case 0:  c = 'A'; break;
        case 1:  c = 'B'; break;
        case 2:  c = (i & 1u) ? 'a' : 'b'; break;
        case 3:  c = 'C'; break;
        case 4:  c = 'D'; break;
        default: c = (char)('0' + (int)(i % 10u)); break;
        }
        srcbuf[i] = c;
    }
    if (len > 0u && (pat == 1 || pat == 4)) {
        size_t pos = (pat == 1) ? 1u : (len / 2u);
        if (pos < len) srcbuf[pos] = '\0'; /* embedded NUL */
    }
    srcbuf[len] = '\0';
}

static size_t source_len_for(unsigned cap, int mode) {
    switch (mode) {
    case 0:  return 0u;                                /* empty string */
    case 1:  return (cap != 0u) ? (size_t)(cap - 1u) : 0u; /* capacity - 1 */
    case 2:  return (size_t)cap;                       /* capacity */
    case 3:  return (size_t)cap + 1u;                  /* capacity + 1 */
    case 4:  return 4096u;                             /* huge input */
    default: return (size_t)cap + 2u;                  /* capacity + 2 */
    }
}

static void finding(unsigned i, int kind, unsigned cap, size_t len, int pat,
                    size_t eff, const char *what) {
    printf("FINDING scenario_07 case=%lu kind=%d dest_capacity=%u "
           "src_len=%lu pattern=%d eff_len=%lu: %s\n",
           (unsigned long)i, kind, cap, (unsigned long)len, pat,
           (unsigned long)eff, what);
    g_findings++;
}

/* Expected outcome is derived from the proven capacity `pc` and the
 * effective source length `eff` (= strlen after any embedded NUL). */
static void verify_case(unsigned i, int kind, unsigned cap, size_t len, int pat,
                        size_t eff, size_t pc, char *dest, char *ret,
                        unsigned long long *canary_p, char *pre) {
    if (eff + 1u <= pc) {
        if (ret != dest)
            finding(i, kind, cap, len, pat, eff,
                    "fitting copy: strcpy did not return dest");
        if (!same_bytes(dest, srcbuf, eff + 1u))
            finding(i, kind, cap, len, pat, eff,
                    "fitting copy: destination differs from source");
    } else {
        if (ret != NULL)
            finding(i, kind, cap, len, pat, eff,
                    "overrun: strcpy was not refused");
        if (errno != ERANGE)
            finding(i, kind, cap, len, pat, eff,
                    "overrun: errno is not ERANGE");
        if (pc > 0u) {
            /* safeclib nulls dest[0] on a constraint violation: a defined,
             * empty destination that cannot be read out of bounds. */
            if (dest[0] != '\0')
                finding(i, kind, cap, len, pat, eff,
                        "overrun: destination not left in defined empty state");
        } else if (pre != NULL && *pre != (char)FILL_BYTE) {
            finding(i, kind, cap, len, pat, eff,
                    "overrun: byte before zero-capacity destination changed");
        }
    }
    if (canary_p != NULL && *canary_p != CANARY_MAGIC)
        finding(i, kind, cap, len, pat, eff, "trailing canary overwritten");
}

/* Call the ordinary (hardened) strcpy with the ORIGINAL destination
 * expression so __builtin_object_size() is evaluated at the call site. */
#define RUN_DEST(EXPR, PCVAL) do {                                            \
        pc = (size_t)(PCVAL);                                                  \
        dest = (EXPR);                                                          \
        errno = 0;                                                              \
        ret = strcpy(EXPR, srcbuf);                                             \
        verify_case(i, kind, cap, len, pat, eff, pc, dest, ret,                 \
                    canary_p, pre);                                             \
    } while (0)

/* Fixed-size stack objects (BOS is exact for each one). */
#define STACK_CASE(N) do {                                                     \
        fill_bytes(s##N, (N), FILL_BYTE);                                       \
        RUN_DEST(s##N, (N));                                                    \
    } while (0)

/* Fixed-size global objects. */
#define GLOBAL_CASE(N) do {                                                    \
        fill_bytes(g##N, (N), FILL_BYTE);                                       \
        RUN_DEST(g##N, (N));                                                    \
    } while (0)

/* Struct member: mode-1 BOS is the member size, so the trailing canary is
 * never part of the destination capacity. */
struct M1   { char d[1];   unsigned long long canary; };
struct M2   { char d[2];   unsigned long long canary; };
struct M3   { char d[3];   unsigned long long canary; };
struct M4   { char d[4];   unsigned long long canary; };
struct M8   { char d[8];   unsigned long long canary; };
struct M16  { char d[16];  unsigned long long canary; };
struct M32  { char d[32];  unsigned long long canary; };
struct M64  { char d[64];  unsigned long long canary; };
struct M128 { char d[128]; unsigned long long canary; };
struct M256 { char d[256]; unsigned long long canary; };

#define STRUCT_CASE(N) do {                                                    \
        struct M##N m;                                                         \
        fill_bytes(m.d, (N), FILL_BYTE);                                        \
        m.canary = CANARY_MAGIC;                                                \
        canary_p = &m.canary;                                                   \
        RUN_DEST(m.d, (N));                                                     \
    } while (0)

#define STRUCT_CASE_ZERO() do {                                                \
        struct M1 m;                                                           \
        fill_bytes(m.d, 1u, FILL_BYTE);                                         \
        m.canary = CANARY_MAGIC;                                                \
        canary_p = &m.canary;                                                   \
        pre = &m.d[0];                                                          \
        RUN_DEST(&m.d[1], 0);                                                   \
    } while (0)

/* Globals used by dest_kind 3. */
static char g1[1], g2[2], g3[3], g4[4], g8[8], g16[16], g32[32], g64[64],
             g128[128], g256[256];

static void run_case(unsigned i) {
    int kind = (int)(i % 4u);
    unsigned cap = CAPS[(i / 4u) % (unsigned)NCAPS];
    int mode = (int)((i / (4u * (unsigned)NCAPS)) % (unsigned)NMODES);
    int pat  = (int)((i / (4u * (unsigned)NCAPS * (unsigned)NMODES))
                     % (unsigned)NPATS);
    size_t len = source_len_for(cap, mode);
    size_t eff, pc;
    char *dest = NULL, *ret = NULL, *pre = NULL;
    unsigned long long *canary_p = NULL;

    build_source(len, pat);
    eff = strlen(srcbuf);

    switch (kind) {
    case 0: { /* stack array, known __builtin_object_size */
        char s1[1], s2[2], s3[3], s4[4], s8[8], s16[16], s32[32], s64[64],
             s128[128], s256[256];
        switch (cap) {
        case 0:  fill_bytes(s1, 1u, FILL_BYTE); pre = &s1[0];
                 RUN_DEST(&s1[1], 0); break;
        case 1:  STACK_CASE(1);   break;
        case 2:  STACK_CASE(2);   break;
        case 3:  STACK_CASE(3);   break;
        case 4:  STACK_CASE(4);   break;
        case 8:  STACK_CASE(8);   break;
        case 16: STACK_CASE(16);  break;
        case 32: STACK_CASE(32);  break;
        case 64: STACK_CASE(64);  break;
        case 128:STACK_CASE(128); break;
        default: STACK_CASE(256); break;
        }
        break;
    }
    case 1: { /* tracked heap allocation */
        size_t hn = (cap != 0u) ? (size_t)cap : 1u; /* malloc(0) -> 1 byte */
        char *h = (char *)malloc(hn);
        if (h == NULL) {
            finding(i, kind, cap, len, pat, eff, "malloc failed");
            break;
        }
        fill_bytes(h, hn, FILL_BYTE);
        RUN_DEST(h, hn);
        free(h);
        break;
    }
    case 2: { /* struct member with trailing canary */
        switch (cap) {
        case 0:  STRUCT_CASE_ZERO(); break;
        case 1:  STRUCT_CASE(1);   break;
        case 2:  STRUCT_CASE(2);   break;
        case 3:  STRUCT_CASE(3);   break;
        case 4:  STRUCT_CASE(4);   break;
        case 8:  STRUCT_CASE(8);   break;
        case 16: STRUCT_CASE(16);  break;
        case 32: STRUCT_CASE(32);  break;
        case 64: STRUCT_CASE(64);  break;
        case 128:STRUCT_CASE(128); break;
        default: STRUCT_CASE(256); break;
        }
        break;
    }
    default: { /* global array, known __builtin_object_size */
        switch (cap) {
        case 0:  fill_bytes(g1, 1u, FILL_BYTE); pre = &g1[0];
                 RUN_DEST(&g1[1], 0); break;
        case 1:  GLOBAL_CASE(1);   break;
        case 2:  GLOBAL_CASE(2);   break;
        case 3:  GLOBAL_CASE(3);   break;
        case 4:  GLOBAL_CASE(4);   break;
        case 8:  GLOBAL_CASE(8);   break;
        case 16: GLOBAL_CASE(16);  break;
        case 32: GLOBAL_CASE(32);  break;
        case 64: GLOBAL_CASE(64);  break;
        case 128:GLOBAL_CASE(128); break;
        default: GLOBAL_CASE(256); break;
        }
        break;
    }
    }
}

int main(void) {
    unsigned i;
    setvbuf(stdout, NULL, _IONBF, 0);
    cplus_runtime_set_violation_trap(0);

    for (i = 0; i < (unsigned)NCASES; ++i)
        run_case(i);

    if (g_findings != 0u) {
        printf("scenario_07: %lu FINDING(S) in %d cases\n",
               (unsigned long)g_findings, NCASES);
        return 1;
    }
    printf("scenario_07: %d cases ok\n", NCASES);
    return 0;
}
