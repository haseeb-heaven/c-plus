/* Deterministic fuzz coverage: checked ORDINARY-NAME memory C layer.
 *
 * Every call below uses the ordinary spelling (memcpy/memmove/memset/memcmp)
 * as mapped by stdlib/safe/safe_c.h to the safeclib-backed auto wrappers.
 * Only well-defined calls are made by the harness itself: all lengths are in
 * [0, 512], except that the fixed "one-over" schedule passes 513 to the
 * CHECKED wrapper so the refusal path can be verified without any raw OOB
 * access.  Stack payloads are surrounded by 16-byte canaries; heap payloads
 * are exact-size tracked malloc_s blocks verified through the metadata API.
 *
 * A valid overlapping memmove (both copy directions) is exercised separately.
 * The whole operation stream is replayed with SafeCRT logging OFF and TRACE;
 * both passes must produce the identical FNV-1a digest.
 */
#include "safec_layer_common.h"

#include "stdlib/safe/safe_c.h"
#include "stdlib/safe/safe_log.h"
#include "stdlib/safe/safe_runtime.h"

static unsigned long g_ops;

static void run_iteration(int iter) {
    size_t n = sc_len_for(iter);
    int over = sc_is_over(n);

    /* 1. memcpy: distinct guarded objects. */
    {
        sc_byte_obj src, dst;
        unsigned char snap[SC_LEN_MAX];
        size_t i;
        sc_init_byte(&src);
        sc_init_byte(&dst);
        memcpy(snap, dst.data, SC_LEN_MAX);
        errno = 0;
        memcpy(dst.data, src.data, n);
        sc_check_canaries(src.pre, src.post);
        sc_check_canaries(dst.pre, dst.post);
        if (over) {
            assert(errno == ERANGE);
            for (i = 0; i < SC_LEN_MAX; ++i) assert(dst.data[i] == snap[i]);
        } else {
            assert(errno == 0);
            for (i = 0; i < n; ++i) assert(dst.data[i] == src.data[i]);
            for (i = n; i < SC_LEN_MAX; ++i) assert(dst.data[i] == snap[i]);
        }
        sc_hash_tag("C");
        sc_hash_u64((uint64_t)n);
        sc_hash_u64((uint64_t)over);
        sc_hash(&dst, sizeof dst);
        ++g_ops;
    }

    /* 2. memmove: distinct guarded objects. */
    {
        sc_byte_obj src, dst;
        unsigned char snap[SC_LEN_MAX];
        size_t i;
        sc_init_byte(&src);
        sc_init_byte(&dst);
        memcpy(snap, dst.data, SC_LEN_MAX);
        errno = 0;
        memmove(dst.data, src.data, n);
        sc_check_canaries(src.pre, src.post);
        sc_check_canaries(dst.pre, dst.post);
        if (over) {
            assert(errno == ERANGE);
            for (i = 0; i < SC_LEN_MAX; ++i) assert(dst.data[i] == snap[i]);
        } else {
            assert(errno == 0);
            for (i = 0; i < n; ++i) assert(dst.data[i] == src.data[i]);
            for (i = n; i < SC_LEN_MAX; ++i) assert(dst.data[i] == snap[i]);
        }
        sc_hash_tag("V");
        sc_hash_u64((uint64_t)n);
        sc_hash_u64((uint64_t)over);
        sc_hash(&dst, sizeof dst);
        ++g_ops;
    }

    /* 3. memmove: genuinely overlapping ranges inside one object, alternating
     *    dst>src (backward copying) and dst<src (forward copying). */
    if (iter % 5 == 3) {
        sc_byte_obj o;
        unsigned char before[SC_LEN_MAX], want[SC_LEN_MAX];
        size_t m, soff, doff, i;
        sc_init_byte(&o);
        memcpy(before, o.data, SC_LEN_MAX);
        m = 17u + (size_t)(sc_rnd() % 32u); /* 17..48, always > the 16-byte shift */
        assert(m + 48u < SC_LEN_MAX);
        memcpy(want, before, SC_LEN_MAX);
        /* The offsets are literals at each call site so the checked wrapper
         * can prove the interior pointer's remaining capacity in strict mode. */
        errno = 0;
        switch (iter & 3) {
            case 0: soff = 0u;  doff = 16u; memmove(o.data + 16u, o.data, m); break;
            case 1: soff = 16u; doff = 0u;  memmove(o.data, o.data + 16u, m); break;
            case 2: soff = 32u; doff = 48u; memmove(o.data + 48u, o.data + 32u, m); break;
            default: soff = 48u; doff = 32u; memmove(o.data + 32u, o.data + 48u, m); break;
        }
        sc_check_canaries(o.pre, o.post);
        assert(errno == 0);
        for (i = 0; i < m; ++i) want[doff + i] = before[soff + i];
        for (i = 0; i < SC_LEN_MAX; ++i) assert(o.data[i] == want[i]);
        assert(soff < doff + m && doff < soff + m); /* overlap was real */
        sc_hash_tag("O");
        sc_hash_u64((uint64_t)m);
        sc_hash_u64((uint64_t)soff);
        sc_hash_u64((uint64_t)doff);
        sc_hash(&o, sizeof o);
        ++g_ops;
    }

    /* 4. memset: fill exactly n bytes, nothing outside the payload. */
    {
        sc_byte_obj dst;
        unsigned char snap[SC_LEN_MAX];
        int c = (int)(sc_rnd() & 0xFFu);
        size_t i;
        sc_init_byte(&dst);
        memcpy(snap, dst.data, SC_LEN_MAX);
        errno = 0;
        memset(dst.data, c, n);
        sc_check_canaries(dst.pre, dst.post);
        if (over) {
            assert(errno == ERANGE);
            for (i = 0; i < SC_LEN_MAX; ++i) assert(dst.data[i] == snap[i]);
        } else {
            assert(errno == 0);
            for (i = 0; i < n; ++i) assert(dst.data[i] == (unsigned char)c);
            for (i = n; i < SC_LEN_MAX; ++i) assert(dst.data[i] == snap[i]);
        }
        sc_hash_tag("S");
        sc_hash_u64((uint64_t)n);
        sc_hash_u64((uint64_t)c);
        sc_hash(&dst, sizeof dst);
        ++g_ops;
    }

    /* 5. memcmp: controlled equal / first-difference cases plus one-over. */
    {
        sc_byte_obj a, b;
        unsigned char snap_a[SC_LEN_MAX], snap_b[SC_LEN_MAX];
        int want_sign = 0, got;
        size_t i;
        sc_init_byte(&a);
        sc_init_byte(&b);
        if (!over && n > 0u) {
            if (iter % 3 == 0) {
                memcpy(b.data, a.data, SC_LEN_MAX); /* exact match */
            } else {
                size_t pos = (size_t)(sc_rnd() % n);
                b.data[pos] = (unsigned char)(a.data[pos] + 1u);
            }
        }
        memcpy(snap_a, a.data, SC_LEN_MAX);
        memcpy(snap_b, b.data, SC_LEN_MAX);
        for (i = 0; !over && i < n && want_sign == 0; ++i) {
            if (snap_a[i] != snap_b[i]) want_sign = (snap_a[i] > snap_b[i]) ? 1 : -1;
        }
        errno = 0;
        got = memcmp(a.data, b.data, n);
        sc_check_canaries(a.pre, a.post);
        sc_check_canaries(b.pre, b.post);
        if (over) {
            assert(got == -1 && errno == ERANGE);
        } else {
            assert(errno == 0);
            assert(((got > 0) - (got < 0)) == want_sign);
        }
        sc_hash_tag("P");
        sc_hash_u64((uint64_t)n);
        sc_hash_u64((uint64_t)over);
        sc_hash_sign(got);
        sc_hash(&a, sizeof a);
        sc_hash(&b, sizeof b);
        ++g_ops;
    }

    /* 6. exact-size tracked heap block: in-bounds fill, then one-over refusal. */
    if (iter % 4 == 0) {
        size_t cap = 1u + (size_t)(sc_rnd() % SC_LEN_MAX);
        unsigned char *p = (unsigned char *)malloc(cap);
        int pat = (int)(sc_rnd() & 0xFFu);
        size_t i;
        assert(p != NULL);
        assert(cplus_safe_ptr_is_live(p) == 1);
        assert(cplus_safe_ptr_size(p) == cap);
        assert(cplus_safe_remaining_capacity(p) == cap);
        assert(cplus_safe_remaining_capacity(p + cap / 2u) == cap - cap / 2u);
        assert(cplus_safe_remaining_capacity(p + cap) == 0u);
        errno = 0;
        memset(p, pat, cap);
        assert(errno == 0);
        for (i = 0; i < cap; ++i) assert(p[i] == (unsigned char)pat);
        errno = 0;
        memset(p, (pat + 1) & 0xFF, cap + 1u);
        assert(errno == ERANGE);
        for (i = 0; i < cap; ++i) assert(p[i] == (unsigned char)pat);
        sc_hash_tag("H");
        sc_hash_u64((uint64_t)cap);
        sc_hash_u64((uint64_t)pat);
        sc_hash(p, cap);
        free(p);
        assert(cplus_safe_ptr_is_live(p) == 0);
        ++g_ops;
    }
}

static uint64_t run_pass(int logging_on) {
    int it;
    g_ops = 0u;
    sc_seed();
    sc_digest_init();
    cplus_safe_log_set_level(logging_on ? CPLUS_SAFE_LOG_TRACE : CPLUS_SAFE_LOG_OFF);
    for (it = 0; it < SC_ITERATIONS; ++it) run_iteration(it);
    return sc_digest_g;
}

int main(void) {
    uint64_t digest_off, digest_on;
    unsigned long ops_off = 0u, ops_on = 0u;

    setvbuf(stdout, NULL, _IONBF, 0);
    sc_install_silent_handler();
    cplus_runtime_set_violation_trap(0);
    cplus_safe_set_unknown_capacity_mode(1);
    assert(cplus_safe_unknown_capacity_strict() == 1);

    sc_silence_stderr();
    digest_off = run_pass(0);
    ops_off = g_ops;
    digest_on = run_pass(1);
    ops_on = g_ops;
    sc_restore_stderr();

    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    assert(digest_off == digest_on);
    assert(ops_off == ops_on);
    printf("safec_layer_memory: digest=%016llx\n", (unsigned long long)digest_off);
    printf("safec_layer_memory: %lu operations, 2 logging passes, parity ok\n", ops_off);
    return 0;
}
