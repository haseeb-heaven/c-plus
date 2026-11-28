/* Deterministic fuzz coverage: checked ORDINARY-NAME string C layer.
 *
 * strcpy/strncpy/strcat/strncat are called through the ordinary-name macros
 * of stdlib/safe/safe_c.h. Sources and destinations are guarded objects with
 * 16-byte canaries; the harness itself never writes outside a declared
 * object. One-over source lengths (513) and one-over n values are passed only
 * to the checked wrapper, whose refusal (errno == ERANGE, canaries intact) is
 * asserted. Safeclib nulls a destination on a string constraint violation, so
 * destination bytes are not asserted in the refusal branches.
 *
 * Logging OFF vs TRACE replay must yield the identical FNV-1a digest.
 */
#include "safec_layer_common.h"

#include "stdlib/safe/safe_c.h"
#include "stdlib/safe/safe_log.h"
#include "stdlib/safe/safe_runtime.h"

static unsigned long g_ops;

static size_t src_len_for(int iter) {
    size_t r = 1u + (size_t)(sc_rnd() % 64u);
    if (iter % 8 == 1) return SC_LEN_MAX;      /* exact fit: 512 chars + NUL */
    if (iter % 8 == 2) return SC_LEN_MAX + 1u; /* one-over source */
    return r;
}

static void run_iteration(int iter) {
    size_t n = sc_len_for(iter);
    size_t slen = src_len_for(iter);

    /* 1. strcpy: exact fit at 512 chars, refusal at 513. */
    {
        sc_src_obj src;
        sc_str_obj dst;
        size_t i;
        sc_init_src(&src, slen);
        sc_init_dest(&dst, 0u);
        errno = 0;
        (void)strcpy(dst.data, src.data);
        sc_check_canaries(src.pre, src.post);
        sc_check_canaries(dst.pre, dst.post);
        if (slen > SC_LEN_MAX) {
            assert(errno == ERANGE);
            assert(dst.data[0] == '\0'); /* safeclib clears dest on refusal */
        } else {
            assert(errno == 0);
            for (i = 0; i < slen; ++i) assert(dst.data[i] == src.data[i]);
            assert(dst.data[slen] == '\0');
        }
        sc_hash_tag("Y");
        sc_hash_u64((uint64_t)slen);
        sc_hash_u64((uint64_t)(slen > SC_LEN_MAX));
        sc_hash(&dst, sizeof dst);
        ++g_ops;
    }

    /* 2. strncpy: copy up to n; safeclib needs room for a NUL, so a source
     *    with no NUL inside the first n chars is refused, as is n >= 513. */
    {
        sc_src_obj src;
        sc_str_obj dst;
        size_t i, m;
        int refuse = (n >= SC_LEN_MAX + 1u);
        sc_init_src(&src, slen);
        sc_init_dest(&dst, 0u);
        errno = 0;
        (void)strncpy(dst.data, src.data, n);
        sc_check_canaries(src.pre, src.post);
        sc_check_canaries(dst.pre, dst.post);
        if (refuse) {
            assert(errno == ERANGE);
        } else {
            assert(errno == 0);
            m = (slen < n) ? slen : n;
            for (i = 0; i < m; ++i) assert(dst.data[i] == src.data[i]);
            assert(dst.data[m] == '\0');
        }
        sc_hash_tag("N");
        sc_hash_u64((uint64_t)n);
        sc_hash_u64((uint64_t)slen);
        sc_hash_u64((uint64_t)refuse);
        sc_hash(&dst, sizeof dst);
        ++g_ops;
    }

    /* 3. strcat: short prefix normally; exact-fit and one-over are forced. */
    {
        sc_src_obj src;
        sc_str_obj dst;
        char prefix[SC_LEN_MAX + 1u];
        size_t dlen = 0u, i;
        int refuse;
        if (iter % 8 == 1) dlen = 0u;        /* slen 512 + NUL == 513: fits */
        else if (iter % 8 == 2) dlen = 1u;   /* slen 513 + prefix: refused */
        else dlen = (size_t)(sc_rnd() % 24u);
        sc_init_src(&src, slen);
        sc_init_dest(&dst, dlen);
        memcpy(prefix, dst.data, sizeof prefix);
        refuse = (dlen + slen + 1u > SC_LEN_MAX + 1u);
        errno = 0;
        (void)strcat(dst.data, src.data);
        sc_check_canaries(src.pre, src.post);
        sc_check_canaries(dst.pre, dst.post);
        if (refuse) {
            assert(errno == ERANGE);
        } else {
            assert(errno == 0);
            for (i = 0; i < dlen; ++i) assert(dst.data[i] == prefix[i]);
            for (i = 0; i < slen; ++i) assert(dst.data[dlen + i] == src.data[i]);
            assert(dst.data[dlen + slen] == '\0');
        }
        sc_hash_tag("A");
        sc_hash_u64((uint64_t)dlen);
        sc_hash_u64((uint64_t)slen);
        sc_hash_u64((uint64_t)refuse);
        sc_hash(&dst, sizeof dst);
        ++g_ops;
    }

    /* 4. strncat: prefix + at most n source chars + terminating NUL. */
    {
        sc_src_obj src;
        sc_str_obj dst;
        char prefix[SC_LEN_MAX + 1u];
        size_t dlen = (size_t)(sc_rnd() % 16u);
        size_t i, m;
        int refuse;
        sc_init_src(&src, slen);
        sc_init_dest(&dst, dlen);
        memcpy(prefix, dst.data, sizeof prefix);
        m = (slen < n) ? slen : n;
        refuse = (n >= SC_LEN_MAX + 1u) || (dlen + m + 1u > SC_LEN_MAX + 1u);
        errno = 0;
        (void)strncat(dst.data, src.data, n);
        sc_check_canaries(src.pre, src.post);
        sc_check_canaries(dst.pre, dst.post);
        if (refuse) {
            assert(errno == ERANGE);
        } else {
            assert(errno == 0);
            for (i = 0; i < dlen; ++i) assert(dst.data[i] == prefix[i]);
            for (i = 0; i < m; ++i) assert(dst.data[dlen + i] == src.data[i]);
            assert(dst.data[dlen + m] == '\0');
        }
        sc_hash_tag("T");
        sc_hash_u64((uint64_t)n);
        sc_hash_u64((uint64_t)dlen);
        sc_hash_u64((uint64_t)refuse);
        sc_hash(&dst, sizeof dst);
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

    sc_silence_stderr();
    digest_off = run_pass(0);
    ops_off = g_ops;
    digest_on = run_pass(1);
    ops_on = g_ops;
    sc_restore_stderr();

    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    assert(digest_off == digest_on);
    assert(ops_off == ops_on);
    printf("safec_layer_string: digest=%016llx\n", (unsigned long long)digest_off);
    printf("safec_layer_string: %lu operations, 2 logging passes, parity ok\n", ops_off);
    return 0;
}
