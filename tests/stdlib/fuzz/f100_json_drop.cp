/* F-093 -- deterministic fuzz config: post-drop refusal without UAF.
 *
 * Config:  F-093 (f100 json family)
 * Seed:    0xC0FFEE93 (fixed)
 * Ops:     128 cycles x 8 refusal checks = 1024 post-drop operations.
 * Build:   --strict --sanitize=none and --strict --sanitize=undefined, each
 *          with -Ithird_party/yyjson -Lbuild/ext -lyyjson as separate operands.
 * Run:     timeout 30 <out>.  Watchdog alarm(8) -> _Exit(99).
 *
 * The contract under test is the DOCUMENTED one: json::drop() clears the owned
 * document pointer, so subsequent root()/at()/member() calls return invalid
 * borrowed views whose getters and write() refuse without dereferencing freed
 * storage; json_builder::drop() clears its document/root pointers so all
 * mutators and write() refuse.  Triple-drop must be idempotent.  This is the
 * UAF-free refusal path -- the API never touches the freed yyjson document.
 *
 * SKIP (documented, not faked): a json_view CAPTURED before drop is a dangling
 * borrow by contract ("valid only while its owner lives"); using it after
 * drop is caller UB and the wrapper does not promise refusal.  ASan is also
 * unavailable on this host, so a UAF there cannot be honestly measured here.
 *
 * Last lines: "F093_DROP <checks> <failures>" and "F093_DIGEST <hex>".
 */
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"

#define F093_SEED 0xC0FFEE93ULL
#define F093_CYCLES 128
#define F093_WATCHDOG_SECS 8

static unsigned int f093_failures = 0;
static unsigned int f093_checks = 0;

static void f093_check(const char *tag, int cond) {
    f093_checks++;
    if (!cond)
        f093_failures++;
    print(tag, cond ? 1 : 0);
}

static void f093_on_alarm(int sig) {
    (void)sig;
    _Exit(99);
}

static unsigned long long f093_rnd(unsigned long long *s) {
    *s = *s * 6364136223846793005ULL + 1442695040888963407ULL;
    return *s >> 33;
}

#define F093_FNV_PRIME 1099511628211ULL
static unsigned long long f093_mix(unsigned long long d, unsigned long long v) {
    return (d ^ v) * F093_FNV_PRIME;
}

int main(void) {
    error f093_err = error();
    unsigned long long f093_state = F093_SEED;
    unsigned long long f093_digest = 1469598103934665603ULL;
    char f093_hex[32];
    int f093_i;
    int f093_all_ok = 1;
    int f093_builder_ok = 1;

    signal(SIGALRM, f093_on_alarm);
    alarm(F093_WATCHDOG_SECS);

    for (f093_i = 0; f093_i < F093_CYCLES; f093_i++) {
        unsigned long long r = f093_rnd(&f093_state);
        int pick = (int)(r % 3ULL);
        const char *text;
        const char *key;
        json f093_doc;
        int cycle_ok = 1;

        if (pick == 0) {
            text = "{\"k\":42,\"s\":\"abc\"}";
            key = "k";
        } else if (pick == 1) {
            text = "[1,2,3]";
            key = "k";
        } else {
            text = "{\"k\":true}";
            key = "k";
        }
        f093_doc = json(text, &f093_err);
        if (!f093_doc.valid()) {
            cycle_ok = 0;
        } else {
            f093_doc.drop();
            f093_doc.drop();
            f093_doc.drop();
            if (f093_doc.valid())
                cycle_ok = 0;
        }

        /* Every view obtained from the dropped document must be invalid. */
        {
            json_view f093_root = f093_doc.root();
            json_view f093_child;
            json_view f093_member;
            int iv = 4242;
            double dv = 4242.0;
            int bv = 4242;
            const char *sv = (const char *)0;
            char *dump;

            if (f093_root.valid())
                cycle_ok = 0;
            if (f093_root.size() != 0)
                cycle_ok = 0;
            if (f093_root.is_object() || f093_root.is_array() ||
                f093_root.is_number() || f093_root.is_string() ||
                f093_root.is_bool())
                cycle_ok = 0;
            f093_child = f093_root.at(0);
            f093_member = f093_root.member(key);
            if (f093_child.valid() || f093_member.valid())
                cycle_ok = 0;
            if (f093_root.get_int(&iv, &f093_err) != STDLIB_FAIL || iv != 4242)
                cycle_ok = 0;
            if (f093_root.get_double(&dv, &f093_err) != STDLIB_FAIL ||
                dv != 4242.0)
                cycle_ok = 0;
            if (f093_root.get_bool(&bv, &f093_err) != STDLIB_FAIL || bv != 4242)
                cycle_ok = 0;
            if (f093_root.get_string(&sv, &f093_err) != STDLIB_FAIL ||
                sv != (const char *)0)
                cycle_ok = 0;
            if (f093_err.category() != STDLIB_CATEGORY_INVALID)
                cycle_ok = 0;
            dump = f093_root.write((size_t *)0);
            if (dump != (char *)0)
                cycle_ok = 0;
            json_free(dump);            /* no-op on NULL */
        }
        f093_doc.drop();                /* fourth drop still inert */

        /* Builder post-drop refusal in each cycle. */
        {
            json_builder f093_bld = json_builder();
            int bok = 1;
            if (!f093_bld.valid()) {
                bok = 0;
            } else {
                if (f093_bld.add_int("k", 1, &f093_err) != STDLIB_OK)
                    bok = 0;
                f093_bld.drop();
                f093_bld.drop();
                f093_bld.drop();
                if (f093_bld.valid())
                    bok = 0;
                if (f093_bld.add_int("k", 1, &f093_err) != STDLIB_FAIL)
                    bok = 0;
                if (f093_bld.add_double("k", 1.0, &f093_err) != STDLIB_FAIL)
                    bok = 0;
                if (f093_bld.add_bool("k", 1, &f093_err) != STDLIB_FAIL)
                    bok = 0;
                if (f093_bld.add_string("k", "v", &f093_err) != STDLIB_FAIL)
                    bok = 0;
                if (f093_bld.write((size_t *)0) != (char *)0)
                    bok = 0;
            }
            if (!bok)
                f093_builder_ok = 0;
        }

        if (!cycle_ok)
            f093_all_ok = 0;
        f093_digest = f093_mix(f093_digest, (unsigned long long)pick);
        f093_digest = f093_mix(f093_digest, (unsigned long long)cycle_ok);
    }
    alarm(0);

    f093_check("f093_document_post_drop_refusal", f093_all_ok == 1);
    f093_check("f093_builder_post_drop_refusal", f093_builder_ok == 1);
    f093_check("f093_op_count", f093_i == F093_CYCLES);
    print("SKIP f093_precapture_view_after_owner_drop reason=json_view_borrows_owner_dangling_use_is_caller_UB_and_ASan_unavailable_on_host");

    snprintf(f093_hex, sizeof(f093_hex), "%016llx", f093_digest);
    print("F093_DIGEST", f093_hex);
    print("F093_DROP", f093_checks, f093_failures);
    return f093_failures == 0 ? 0 : 1;
}
