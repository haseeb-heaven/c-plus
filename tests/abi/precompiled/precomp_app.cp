/* C+ application linked against the ALREADY-BUILT precompiled C library.
 *
 * Build (see build_app_against_precompiled.sh):
 *   ./build/c+ build --sanitize=none tests/abi/precompiled/precomp_app.cp \
 *     -o build/abi-precompiled/app_static \
 *     -- build/abi-precompiled/libprecomp.a \
 *        -I$ROOT/tests/abi/precompiled \
 *        -DPRECOMP_REBUILD_NONCE=<n> -Wno-unused-command-line-argument
 *
 * The .a / .dylib is never recompiled here.  The app does, however, include
 * the C+ stdlib (stdlib/vector.hp) on purpose: this is the part that must be
 * rebuilt whenever stdlib sources change, while the saved C binary stays
 * valid.  All identifiers use a precomp_ prefix, struct copies use
 * __builtin_memcpy, there is no variable division/modulo, and every
 * expression is bound to a named temporary before use.
 */
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"

#include "precomp_shared.h"

#ifndef PRECOMP_REBUILD_NONCE
#define PRECOMP_REBUILD_NONCE 0
#endif

#define PRECOMP_EXPECTED_VERSION 3

static int precomp_checks;
static int precomp_failed;

static void precomp_ck(const char *name, int ok)
{
    ++precomp_checks;
    if (!ok)
        ++precomp_failed;
    printf("%-56s %s\n", name, ok ? "OK" : "MISMATCH");
}

int main(void)
{
    precomp_record precomp_observed;
    precomp_record precomp_bad;
    unsigned char precomp_bytes[8];
    vector precomp_vec;
    int precomp_sum;
    int precomp_nullsum;
    int precomp_at;
    int precomp_total;
    size_t precomp_c_size;
    size_t precomp_c_align;

    printf("precomp_app: nonce=%d\n", PRECOMP_REBUILD_NONCE);
    printf("precomp_app: C+ sizeof(record)=%llu align=%llu\n",
           (unsigned long long)sizeof(precomp_record),
           (unsigned long long)__alignof__(precomp_record));

    /* ---- scalar behavior of the saved binary ---------------------------- */
    precomp_ck("lib version == 3", precomp_lib_version() == PRECOMP_EXPECTED_VERSION);
    precomp_ck("precomp_mul(20,22) == 440", precomp_mul(20, 22) == 440);

    precomp_bytes[0] = (unsigned char)1;
    precomp_bytes[1] = (unsigned char)2;
    precomp_bytes[2] = (unsigned char)3;
    precomp_bytes[3] = (unsigned char)4;
    precomp_bytes[4] = (unsigned char)5;
    precomp_bytes[5] = (unsigned char)6;
    precomp_bytes[6] = (unsigned char)7;
    precomp_bytes[7] = (unsigned char)8;
    precomp_sum = precomp_checksum(precomp_bytes, (size_t)8);
    precomp_nullsum = precomp_checksum((const unsigned char *)0, (size_t)0);
    precomp_ck("checksum(byte[8]) == 36", precomp_sum == 36);
    precomp_ck("checksum(NULL,0) == -1", precomp_nullsum == -1);

    /* ---- data layout: C library view vs captured contract --------------- */
    precomp_c_size = precomp_record_sizeof();
    precomp_c_align = precomp_record_alignof();
    precomp_ck("lib sizeof(record) == 32", precomp_c_size == (size_t)32);
    precomp_ck("lib alignof(record) == 8", precomp_c_align == (size_t)8);
    precomp_ck("lib offset i == 0", precomp_offset_i() == (size_t)0);
    precomp_ck("lib offset u == 4", precomp_offset_u() == (size_t)4);
    precomp_ck("lib offset d == 8", precomp_offset_d() == (size_t)8);
    precomp_ck("lib offset p == 16", precomp_offset_p() == (size_t)16);
    precomp_ck("lib offset n == 24", precomp_offset_n() == (size_t)24);

    /* ---- data layout: C+ view must equal the saved binary's view -------- */
    precomp_ck("C+ sizeof(record) == lib sizeof",
               sizeof(precomp_record) == precomp_c_size);
    precomp_ck("C+ alignof(record) == lib alignof",
               __alignof__(precomp_record) == precomp_c_align);
    precomp_ck("C+ sizeof(record) == 32", sizeof(precomp_record) == (size_t)32);
    precomp_ck("C+ alignof(record) == 8", __alignof__(precomp_record) == (size_t)8);

    /* ---- filled by C, interpreted by C+ (layout agreement in bytes) ----- */
    __builtin_memset(&precomp_observed, 0, sizeof precomp_observed);
    precomp_fill_record(&precomp_observed);
    precomp_ck("C filled record.i", precomp_observed.i == -12345);
    precomp_ck("C filled record.u", precomp_observed.u == 0xabcdefu);
    precomp_ck("C filled record.d", precomp_observed.d == 3.5);
    precomp_ck("C filled record.p non-null", precomp_observed.p != (void *)0);
    precomp_ck("C filled record.n", precomp_observed.n == (size_t)7);

    /* ---- C+ passes the record back into the saved binary ---------------- */
    precomp_ck("C+ -> C verify BY VALUE",
               precomp_verify_record_by_value(precomp_observed) == 0);
    precomp_ck("C+ -> C verify BY POINTER",
               precomp_verify_record_by_ptr(&precomp_observed) == 0);
    precomp_ck("C+ -> C NULL pointer refused",
               precomp_verify_record_by_ptr((const precomp_record *)0) == 7);

    __builtin_memcpy(&precomp_bad, &precomp_observed, sizeof precomp_bad);
    precomp_bad.u = 1u;
    precomp_ck("C detects bad record BY VALUE",
               precomp_verify_record_by_value(precomp_bad) == 2);
    precomp_ck("C detects bad record BY POINTER",
               precomp_verify_record_by_ptr(&precomp_bad) == 2);

    /* ---- stdlib dependency: this is what gets rebuilt after a stdlib edit */
    precomp_vec.push_back(3);
    precomp_vec.push_back(5);
    precomp_vec.push_back(7);
    precomp_ck("stdlib vector size == 3", precomp_vec.size() == (size_t)3);
    precomp_at = precomp_vec.at(1);
    precomp_ck("stdlib vector at(1) == 5", precomp_at == 5);
    precomp_total = precomp_vec.at(0) + precomp_vec.at(1) + precomp_vec.at(2);
    precomp_ck("stdlib vector sum == 15", precomp_total == 15);
    precomp_vec.drop();
    precomp_ck("stdlib vector drop clears size", precomp_vec.size() == (size_t)0);

    printf("PRECOMP_APP %d %d\n", precomp_checks, precomp_failed);
    return precomp_failed == 0 ? 0 : 1;
}
