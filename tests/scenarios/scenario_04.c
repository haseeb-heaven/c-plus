/* scenario_04 - flexible-array member size mismatch (1000+ distinct cases).
 *
 * A struct with a C99 flexible array member is allocated with an actual
 * payload size that is intentionally unrelated to the value stored in its
 * "length" header.  Every write goes through the ORDINARY C spelling of
 * memset, hardened by stdlib/safe/safe_c.h.  The wrapper must refuse any
 * write that exceeds the REAL tracked remaining capacity of the allocation
 * (not the capacity claimed by r->length), leaving memory untouched, and
 * must allow writes that fit.
 *
 * No raw out-of-bounds access is ever performed by this test: the write
 * pointer is always inside [base, base + sizeof(Record) + payload], and all
 * writes are made through the hardened wrapper.  A trailing canary
 * allocation plus a full in-allocation byte model detect any overrun.
 */
#define _POSIX_C_SOURCE 200112L

/* Include the hardening header FIRST: it remaps malloc/free/memset. */
#include "stdlib/safe/safe_c.h"
#include "stdlib/safe/safe_runtime.h"

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NCASES   1200
#define CANARY_SZ 32u
/* Interior-pointer slack: malloc's alignment makes base+SLACK suitably
 * aligned for size_t, and an interior address cannot alias a neighbouring
 * block's one-past-end (the source of ambiguous capacity queries). */
#define SLACK    (sizeof(void *) * 2u)

struct Record {
    size_t length;
    unsigned char data[];
};

typedef struct {
    size_t header_size;      /* nominal header size (case label) */
    size_t declared_length;  /* value stored in r->length (may be bogus) */
    size_t allocated_payload;/* real payload bytes allocated */
    size_t write_len;        /* bytes requested in the hardened memset */
    ptrdiff_t offset;        /* signed: <0 -> inside header, ==payload -> 1-past */
} Case;

/* Last case being exercised; used by the crash FINDING handler. */
static char     g_case_msg[256];
static size_t   g_case_msg_len;
static volatile sig_atomic_t g_active;
static unsigned long g_findings;

/* Keep every block live for the whole run: no freed address can be reused as
 * an interior write pointer (the recent-free cache would report a false UAF).
 * Total footprint is under a few MiB. */
static void *g_rec_raw[NCASES];
static void *g_can_raw[NCASES];

static void crash_handler(int sig) {
    static const char pre[]  = "FINDING scenario_04 CRASH signal=";
    static const char mid[]  = " : ";
    static const char nl[]   = "\n";
    char d[3];
    (void)sig;
    if (g_active) {
        d[0] = (char)('0' + (sig / 10));
        d[1] = (char)('0' + (sig % 10));
        d[2] = '\0';
        (void)write(2, pre, sizeof(pre) - 1u);
        (void)write(2, d, 2u);
        (void)write(2, mid, sizeof(mid) - 1u);
        (void)write(2, g_case_msg, g_case_msg_len);
        (void)write(2, nl, 1u);
    }
    _exit(70);
}

static void finding(const char *reason) {
    ++g_findings;
    fprintf(stderr, "FINDING scenario_04 %s : %s\n", g_case_msg, reason);
    fflush(stderr);
}

/* Remaining writable bytes from the FAM position plus `off` to the end of
 * the tracked block.  `off` is kept within [-sizeof(Record), payload]. */
static size_t remaining_for(size_t payload, ptrdiff_t off) {
    if (off >= 0) return payload - (size_t)off;
    return payload + (size_t)(-(off));
}

static void make_case(size_t i, Case *c) {
    size_t rem;

    c->header_size = sizeof(struct Record) + (i % 5u) * 4u; /* 8,12,16,20,24 */
    c->allocated_payload = i;                                /* unique => 1200 distinct */

    switch (i % 7u) {
        case 0: c->declared_length = 0; break;                       /* length 0 */
        case 1: c->declared_length = c->allocated_payload; break;    /* length == payload */
        case 2: c->declared_length = c->allocated_payload + 1u; break;/* length > payload */
        case 3: c->declared_length = SIZE_MAX / (2u + i % 7u); break;/* huge */
        case 4: c->declared_length = c->allocated_payload / 2u; break;
        case 5: c->declared_length = SIZE_MAX; break;                /* extreme */
        default: c->declared_length = i * 3u; break;
    }

    /* Negative offsets stay inside the allocation (inside the header). */
    switch (i % 5u) {
        case 0: c->offset = -(ptrdiff_t)sizeof(struct Record); break; /* base of block */
        case 1: c->offset = -1; break;                               /* last header byte */
        case 2: c->offset = 0; break;                                /* first payload byte */
        case 3: c->offset = (ptrdiff_t)(c->allocated_payload / 2u); break;
        default: c->offset = (ptrdiff_t)c->allocated_payload; break;  /* one-past-end */
    }

    rem = remaining_for(c->allocated_payload, c->offset);

    /* write_len spans exact-fit, one-over, far-over, zero, huge and small. */
    switch (i % 9u) {
        case 0: c->write_len = 0; break;
        case 1: c->write_len = 1; break;
        case 2: c->write_len = rem; break;                            /* exact fit */
        case 3: c->write_len = rem + 1u; break;                       /* one over */
        case 4: c->write_len = rem + 1u + (i % 11u); break;           /* over */
        case 5: c->write_len = SIZE_MAX; break;                       /* > RSIZE_MAX_MEM */
        case 6: c->write_len = SIZE_MAX / 4u; break;                  /* huge, in RSIZE range */
        case 7: c->write_len = rem / 2u + 1u; break;                  /* boundary dependent */
        default: c->write_len = 2u + (i % 17u); break;                /* small fixed */
    }
}

int main(void) {
    size_t i;
    unsigned long before = g_findings;

    signal(SIGSEGV, crash_handler);
    signal(SIGBUS,  crash_handler);
    signal(SIGABRT, crash_handler);
    signal(SIGFPE,  crash_handler);

    /* Refusals must be recoverable diagnostics, not process termination. */
    cplus_runtime_set_violation_trap(0);

    for (i = 0; i < NCASES; ++i) {
        Case c;
        struct Record *r;
        unsigned char *canary, *rec_raw, *can_raw;
        unsigned char *dst;
        unsigned char *rb;
        size_t block_size, consumed, rem, tracked, j, n;
        size_t declared_copy;
        int expected_ok, actual_ok;

        make_case(i, &c);

        g_case_msg_len = (size_t)snprintf(
            g_case_msg, sizeof(g_case_msg),
            "case=%lu header_size=%zu declared_length=%zu "
            "allocated_payload=%zu write_len=%zu offset=%td",
            (unsigned long)i, c.header_size, c.declared_length,
            c.allocated_payload, c.write_len, c.offset);
        if (g_case_msg_len >= sizeof(g_case_msg))
            g_case_msg_len = sizeof(g_case_msg) - 1u;

        g_active = 1;

        block_size = sizeof(struct Record) + c.allocated_payload;
        consumed   = (size_t)((ptrdiff_t)sizeof(struct Record) + c.offset);
        rem        = block_size - consumed; /* == remaining_for(...) */

        /* Allocate the canary first, then the record, so the record's
         * one-past-end address is not the base of another live block.  Both
         * usable pointers are interior to their tracked allocation. */
        can_raw = (unsigned char *)malloc(SLACK + CANARY_SZ);
        rec_raw = (unsigned char *)malloc(SLACK + block_size);
        if (rec_raw == NULL || can_raw == NULL) {
            finding("allocation failed");
            g_active = 0;
            if (rec_raw) free(rec_raw);
            if (can_raw) free(can_raw);
            continue;
        }
        g_can_raw[i] = can_raw;
        g_rec_raw[i] = rec_raw;
        canary = can_raw + SLACK;
        r = (struct Record *)(void *)(rec_raw + SLACK);

        /* Initialize the whole tracked region and the trailing canary. */
        (void)memset(r, 0x5A, block_size);
        (void)memset(canary, 0xC3, CANARY_SZ);
        {
            size_t k;
            for (k = 0; k < CANARY_SZ; ++k) {
                if (canary[k] != 0xC3) {
                    finding("canary initialization did not take effect");
                    break;
                }
            }
        }
        r->length = c.declared_length;
        declared_copy = r->length;

        /* `dst` is exactly r->data + offset, formed from the block base so
         * that negative offsets never index the FAM object itself. */
        dst = (unsigned char *)(void *)r + consumed;

        /* With the layout above, remaining capacity is unambiguous even at
         * the one-past-end position (it must report exactly zero). */
        tracked = cplus_safe_remaining_capacity(dst);
        if (tracked != rem) {
            finding("tracked-capacity model mismatch");
        }

        expected_ok = (c.write_len == 0) ||
                      (c.write_len <= rem && c.write_len <= RSIZE_MAX_MEM);

        /* The hardened ordinary memset macro discards its return value, so a
         * refusal is observed via its diagnostic (errno == ERANGE, with a
         * "[CPLUS-SAFE] ... refused" message on stderr).  The byte model and
         * canary below independently prove that no out-of-bounds write
         * happened. */
        errno = 0;
        memset(dst, 0x41, c.write_len); /* hardened ordinary memset */
        actual_ok = (errno == 0);
        if (actual_ok != expected_ok) {
            finding(actual_ok ? "over-capacity write was NOT refused"
                               : "in-capacity write was refused");
        }
        if (!expected_ok && errno == 0) {
            finding("refused write emitted no diagnostic");
        }

        /* Full in-allocation byte model: nothing outside the intended range
         * may change, and a permitted write must cover exactly that range. */
        rb = (unsigned char *)(void *)r;
        for (j = 0; j < block_size; ++j) {
            unsigned char exp = 0x5A;
            if (j < sizeof(struct Record) && j < sizeof(size_t)) {
                exp = ((unsigned char *)&declared_copy)[j];
            }
            if (actual_ok && c.write_len > 0 &&
                j >= consumed && j - consumed < c.write_len) {
                exp = 0x41;
            }
            if (rb[j] != exp) {
                static char dbg[128];
                snprintf(dbg, sizeof(dbg),
                         "unexpected byte at j=%zu exp=0x%02x got=0x%02x "
                         "(block=%zu consumed=%zu wlen=%zu)",
                         j, (unsigned)exp, (unsigned)rb[j], block_size,
                         consumed, c.write_len);
                finding(dbg);
                break;
            }
        }

        /* Canary after the allocation must be untouched. */
        for (n = 0; n < CANARY_SZ; ++n) {
            if (canary[n] != 0xC3) {
                static char dbg2[160];
                snprintf(dbg2, sizeof(dbg2),
                         "trailing canary corrupted at n=%zu got=0x%02x "
                         "(block=%zu consumed=%zu wlen=%zu errno=%d)",
                         n, (unsigned)canary[n], block_size, consumed,
                         c.write_len, errno);
                finding(dbg2);
                break;
            }
        }

        g_active = 0;
    }

    for (i = 0; i < NCASES; ++i) {
        if (g_rec_raw[i]) free(g_rec_raw[i]);
        if (g_can_raw[i]) free(g_can_raw[i]);
    }

    cplus_runtime_set_violation_trap(1);

    if (g_findings != before) {
        fprintf(stderr, "scenario_04: %lu FINDINGs\n", g_findings - before);
        fflush(stderr);
        return 1;
    }

    printf("scenario_04: %d cases ok\n", NCASES);
    fflush(stdout);
    return 0;
}
